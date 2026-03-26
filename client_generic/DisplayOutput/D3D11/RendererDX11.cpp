#include "RendererDX11.h"
#include "DisplayDX11.h"
#include "Log.h"
#include <cassert>
#include <cstring>

#ifdef WIN32
#include "FirstTimeSetupWin32.h"
#include <windows.h>
#endif

namespace DisplayOutput {

// Some Windows DX11 builds do not link DisplayOutput/Renderer/Text.cpp into this target.
// Provide local base text definitions to satisfy linkage for DX11 text objects.
CBaseText::CBaseText() {}
CBaseText::~CBaseText() {}

namespace {
const char* kQuadPassVertexHlsl = R"(
struct VSInput {
    float3 pos : POSITION;
    float2 uv  : TEXCOORD0;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

VSOutput main(VSInput input) {
    VSOutput output;
    output.position = float4(input.pos.xy * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), input.pos.z, 1.0f);
    output.uv = input.uv;
    return output;
}
)";

const char* kDrawTextureFragmentHlsl = R"(
Texture2D tx0 : register(t0);
SamplerState smp0 : register(s0);

cbuffer QuadUniforms : register(b0) {
    float4 rect;
    float4 uvRect;
    float4 color;
    float brightness;
    float3 padding;
};

struct PSInput {
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET {
    float2 adjustedUV = (input.uv - rect.xy) / rect.zw;
    if (adjustedUV.x <= 0.0f || adjustedUV.x >= 1.0f ||
        adjustedUV.y <= 0.0f || adjustedUV.y >= 1.0f) {
        discard;
    }

    adjustedUV = (adjustedUV + uvRect.xy) * uvRect.zw;
    float4 sampled = tx0.Sample(smp0, adjustedUV);
    sampled.rgb += brightness;
    return float4(sampled.rgb * color.rgb, sampled.a * color.a);
}
)";

const char* kDrawDecodedFrameFragmentHlsl = R"(
Texture2D tx0 : register(t0);
SamplerState smp0 : register(s0);

cbuffer QuadUniforms : register(b0) {
    float4 rect;
    float4 uvRect;
    float4 color;
    float brightness;
    float3 padding;
};

struct PSInput {
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET {
    float2 adjustedUV = (input.uv - rect.xy) / rect.zw;
    adjustedUV = (adjustedUV + uvRect.xy) * uvRect.zw;
    float4 sampled = tx0.Sample(smp0, adjustedUV);
    sampled.rgb += brightness;
    return float4(sampled.rgb * color.rgb, color.a);
}
)";

bool CreateDefaultSampler(ID3D11Device* device, ID3D11SamplerState** outSampler)
{
    if (!device || !outSampler)
        return false;

    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samplerDesc.MinLOD = 0.0f;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

    HRESULT hr = device->CreateSamplerState(&samplerDesc, outSampler);
    return SUCCEEDED(hr);
}

class CFontDX11Gdi final : public CBaseFont
{
public:
    CFontDX11Gdi() : m_hFont(nullptr) {}
    ~CFontDX11Gdi() override
    {
        if (m_hFont)
            DeleteObject(m_hFont);
    }

    bool Create() override { return EnsureCreated(); }

    bool EnsureCreated()
    {
        if (m_hFont)
            return true;

        LOGFONTA lf = {};
        lf.lfHeight = -static_cast<LONG>(FontDescription().Height());
        lf.lfWeight = FW_NORMAL;
        switch (FontDescription().Style())
        {
        case CFontDescription::Thin: lf.lfWeight = FW_THIN; break;
        case CFontDescription::Light: lf.lfWeight = FW_LIGHT; break;
        case CFontDescription::Normal: lf.lfWeight = FW_NORMAL; break;
        case CFontDescription::Bold: lf.lfWeight = FW_BOLD; break;
        case CFontDescription::UberBold: lf.lfWeight = FW_EXTRABOLD; break;
        }
        lf.lfItalic = FontDescription().Italic() ? TRUE : FALSE;
        lf.lfUnderline = FontDescription().Underline() ? TRUE : FALSE;
        lf.lfQuality = FontDescription().AntiAliased() ? ANTIALIASED_QUALITY : NONANTIALIASED_QUALITY;
        std::strncpy(lf.lfFaceName, FontDescription().TypeFace().c_str(), LF_FACESIZE - 1);
        lf.lfFaceName[LF_FACESIZE - 1] = '\0';

        m_hFont = CreateFontIndirectA(&lf);
        return m_hFont != nullptr;
    }

