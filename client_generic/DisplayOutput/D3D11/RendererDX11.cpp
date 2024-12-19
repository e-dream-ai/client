#include "RendererDX11.h"
#include "DisplayDX11.h"
#include "Log.h"
#include <cassert>

namespace DisplayOutput {

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

    return true;
}

bool CRendererDX11::CreateRenderTargets()
{
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
    if (drawn && m_context) {
        // Present the frame
        return true;
    }
    return false;
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
    return nullptr;
}

spCBaseText CRendererDX11::NewText(spCBaseFont _font, const std::string& _text) {
    return nullptr;
}

spCShader CRendererDX11::NewShader(const char* _pVertexShader, const char* _pFragmentShader,
                                  std::vector<std::pair<std::string, eUniformType>> uniforms) {
    return nullptr;
}

// TODO
void CRendererDX11::DrawText(spCBaseText _text, const Base::Math::CVector4& _color) {
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
    if (!m_quadVertexBuffer || !m_quadIndexBuffer)
    {
        if (!CreateQuadBuffers())
            return;
    }

    // Update shader uniforms
    struct QuadUniforms
    {
        float rect[4];   // x, y, width, height
        float uvRect[4]; // x, y, width, height
        float color[4];  // r, g, b, a
        float brightness;
    } uniforms;

    uniforms.rect[0] = _rect.m_X0;
    uniforms.rect[1] = _rect.m_Y0;
    uniforms.rect[2] = _rect.m_X1 - _rect.m_X0;
    uniforms.rect[3] = _rect.m_Y1 - _rect.m_Y0;

    uniforms.uvRect[0] = _uvRect.m_X0;
    uniforms.uvRect[1] = _uvRect.m_Y0;
    uniforms.uvRect[2] = _uvRect.m_X1 - _uvRect.m_X0;
    uniforms.uvRect[3] = _uvRect.m_Y1 - _uvRect.m_Y0;

    uniforms.color[0] = _color.m_X;
    uniforms.color[1] = _color.m_Y;
    uniforms.color[2] = _color.m_Z;
    uniforms.color[3] = _color.m_W;
    uniforms.brightness = m_Brightness;

    // Apply active shader and upload uniforms
    auto activeShader =
        std::static_pointer_cast<CShaderDX11>(m_spSelectedShader);
    if (activeShader)
    {
        activeShader->Bind();
        activeShader->Set("QuadUniforms", uniforms.rect[0], uniforms.rect[1],
                          uniforms.rect[2], uniforms.rect[3]);
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

    if (activeShader)
    {
        activeShader->Unbind();
    }
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