    HFONT Handle() { return EnsureCreated() ? m_hFont : nullptr; }

private:
    HFONT m_hFont;
};

class CTextDX11Gdi final : public CBaseText
{
public:
    explicit CTextDX11Gdi(std::shared_ptr<CFontDX11Gdi> font, std::string text, HWND hwnd,
                          uint32_t displayWidth, uint32_t displayHeight)
        : m_font(std::move(font)),
          m_text(std::move(text)),
          m_enabled(true),
          m_hwnd(hwnd),
          m_displayWidth(displayWidth),
          m_displayHeight(displayHeight)
    {
    }

    void SetText(const std::string& text) override { m_text = text; }

    Base::Math::CVector2 GetExtent() override
    {
        Base::Math::CVector2 result;
        if (!m_hwnd)
            return result;
        HFONT font = m_font ? m_font->Handle() : nullptr;
        if (!font)
            return result;

        HDC hdc = GetDC(m_hwnd);
        if (!hdc)
            return result;
        HGDIOBJ oldFont = SelectObject(hdc, font);
        SIZE size = {};
        if (GetTextExtentPoint32A(hdc, m_text.c_str(), static_cast<int>(m_text.size()), &size))
        {
            const uint32_t safeW = (m_displayWidth == 0u) ? 1u : m_displayWidth;
            const uint32_t safeH = (m_displayHeight == 0u) ? 1u : m_displayHeight;
            const float w = static_cast<float>(safeW);
            const float h = static_cast<float>(safeH);
            result = Base::Math::CVector2(size.cx / w, size.cy / h);
        }
        SelectObject(hdc, oldFont);
        ReleaseDC(m_hwnd, hdc);
        return result;
    }

    void SetEnabled(bool enabled) override { m_enabled = enabled; }
    bool Enabled() const { return m_enabled; }
    const std::string& Text() const { return m_text; }
    std::shared_ptr<CFontDX11Gdi> Font() const { return m_font; }

private:
    std::shared_ptr<CFontDX11Gdi> m_font;
    std::string m_text;
    bool m_enabled;
    HWND m_hwnd;
    uint32_t m_displayWidth;
    uint32_t m_displayHeight;
};
} // namespace

CRendererDX11::CRendererDX11() : CRenderer() {
}

CRendererDX11::~CRendererDX11() {
    // ComPtr handles cleanup automatically
}

bool CRendererDX11::Initialize(spCDisplayOutput _spDisplay) {
    if (!CRenderer::Initialize(_spDisplay))
        return false;
     
    auto display = static_cast<CDisplayDX11*>(_spDisplay.get());
    m_device = display->GetDevice();
    m_context = display->GetContext();

    if (!m_device || !m_context)
    {
        g_Log->Error("Invalid D3D11 device or context");
        return false;
    }

    if (!CreateRenderTargets())
        return false;

    if (!CreateBlendStates())
        return false;

    if (!CreateDefaultSampler(m_device.Get(), &m_defaultSampler))
    {
        g_Log->Error("Failed to create default DX11 sampler");
        return false;
    }

    // Mirror Metal initialization so generic texture rendering has a default shader.
    m_drawTextureShader = NewShader("quadPassVertex", "drawTextureFragment");

    return true;
}

bool CRendererDX11::CreateRenderTargets()
{
    g_Log->Info("Creating render targets, display dimensions: %dx%d",
                m_spDisplay->Width(), m_spDisplay->Height());
    // Get the backbuffer texture
    ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr =
        m_spDisplay->GetSwapChain()->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr))
    {
        g_Log->Error("Failed to get back buffer: %08X", hr);
        return false;
    }

    // Check we have a backbuffer
    if (!backBuffer)
    {
        g_Log->Error("Null backbuffer obtained from swap chain");
        return false;
    }

    // Log back buffer properties
    D3D11_TEXTURE2D_DESC backBufferDesc;
    backBuffer->GetDesc(&backBufferDesc);
    g_Log->Info("Back buffer format: %d, dimensions: %dx%d",
                backBufferDesc.Format, backBufferDesc.Width,
                backBufferDesc.Height);

    // Create render target view
    hr = m_device->CreateRenderTargetView(backBuffer.Get(), nullptr,
                                          &m_renderTargetView);
    if (FAILED(hr))
    {
        g_Log->Error("Failed to create render target view: %08X", hr);
        return false;
    }

    // Create depth stencil texture
    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = m_spDisplay->Width();
    depthDesc.Height = m_spDisplay->Height();
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    ComPtr<ID3D11Texture2D> depthStencil;
    hr = m_device->CreateTexture2D(&depthDesc, nullptr, &depthStencil);
    if (FAILED(hr))
    {
        g_Log->Error("Failed to create depth stencil texture: %08X", hr);
        return false;
    }

    // Create depth stencil view
    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = depthDesc.Format;
    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;

    hr = m_device->CreateDepthStencilView(depthStencil.Get(), &dsvDesc,
                                          &m_depthStencilView);
    if (FAILED(hr))
    {
        g_Log->Error("Failed to create depth stencil view: %08X", hr);
        return false;
    }

    // Set the viewport
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(m_spDisplay->Width());
    viewport.Height = static_cast<float>(m_spDisplay->Height());
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);

    return true;
}

void CRendererDX11::PrepareForSwapChainResize()
{
    // CRTV for the swap chain buffer holds a ref; DXGI ResizeBuffers requires all refs released.
    m_renderTargetView.Reset();
    m_depthStencilView.Reset();
}

bool CRendererDX11::RecreateRenderTargetsAfterResize()
{
    m_renderTargetView.Reset();
    m_depthStencilView.Reset();
    return CreateRenderTargets();
}

bool CRendererDX11::CreateBlendStates() {
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable = false;
    blendDesc.IndependentBlendEnable = false;
    auto& rtDesc = blendDesc.RenderTarget[0];
    rtDesc.BlendEnable = true;
    rtDesc.SrcBlend = D3D11_BLEND_SRC_ALPHA;
    rtDesc.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    rtDesc.BlendOp = D3D11_BLEND_OP_ADD;
    rtDesc.SrcBlendAlpha = D3D11_BLEND_ONE;
    rtDesc.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    rtDesc.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    rtDesc.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    HRESULT hr = m_device->CreateBlendState(&blendDesc, &m_blendState);
    if (FAILED(hr)) {
        g_Log->Error("Failed to create blend state: %08X", hr);
        return false;
    }

    return true;
}

void CRendererDX11::Defaults() {
    // Set default render states
}

bool CRendererDX11::BeginFrame() {
    m_pendingTextDraws.clear();

    if (!CRenderer::BeginFrame())
        return false;

    if (m_context && m_renderTargetView) {
        ID3D11RenderTargetView* rtv = m_renderTargetView.Get();
        m_context->OMSetRenderTargets(1, &rtv, m_depthStencilView.Get());

        float blendFactor[4] = {0.f, 0.f, 0.f, 0.f};
        m_context->OMSetBlendState(m_blendState.Get(), blendFactor, 0xFFFFFFFF);
    }

    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(m_spDisplay->Width());
    viewport.Height = static_cast<float>(m_spDisplay->Height());
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);

    // Clear render target and depth buffer
    if (m_context && m_renderTargetView) {
        float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        m_context->ClearRenderTargetView(m_renderTargetView.Get(), clearColor);
        if (m_depthStencilView) {
            m_context->ClearDepthStencilView(m_depthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
        }

        return true;
    }
    return false;
}

bool CRendererDX11::EndFrame(bool drawn) {
#ifdef WIN32
    // Base CRenderer::EndFrame returns false when !drawn, which would skip overlay + Present.
    bool forcePresent = false;
    if (m_context && m_renderTargetView && m_spDisplay) {
        forcePresent = FirstTimeSetupWin32_RenderIfNeeded(
            m_device.Get(), m_context.Get(), m_renderTargetView.Get(),
            static_cast<float>(m_spDisplay->Width()),
            static_cast<float>(m_spDisplay->Height()));
    }
    const bool effectiveDrawn = drawn || forcePresent;
    if (!CRenderer::EndFrame(effectiveDrawn))
        return false;

    if (effectiveDrawn && m_context && m_spDisplay) {
        m_spDisplay->SwapBuffers();

        auto display = std::dynamic_pointer_cast<CDisplayDX11>(m_spDisplay);
        if (display && display->GetWindowHandle() && !m_pendingTextDraws.empty())
        {
            HDC hdc = GetDC(display->GetWindowHandle());
            if (hdc)
            {
                SetBkMode(hdc, TRANSPARENT);
                for (const auto& draw : m_pendingTextDraws)
                {
                    auto text = std::dynamic_pointer_cast<CTextDX11Gdi>(draw.text);
                    if (!text || !text->Enabled())
                        continue;
                    auto font = text->Font();
                    HFONT hFont = font ? font->Handle() : nullptr;
                    if (!hFont)
                        continue;

                    HGDIOBJ oldFont = SelectObject(hdc, hFont);
                    COLORREF c = RGB(
                        static_cast<int>(draw.color.m_X * 255.0f),
                        static_cast<int>(draw.color.m_Y * 255.0f),
                        static_cast<int>(draw.color.m_Z * 255.0f));
                    SetTextColor(hdc, c);

                    const Base::Math::CRect r = text->GetRect();
                    const int left = static_cast<int>(r.m_X0 * static_cast<float>(display->Width()));
                    const int top = static_cast<int>(r.m_Y0 * static_cast<float>(display->Height()));
                    RECT rc = {left, top, static_cast<int>(display->Width()), static_cast<int>(display->Height())};
                    DrawTextA(hdc, text->Text().c_str(), -1, &rc, DT_LEFT | DT_TOP | DT_NOCLIP);
                    SelectObject(hdc, oldFont);
                }
                ReleaseDC(display->GetWindowHandle(), hdc);
            }
        }
        return true;
    }
    return false;
#else
    if (!CRenderer::EndFrame(drawn))
        return false;

    if (drawn && m_context && m_spDisplay) {
        m_spDisplay->SwapBuffers();
        return true;
    }
    return false;
#endif
}

void CRendererDX11::Apply() {
    CRenderer::Apply();
    // Apply current render states
}

void CRendererDX11::Reset(const uint32_t _flags) {
    CRenderer::Reset(_flags);
    // Reset render states based on flags
}

spCTextureFlat CRendererDX11::NewTextureFlat(const uint32_t flags)
{
    return std::make_shared<CTextureFlatDX11>(m_device, m_context, flags);
}

spCTextureFlat CRendererDX11::NewTextureFlat(spCImage _spImage,
                                             const uint32_t flags)
{
    spCTextureFlatDX11 texture =
        std::make_shared<CTextureFlatDX11>(m_device, m_context, flags);
    texture->Upload(_spImage);
    return texture;
}

// TODO: remaining resource creation stubs
spCBaseFont CRendererDX11::GetFont(CFontDescription& _desc) {
    const std::string key = _desc.TypeFace() + "#" + std::to_string(static_cast<int>(_desc.Height())) +
                            "#" + std::to_string(static_cast<int>(_desc.Style())) +
                            "#" + (_desc.Italic() ? "i" : "n") +
                            "#" + (_desc.Underline() ? "u" : "n") +
                            "#" + (_desc.AntiAliased() ? "aa" : "na");
    auto it = m_fontPool.find(key);
    if (it != m_fontPool.end())
        return it->second;

    auto font = std::make_shared<CFontDX11Gdi>();
    font->FontDescription(_desc);
    if (!font->EnsureCreated())
    {
        g_Log->Warning("DX11 font '%s' unavailable, trying Arial fallback", _desc.TypeFace().c_str());
        CFontDescription fallback = _desc;
        fallback.TypeFace("Arial");
        font->FontDescription(fallback);
        if (!font->EnsureCreated())
            return nullptr;
    }

    m_fontPool[key] = font;
    return font;
}

spCBaseText CRendererDX11::NewText(spCBaseFont _font, const std::string& _text) {
    auto font = std::dynamic_pointer_cast<CFontDX11Gdi>(_font);
    if (!font)
        return nullptr;
    auto display = std::dynamic_pointer_cast<CDisplayDX11>(m_spDisplay);
    if (!display)
        return nullptr;
    return std::make_shared<CTextDX11Gdi>(
        font, _text, display->GetWindowHandle(), display->Width(), display->Height());
}

spCShader CRendererDX11::NewShader(const char* _pVertexShader, const char* _pFragmentShader,
                                  std::vector<std::pair<std::string, eUniformType>> uniforms) {
    if (!_pVertexShader || !_pFragmentShader) {
        g_Log->Error("NewShader called with null shader names");
        return nullptr;
    }

    const std::string vertexName(_pVertexShader);
    const std::string fragmentName(_pFragmentShader);

    if (vertexName != "quadPassVertex") {
        g_Log->Warning("Unsupported DX11 vertex shader requested: %s", vertexName.c_str());
        return nullptr;
    }

    // Windows DX11 currently renders decoded RGBA frames (no YUV shader path).
    // Map all frame fragment variants to the same RGBA fragment implementation.
    const bool supportedFragment =
        fragmentName == "drawTextureFragment" ||
        fragmentName == "drawDecodedFrameNoBlendingFragment" ||
        fragmentName == "drawDecodedFrameLinearFrameBlendFragment" ||
        fragmentName == "drawDecodedFrameCubicFrameBlendFragment";

    if (!supportedFragment) {
        g_Log->Warning("Unsupported DX11 fragment shader requested: %s", fragmentName.c_str());
        return nullptr;
    }

    const char* fragmentSource = kDrawTextureFragmentHlsl;
    if (fragmentName == "drawDecodedFrameNoBlendingFragment" ||
        fragmentName == "drawDecodedFrameLinearFrameBlendFragment" ||
        fragmentName == "drawDecodedFrameCubicFrameBlendFragment") {
        fragmentSource = kDrawDecodedFrameFragmentHlsl;
    }

    auto shader = std::make_shared<CShaderDX11>(m_device, m_context);
    if (!shader->Build(kQuadPassVertexHlsl, fragmentSource)) {
        g_Log->Error("Failed building DX11 shader pair: %s / %s",
                     vertexName.c_str(), fragmentName.c_str());
        return nullptr;
    }

    for (uint32_t slot = 0; slot < uniforms.size(); ++slot) {
        shader->CreateUniform(uniforms[slot].first, uniforms[slot].second, slot);
    }

    return shader;
}

// TODO
void CRendererDX11::DrawText(spCBaseText _text, const Base::Math::CVector4& _color) {
    if (!_text)
        return;
    m_pendingTextDraws.push_back(PendingTextDraw{_text, _color});
}


bool CRendererDX11::CreateQuadBuffers()
{
    // Vertex buffer
    Vertex vertices[] = {{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
                         {{1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
                         {{0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
                         {{1.0f, 1.0f, 0.0f}, {1.0f, 1.0f}}};

    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.ByteWidth = sizeof(vertices);
    vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA vbData = {};
    vbData.pSysMem = vertices;

    HRESULT hr = m_device->CreateBuffer(&vbDesc, &vbData, &m_quadVertexBuffer);
    if (FAILED(hr))
    {
        g_Log->Error("Failed to create quad vertex buffer: %08X", hr);
        return false;
    }

    // Index buffer
    uint16_t indices[] = {0, 1, 2, 2, 1, 3};
    D3D11_BUFFER_DESC ibDesc = {};
    ibDesc.ByteWidth = sizeof(indices);
    ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
    ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

    D3D11_SUBRESOURCE_DATA ibData = {};
    ibData.pSysMem = indices;

    hr = m_device->CreateBuffer(&ibDesc, &ibData, &m_quadIndexBuffer);
    if (FAILED(hr))
    {
        g_Log->Error("Failed to create quad index buffer: %08X", hr);
        return false;
    }

    return true;
}

void CRendererDX11::DrawQuad(const Base::Math::CRect& _rect,
                             const Base::Math::CVector4& _color,
                             const Base::Math::CRect& _uvRect)
{
    static bool s_loggedMissingVertexShader = false;

    if (!m_renderTargetView)
    {
        g_Log->Error("No render target view");
        return;
    }


    if (!m_quadVertexBuffer || !m_quadIndexBuffer)
    {
        if (!CreateQuadBuffers())
            return;
    }

    if (!m_quadUniformBuffer)
    {
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.ByteWidth = sizeof(QuadUniforms);
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        HRESULT hr = m_device->CreateBuffer(&cbDesc, nullptr, &m_quadUniformBuffer);
        if (FAILED(hr))
        {
            g_Log->Error("Failed to create quad uniform buffer: %08X", hr);
            return;
        }
    }

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT mapHr = m_context->Map(m_quadUniformBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(mapHr))
    {
        g_Log->Error("Failed to map quad uniform buffer: %08X", mapHr);
        return;
    }

    auto* uniforms = reinterpret_cast<QuadUniforms*>(mapped.pData);
    uniforms->rect[0] = _rect.m_X0;
    uniforms->rect[1] = _rect.m_Y0;
    uniforms->rect[2] = _rect.Width();
    uniforms->rect[3] = _rect.Height();

    uniforms->uvRect[0] = _uvRect.m_X0;
    uniforms->uvRect[1] = _uvRect.m_Y0;
    uniforms->uvRect[2] = _uvRect.Width();
    uniforms->uvRect[3] = _uvRect.Height();

    uniforms->color[0] = _color.m_X;
    uniforms->color[1] = _color.m_Y;
    uniforms->color[2] = _color.m_Z;
    uniforms->color[3] = _color.m_W;
    uniforms->brightness = GetBrightness();
    uniforms->padding[0] = uniforms->padding[1] = uniforms->padding[2] = 0.0f;

    m_context->Unmap(m_quadUniformBuffer.Get(), 0);
    ID3D11Buffer* cb = m_quadUniformBuffer.Get();
    m_context->VSSetConstantBuffers(0, 1, &cb);
    m_context->PSSetConstantBuffers(0, 1, &cb);

    // Match Metal behavior: use default texture shader when no shader was selected.
    if (!m_spSelectedShader && m_drawTextureShader) {
        SetShader(m_drawTextureShader);
    }

    // Sync selected render state (textures/shader) before issuing draw call.
    // This avoids stale active state in CRenderer::Apply.
    Apply();

    // Ensure PS sampler slot 0 is always explicitly bound.
    ID3D11SamplerState* fallbackSampler = m_defaultSampler.Get();
    m_context->PSSetSamplers(0, 1, &fallbackSampler);

    // D3D11 always needs a vertex shader for DrawIndexed.
    ComPtr<ID3D11VertexShader> boundVertexShader;
    m_context->VSGetShader(&boundVertexShader, nullptr, nullptr);
    if (!boundVertexShader)
    {
        if (!s_loggedMissingVertexShader)
        {
            g_Log->Warning("Skipping DrawQuad: no vertex shader bound");
            s_loggedMissingVertexShader = true;
        }
        return;
    }

    // Set vertex/index buffers and draw
    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    m_context->IASetVertexBuffers(0, 1, m_quadVertexBuffer.GetAddressOf(),
                                  &stride, &offset);
    m_context->IASetIndexBuffer(m_quadIndexBuffer.Get(), DXGI_FORMAT_R16_UINT,
                                0);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Draw the quad
    m_context->DrawIndexed(6, 0, 0);
}

void CRendererDX11::DrawQuad(const Base::Math::CRect& _rect,
                             const Base::Math::CVector4& _color)
{
    DrawQuad(_rect, _color, Base::Math::CRect(0, 0, 1, 1));
}

void CRendererDX11::DrawSoftQuad(const Base::Math::CRect& _rect,
                                 const Base::Math::CVector4& _color,
                                 const float _width)
{
    // Draw inner quad
    Base::Math::CRect innerRect = _rect;
    innerRect.m_X0 += _width;
    innerRect.m_Y0 += _width;
    innerRect.m_X1 -= _width;
    innerRect.m_Y1 -= _width;

    // Draw with alpha gradient for soft edges
    Base::Math::CVector4 edgeColor = _color;
    edgeColor.m_W = 0.0f; // Transparent edges

    // Draw edges with gradient
    DrawQuad(
        Base::Math::CRect(_rect.m_X0, _rect.m_Y0, _rect.m_X1, innerRect.m_Y0),
        edgeColor); // Top
    DrawQuad(
        Base::Math::CRect(_rect.m_X0, innerRect.m_Y1, _rect.m_X1, _rect.m_Y1),
        edgeColor); // Bottom
    DrawQuad(Base::Math::CRect(_rect.m_X0, innerRect.m_Y0, innerRect.m_X0,
                               innerRect.m_Y1),
             edgeColor); // Left
    DrawQuad(Base::Math::CRect(innerRect.m_X1, innerRect.m_Y0, _rect.m_X1,
                               innerRect.m_Y1),
             edgeColor); // Right

    // Draw inner quad with full alpha
    DrawQuad(innerRect, _color);
}

} // namespace DisplayOutput