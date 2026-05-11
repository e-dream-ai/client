#include "RendererDX11.h"
#include "DisplayDX11.h"
#include "Log.h"
#include "base.h"
#include "GlyphAtlasDX11.h"
#include "Settings.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <string>
#include <d3dcompiler.h>

#ifdef WIN32
#include "AboutDialogWin32.h"
#include "FirstTimeSetupWin32.h"
#include "SettingsDialogWin32.h"
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
    if (adjustedUV.x < 0.0f || adjustedUV.x > 1.0f ||
        adjustedUV.y < 0.0f || adjustedUV.y > 1.0f) {
        discard;
    }

    adjustedUV = uvRect.xy + adjustedUV * uvRect.zw;
    float4 sampled = tx0.Sample(smp0, adjustedUV);
    sampled.rgb += brightness;
    return float4(sampled.rgb * color.rgb, sampled.a * color.a);
}
)";

// Batched HUD text shader: per-vertex position/UV/color, sampled from glyph atlas.
const char* kTextBatchVertexHlsl = R"(
struct VSInput {
    float3 pos   : POSITION;
    float2 uv    : TEXCOORD0;
    float4 color : COLOR0;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
    float4 color    : COLOR0;
};

VSOutput main(VSInput input) {
    VSOutput output;
    output.position = float4(input.pos.xy * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), input.pos.z, 1.0f);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}
)";

const char* kTextBatchFragmentHlsl = R"(
Texture2D tx0 : register(t0);
SamplerState smp0 : register(s0);

cbuffer TextBatchUniforms : register(b0) {
    float brightness;
    float3 padding;
};

struct PSInput {
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
    float4 color    : COLOR0;
};

float4 main(PSInput input) : SV_TARGET {
    float4 sampled = tx0.Sample(smp0, input.uv);
    sampled.rgb += brightness;
    return float4(sampled.rgb * input.color.rgb, sampled.a * input.color.a);
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
    adjustedUV = uvRect.xy + adjustedUV * uvRect.zw;
    float4 sampled = tx0.Sample(smp0, adjustedUV);
    sampled.rgb += brightness;
    return float4(sampled.rgb * color.rgb, color.a);
}
)";

// CLinearFrameDisplay binds decoded RGBA textures at indices 1 and 2 (see SetTexture).
const char* kDrawDecodedFrameLinearFragmentHlsl = R"(
Texture2D tx1 : register(t1);
Texture2D tx2 : register(t2);
SamplerState smp1 : register(s1);
SamplerState smp2 : register(s2);

cbuffer QuadUniforms : register(b0) {
    float4 rect;
    float4 uvRect;
    float4 color;
    float brightness;
    float3 padding;
};

cbuffer LinearDeltaCB : register(b1) {
    float delta;
    float3 _padDelta;
};

struct PSInput {
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET {
    float2 adjustedUV = (input.uv - rect.xy) / rect.zw;
    adjustedUV = uvRect.xy + adjustedUV * uvRect.zw;
    float4 c1 = tx1.Sample(smp1, adjustedUV);
    float4 c2 = tx2.Sample(smp2, adjustedUV);
    float4 blended = lerp(c1, c2, delta);
    blended.rgb += brightness;
    return float4(blended.rgb * color.rgb, color.a);
}
)";

// Shared NV12 YUV->RGB helper (BT.601 limited range, matching MetalShaders.metal SampleYUVTexturesRGBA).
// Slot layout: YUV textures are bound at i*2 (Y) and i*2+1 (UV) where i is the SetTexture index.
// No-blend:    t0=Y, t1=UV          (SetTexture index 0)
// Linear:      t2=f1Y, t3=f1UV, t4=f2Y, t5=f2UV   (indices 1 and 2)
// Cubic:       t2=f1Y,t3=f1UV, t4=f2Y,t5=f2UV, t6=f3Y,t7=f3UV, t8=f4Y,t9=f4UV (indices 1-4)
static const char* kNV12SampleHlslHelper = R"(
float4 SampleNV12(Texture2D yTex, Texture2D uvTex, SamplerState sY, SamplerState sUV, float2 uv) {
    float  y  = yTex.Sample(sY,  uv).r;
    float2 uv2 = uvTex.Sample(sUV, uv).rg;
    float  cb = uv2.x - 0.5;
    float  cr = uv2.y - 0.5;
    y = 1.164383561643836 * (y - 0.0625);
    float r = saturate(y + 1.792741071428571 * cr);
    float g = saturate(y - 0.532909328559444 * cr - 0.21324861427373 * cb);
    float b = saturate(y + 2.112401785714286 * cb);
    return float4(r, g, b, 1.0);
}
)";

// NV12 no-blend: texture index 0 → t0=Y, t1=UV
const char* kDrawDecodedFrameYUVFragmentHlsl = R"(
Texture2D  yTex  : register(t0);
Texture2D  uvTex : register(t1);
SamplerState sY  : register(s0);
SamplerState sUV : register(s1);

cbuffer QuadUniforms : register(b0) {
    float4 rect;
    float4 uvRect;
    float4 color;
    float  brightness;
    float3 padding;
};

struct PSInput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };

float4 SampleNV12(Texture2D yT, Texture2D uvT, SamplerState sY2, SamplerState sUV2, float2 uv) {
    float  y   = yT.Sample(sY2,  uv).r;
    float2 uv2 = uvT.Sample(sUV2, uv).rg;
    float  cb  = uv2.x - 0.5; float cr = uv2.y - 0.5;
    y = 1.164383561643836 * (y - 0.0625);
    return float4(saturate(y + 1.792741071428571 * cr),
                  saturate(y - 0.532909328559444 * cr - 0.21324861427373 * cb),
                  saturate(y + 2.112401785714286 * cb), 1.0);
}

float4 main(PSInput input) : SV_TARGET {
    float2 adjustedUV = (input.uv - rect.xy) / rect.zw;
    adjustedUV = uvRect.xy + adjustedUV * uvRect.zw;
    float4 c = SampleNV12(yTex, uvTex, sY, sUV, adjustedUV);
    c.rgb += brightness;
    return float4(c.rgb * color.rgb, color.a);
}
)";

// NV12 linear blend: indices 1 and 2 → t2=f1Y, t3=f1UV, t4=f2Y, t5=f2UV
const char* kDrawDecodedFrameLinearYUVFragmentHlsl = R"(
Texture2D  f1Y  : register(t2);
Texture2D  f1UV : register(t3);
Texture2D  f2Y  : register(t4);
Texture2D  f2UV : register(t5);
SamplerState s2 : register(s2);
SamplerState s3 : register(s3);
SamplerState s4 : register(s4);
SamplerState s5 : register(s5);

cbuffer QuadUniforms : register(b0) {
    float4 rect; float4 uvRect; float4 color; float brightness; float3 padding;
};
cbuffer LinearDeltaCB : register(b1) { float delta; float3 _padDelta; };

struct PSInput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };

float4 SampleNV12_2(Texture2D yT, Texture2D uvT, SamplerState sY2, SamplerState sUV2, float2 uv) {
    float  y   = yT.Sample(sY2,  uv).r;
    float2 uv2 = uvT.Sample(sUV2, uv).rg;
    float  cb  = uv2.x - 0.5; float cr = uv2.y - 0.5;
    y = 1.164383561643836 * (y - 0.0625);
    return float4(saturate(y + 1.792741071428571 * cr),
                  saturate(y - 0.532909328559444 * cr - 0.21324861427373 * cb),
                  saturate(y + 2.112401785714286 * cb), 1.0);
}

float4 main(PSInput input) : SV_TARGET {
    float2 adjustedUV = (input.uv - rect.xy) / rect.zw;
    adjustedUV = uvRect.xy + adjustedUV * uvRect.zw;
    float4 c1 = SampleNV12_2(f1Y, f1UV, s2, s3, adjustedUV);
    float4 c2 = SampleNV12_2(f2Y, f2UV, s4, s5, adjustedUV);
    float4 blended = lerp(c1, c2, delta);
    blended.rgb += brightness;
    return float4(blended.rgb * color.rgb, color.a);
}
)";

// NV12 cubic blend: indices 1-4 → t2/t3, t4/t5, t6/t7, t8/t9
const char* kDrawDecodedFrameCubicYUVFragmentHlsl = R"(
Texture2D  f1Y  : register(t2);  Texture2D f1UV : register(t3);
Texture2D  f2Y  : register(t4);  Texture2D f2UV : register(t5);
Texture2D  f3Y  : register(t6);  Texture2D f3UV : register(t7);
Texture2D  f4Y  : register(t8);  Texture2D f4UV : register(t9);
SamplerState s2 : register(s2);  SamplerState s3 : register(s3);
SamplerState s4 : register(s4);  SamplerState s5 : register(s5);
SamplerState s6 : register(s6);  SamplerState s7 : register(s7);
SamplerState s8 : register(s8);  SamplerState s9 : register(s9);

cbuffer QuadUniforms : register(b0) {
    float4 rect; float4 uvRect; float4 color; float brightness; float3 padding;
};
cbuffer CubicWeightsCB : register(b1) { float4 weights; };

struct PSInput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };

float4 SampleNV12_3(Texture2D yT, Texture2D uvT, SamplerState sY2, SamplerState sUV2, float2 uv) {
    float  y   = yT.Sample(sY2,  uv).r;
    float2 uv2 = uvT.Sample(sUV2, uv).rg;
    float  cb  = uv2.x - 0.5; float cr = uv2.y - 0.5;
    y = 1.164383561643836 * (y - 0.0625);
    return float4(saturate(y + 1.792741071428571 * cr),
                  saturate(y - 0.532909328559444 * cr - 0.21324861427373 * cb),
                  saturate(y + 2.112401785714286 * cb), 1.0);
}

float4 main(PSInput input) : SV_TARGET {
    float2 adjustedUV = (input.uv - rect.xy) / rect.zw;
    adjustedUV = uvRect.xy + adjustedUV * uvRect.zw;
    float4 c1 = SampleNV12_3(f1Y, f1UV, s2, s3, adjustedUV);
    float4 c2 = SampleNV12_3(f2Y, f2UV, s4, s5, adjustedUV);
    float4 c3 = SampleNV12_3(f3Y, f3UV, s6, s7, adjustedUV);
    float4 c4 = SampleNV12_3(f4Y, f4UV, s8, s9, adjustedUV);
    float4 blended = c1 * weights.x + c2 * weights.y + c3 * weights.z + c4 * weights.w;
    blended.rgb += brightness;
    return float4(blended.rgb * color.rgb, color.a);
}
)";

// CCubicFrameDisplay binds four decoded RGBA frames at texture indices 1-4.
const char* kDrawDecodedFrameCubicFragmentHlsl = R"(
Texture2D tx1 : register(t1);
Texture2D tx2 : register(t2);
Texture2D tx3 : register(t3);
Texture2D tx4 : register(t4);
SamplerState smp1 : register(s1);
SamplerState smp2 : register(s2);
SamplerState smp3 : register(s3);
SamplerState smp4 : register(s4);

cbuffer QuadUniforms : register(b0) {
    float4 rect;
    float4 uvRect;
    float4 color;
    float brightness;
    float3 padding;
};

cbuffer CubicWeightsCB : register(b1) {
    float4 weights;
};

struct PSInput {
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET {
    float2 adjustedUV = (input.uv - rect.xy) / rect.zw;
    adjustedUV = uvRect.xy + adjustedUV * uvRect.zw;
    float4 c1 = tx1.Sample(smp1, adjustedUV);
    float4 c2 = tx2.Sample(smp2, adjustedUV);
    float4 c3 = tx3.Sample(smp3, adjustedUV);
    float4 c4 = tx4.Sample(smp4, adjustedUV);
    float4 blended = (c1 * weights.x) + (c2 * weights.y) + (c3 * weights.z) + (c4 * weights.w);
    blended.rgb += brightness;
    return float4(blended.rgb * color.rgb, color.a);
}
)";

bool CreateDefaultSampler(ID3D11Device* device, ID3D11SamplerState** outSampler)
{
    if (!device || !outSampler)
        return false;

    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    // Clamp avoids sampling wrapped neighbors in atlases / frame edges when filtering.
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samplerDesc.MinLOD = 0.0f;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

    HRESULT hr = device->CreateSamplerState(&samplerDesc, outSampler);
    return SUCCEEDED(hr);
}

bool CreateGlyphPointSampler(ID3D11Device* device, ID3D11SamplerState** outSampler)
{
    if (!device || !outSampler)
        return false;

    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samplerDesc.MinLOD = 0.0f;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

    HRESULT hr = device->CreateSamplerState(&samplerDesc, outSampler);
    return SUCCEEDED(hr);
}

bool IsPreserveAspectEnabled()
{
    return g_Settings() && g_Settings()->Get("settings.player.preserve_AR", false);
}

bool ComputeAspectViewport16By9(float displayW, float displayH, D3D11_VIEWPORT& outViewport)
{
    if (displayW <= 0.0f || displayH <= 0.0f)
        return false;

    constexpr float kTargetAspect16By9 = 16.0f / 9.0f;
    const float displayAspect = displayW / displayH;
    float vpW = displayW;
    float vpH = displayH;

    if (kTargetAspect16By9 > displayAspect)
        vpH = std::max(1.0f, std::round(displayW / kTargetAspect16By9));
    else
        vpW = std::max(1.0f, std::round(displayH * kTargetAspect16By9));

    outViewport.TopLeftX = std::floor((displayW - vpW) * 0.5f);
    outViewport.TopLeftY = std::floor((displayH - vpH) * 0.5f);
    outViewport.Width = vpW;
    outViewport.Height = vpH;
    outViewport.MinDepth = 0.0f;
    outViewport.MaxDepth = 1.0f;
    return true;
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
        std::wstring wide;
        bool gotExtent = false;
        int nWide = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, m_text.c_str(),
                                        static_cast<int>(m_text.size()), nullptr, 0);
        if (nWide > 0)
        {
            wide.resize(static_cast<size_t>(nWide));
            if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, m_text.c_str(),
                                    static_cast<int>(m_text.size()), wide.data(), nWide) > 0)
                gotExtent = GetTextExtentPoint32W(hdc, wide.c_str(), nWide, &size);
        }
        if (!gotExtent)
        {
            nWide = MultiByteToWideChar(CP_ACP, 0, m_text.c_str(),
                                        static_cast<int>(m_text.size()), nullptr, 0);
            if (nWide > 0)
            {
                wide.resize(static_cast<size_t>(nWide));
                if (MultiByteToWideChar(CP_ACP, 0, m_text.c_str(),
                                        static_cast<int>(m_text.size()), wide.data(), nWide) > 0)
                    gotExtent = GetTextExtentPoint32W(hdc, wide.c_str(), nWide, &size);
            }
        }
        if (gotExtent)
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

CRendererDX11::CRendererDX11()
    : CRenderer(),
      m_avgGpuFrameTimeMs(-1.0f),
      m_gpuTimingSlot(0),
      m_gpuLastClosedSlot(-1),
      m_gpuTimingOpen(false),
      m_gpuQueriesOk(false)
{
}

CRendererDX11::~CRendererDX11() {
    // ComPtr handles cleanup automatically
}

bool CRendererDX11::CreateGpuTimingQueries()
{
    if (!m_device)
        return false;

    D3D11_QUERY_DESC desc = {};
    for (int i = 0; i < 2; ++i)
    {
        desc.Query = D3D11_QUERY_TIMESTAMP;
        if (FAILED(m_device->CreateQuery(&desc, &m_tsStart[i])))
            return false;
        if (FAILED(m_device->CreateQuery(&desc, &m_tsEnd[i])))
            return false;
        desc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
        if (FAILED(m_device->CreateQuery(&desc, &m_disjointQuery[i])))
            return false;
    }
    return true;
}

void CRendererDX11::ResolveGpuTimingSlot(int slot)
{
    if (!m_gpuQueriesOk || !m_context || slot < 0 || slot > 1)
        return;

    // Help prior frame's queries complete (avoids indefinite S_FALSE on some drivers).
    m_context->Flush();

    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint = {};
    HRESULT hr = m_context->GetData(m_disjointQuery[slot].Get(), &disjoint,
                                    sizeof(disjoint), 0);
    if (hr != S_OK)
        return;
    if (disjoint.Disjoint || disjoint.Frequency == 0)
        return;

    UINT64 t0 = 0;
    UINT64 t1 = 0;
    hr = m_context->GetData(m_tsStart[slot].Get(), &t0, sizeof(t0), 0);
    if (hr != S_OK)
        return;
    hr = m_context->GetData(m_tsEnd[slot].Get(), &t1, sizeof(t1), 0);
    if (hr != S_OK)
        return;
    if (t1 <= t0)
        return;

    const float ms =
        static_cast<float>((double)(t1 - t0) * 1000.0 / (double)disjoint.Frequency);
    if (m_avgGpuFrameTimeMs < 0.0f)
        m_avgGpuFrameTimeMs = ms;
    else
        m_avgGpuFrameTimeMs = m_avgGpuFrameTimeMs * 0.99f + ms * 0.01f;
}

void CRendererDX11::EndGpuTimingFrame()
{
    if (!m_gpuTimingOpen || !m_gpuQueriesOk || !m_context)
        return;

    m_context->End(m_tsEnd[m_gpuTimingSlot].Get());
    m_context->End(m_disjointQuery[m_gpuTimingSlot].Get());
    m_gpuLastClosedSlot = m_gpuTimingSlot;
    m_gpuTimingSlot = 1 - m_gpuTimingSlot;
    m_gpuTimingOpen = false;
}

float CRendererDX11::DisplayRefreshHz() const
{
    auto display = std::dynamic_pointer_cast<DisplayOutput::CDisplayDX11>(m_spDisplay);
    if (!display)
        return 60.0f;
    IDXGISwapChain* sc = display->GetSwapChain();
    if (!sc)
        return 60.0f;

    DXGI_SWAP_CHAIN_DESC scd = {};
    if (FAILED(sc->GetDesc(&scd)))
        return 60.0f;
    const UINT den = scd.BufferDesc.RefreshRate.Denominator;
    const UINT num = scd.BufferDesc.RefreshRate.Numerator;
    if (den == 0 || num == 0)
        return 60.0f;
    const float hz = static_cast<float>(num) / static_cast<float>(den);
    return hz > 0.5f ? hz : 60.0f;
}

float CRendererDX11::GetGPUFrameTimeMs()
{
    return m_avgGpuFrameTimeMs;
}

float CRendererDX11::GetGPUUtilization()
{
    if (m_avgGpuFrameTimeMs < 0.0f)
        return -1.0f;
    const float hz = DisplayRefreshHz();
    if (hz <= 0.0f)
        return -1.0f;
    const float budgetMs = 1000.0f / hz;
    return m_avgGpuFrameTimeMs / budgetMs * 100.0f;
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

    if (!CreateDepthStencilStates())
        return false;

    if (!CreateRasterizerStates())
        return false;

    m_gpuQueriesOk = CreateGpuTimingQueries();
    if (!m_gpuQueriesOk)
        g_Log->Warning("D3D11 GPU timestamp queries unavailable; HUD GPU stats disabled");

    if (!CreateDefaultSampler(m_device.Get(), &m_defaultSampler))
    {
        g_Log->Error("Failed to create default DX11 sampler");
        return false;
    }
    if (!CreateGlyphPointSampler(m_device.Get(), &m_glyphPointSampler))
    {
        g_Log->Error("Failed to create glyph point sampler");
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

bool CRendererDX11::CreateDepthStencilStates()
{
    D3D11_DEPTH_STENCIL_DESC ds = {};
    ds.DepthEnable = FALSE;
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    ds.DepthFunc = D3D11_COMPARISON_LESS;
    ds.StencilEnable = FALSE;

    HRESULT hr = m_device->CreateDepthStencilState(&ds, &m_depthStencilNoDepthTest);
    if (FAILED(hr))
    {
        g_Log->Error("Failed to create depth-stencil state (2D overlay): %08X", hr);
        return false;
    }
    return true;
}

bool CRendererDX11::CreateRasterizerStates()
{
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.FrontCounterClockwise = FALSE;
    rd.DepthBias = 0;
    rd.DepthBiasClamp = 0.0f;
    rd.SlopeScaledDepthBias = 0.0f;
    rd.DepthClipEnable = TRUE;
    rd.ScissorEnable = FALSE;
    rd.MultisampleEnable = FALSE;
    rd.AntialiasedLineEnable = FALSE;

    HRESULT hr = m_device->CreateRasterizerState(&rd, &m_rasterizerDefault);
    if (FAILED(hr))
    {
        g_Log->Error("Failed to create default rasterizer state: %08X", hr);
        return false;
    }
    rd.ScissorEnable = TRUE;
    hr = m_device->CreateRasterizerState(&rd, &m_rasterizerScissor);
    if (FAILED(hr))
    {
        g_Log->Error("Failed to create scissor rasterizer state: %08X", hr);
        return false;
    }
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
    std::lock_guard<std::recursive_mutex> d3dLock(GetD3D11ImmediateContextMutex());
    m_pendingTextDraws.clear();

    // Abort immediately if the D3D11 device was lost (DXGI_ERROR_DEVICE_REMOVED /
    // DXGI_ERROR_DEVICE_HUNG during a previous Present).  Every D3D11 call on a
    // removed device returns DXGI_ERROR_DEVICE_REMOVED, so continuing would
    // flood the log and accomplish nothing useful.
    if (m_device)
    {
        HRESULT removedReason = m_device->GetDeviceRemovedReason();
        if (removedReason != S_OK)
        {
            static bool s_deviceLostLogged = false;
            if (!s_deviceLostLogged)
            {
                g_Log->Error("D3D11 device removed (reason: %08X); all rendering suspended", removedReason);
                s_deviceLostLogged = true;
            }
            return false;
        }
    }

    if (!CRenderer::BeginFrame())
        return false;

    if (m_context && m_renderTargetView) {
        ID3D11RenderTargetView* rtv = m_renderTargetView.Get();
        m_context->OMSetRenderTargets(1, &rtv, m_depthStencilView.Get());

        float blendFactor[4] = {0.f, 0.f, 0.f, 0.f};
        m_context->OMSetBlendState(m_blendState.Get(), blendFactor, 0xFFFFFFFF);
        // All quads share z=0; default LESS depth test rejects every overlay after the first
        // fullscreen pass. Disable depth testing for 2D compositing (video + HUD + deferred text).
        m_context->OMSetDepthStencilState(m_depthStencilNoDepthTest.Get(), 0);
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

        if (m_gpuQueriesOk)
        {
            if (m_gpuLastClosedSlot >= 0)
            {
                ResolveGpuTimingSlot(m_gpuLastClosedSlot);
                m_gpuLastClosedSlot = -1;
            }
            m_context->Begin(m_disjointQuery[m_gpuTimingSlot].Get());
            m_context->End(m_tsStart[m_gpuTimingSlot].Get());
            m_gpuTimingOpen = true;
        }

        return true;
    }
    return false;
}

bool CRendererDX11::EnsureTextBatchResources()
{
    if (!m_device || !m_context)
        return false;

    if (!m_textBatchVS || !m_textBatchPS || !m_textBatchInputLayout)
    {
        ComPtr<ID3DBlob> vsBlob;
        {
            UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
            flags |= D3DCOMPILE_DEBUG;
#endif
            ComPtr<ID3DBlob> errorBlob;
            HRESULT hr = D3DCompile(kTextBatchVertexHlsl, std::strlen(kTextBatchVertexHlsl),
                                    nullptr, nullptr, nullptr, "main", "vs_5_0",
                                    flags, 0, &vsBlob, &errorBlob);
            if (FAILED(hr))
            {
                if (errorBlob)
                    g_Log->Error("Text batch VS compilation failed: %s",
                                 (char*)errorBlob->GetBufferPointer());
                return false;
            }
        }

        HRESULT hr = m_device->CreateVertexShader(vsBlob->GetBufferPointer(),
                                                  vsBlob->GetBufferSize(),
                                                  nullptr, &m_textBatchVS);
        if (FAILED(hr))
        {
            g_Log->Error("Failed to create text batch vertex shader: %08X", hr);
            return false;
        }

        D3D11_INPUT_ELEMENT_DESC layout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, (UINT)offsetof(TextBatchVertex, pos),
             D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, (UINT)offsetof(TextBatchVertex, uv),
             D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, (UINT)offsetof(TextBatchVertex, color),
             D3D11_INPUT_PER_VERTEX_DATA, 0},
        };

        hr = m_device->CreateInputLayout(layout, ARRAYSIZE(layout),
                                         vsBlob->GetBufferPointer(),
                                         vsBlob->GetBufferSize(),
                                         &m_textBatchInputLayout);
        if (FAILED(hr))
        {
            g_Log->Error("Failed to create text batch input layout: %08X", hr);
            return false;
        }

        ComPtr<ID3DBlob> psBlob;
        {
            UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
            flags |= D3DCOMPILE_DEBUG;
#endif
            ComPtr<ID3DBlob> errorBlob;
            hr = D3DCompile(kTextBatchFragmentHlsl, std::strlen(kTextBatchFragmentHlsl),
                            nullptr, nullptr, nullptr, "main", "ps_5_0",
                            flags, 0, &psBlob, &errorBlob);
            if (FAILED(hr))
            {
                if (errorBlob)
                    g_Log->Error("Text batch PS compilation failed: %s",
                                 (char*)errorBlob->GetBufferPointer());
                return false;
            }
        }

        hr = m_device->CreatePixelShader(psBlob->GetBufferPointer(),
                                         psBlob->GetBufferSize(),
                                         nullptr, &m_textBatchPS);
        if (FAILED(hr))
        {
            g_Log->Error("Failed to create text batch pixel shader: %08X", hr);
            return false;
        }
    }

    if (!m_textBatchUniformBuffer)
    {
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.ByteWidth = 16; // brightness + padding (16-byte aligned)
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        HRESULT hr = m_device->CreateBuffer(&cbDesc, nullptr, &m_textBatchUniformBuffer);
        if (FAILED(hr))
        {
            g_Log->Error("Failed to create text batch uniform buffer: %08X", hr);
            return false;
        }
    }

    return true;
}

bool CRendererDX11::EndFrame(bool drawn) {
    std::lock_guard<std::recursive_mutex> d3dLock(GetD3D11ImmediateContextMutex());
#ifdef WIN32
    // Base CRenderer::EndFrame returns false when !drawn, which would skip overlay + Present.
    // Settings overlay is rendered after HUD text so it always appears top-most.
    bool forcePresent = false;
    bool firstTimeWizardDrawn = false;
    if (m_context && m_renderTargetView && m_spDisplay) {
        const float viewportW = static_cast<float>(m_spDisplay->Width());
        const float viewportH = static_cast<float>(m_spDisplay->Height());
        const bool settingsPendingOrVisible = SettingsDialogWin32_HasPendingOrVisible();
        const bool aboutPendingOrVisible = AboutDialogWin32_HasPendingOrVisible();
        firstTimeWizardDrawn = FirstTimeSetupWin32_RenderIfNeeded(
            m_device.Get(), m_context.Get(), m_renderTargetView.Get(), viewportW, viewportH);
        forcePresent = firstTimeWizardDrawn || settingsPendingOrVisible || aboutPendingOrVisible;
    }
    const bool effectiveDrawn = drawn || forcePresent;
    if (!CRenderer::EndFrame(effectiveDrawn))
    {
        EndGpuTimingFrame();
        return false;
    }

    if (effectiveDrawn && m_context && m_spDisplay) {
        auto display = std::dynamic_pointer_cast<DisplayOutput::CDisplayDX11>(m_spDisplay);
        // Defer HUD text is drawn after the wizard; skip it so stats/indicators do not cover the dialog.
        if (display && !m_pendingTextDraws.empty() && !firstTimeWizardDrawn)
        {
            Microsoft::WRL::ComPtr<ID3D11RasterizerState> prevRasterizer;
            if (m_context && m_rasterizerScissor)
                m_context->RSGetState(prevRasterizer.GetAddressOf());

            // Render all queued HUD texts into the current backbuffer.
            for (const auto& draw : m_pendingTextDraws)
            {
                auto text = std::dynamic_pointer_cast<CTextDX11Atlas>(draw.text);
                if (!text || !text->Enabled())
                    continue;

                auto font = text->Font();
                if (!font)
                    continue;

                auto atlasTex = font->AtlasTexture();
                if (!atlasTex)
                    continue;

                // Bind atlas texture for the quad shader.
                const Base::Math::CRect r = text->GetRect();
                const uint32_t dispW = display->Width();
                const uint32_t dispH = display->Height();
                if (dispW == 0u || dispH == 0u)
                    continue;

                // Batched path: a single DrawIndexed for all glyph quads (shadow + foreground).
                DrawTextBatched(text, draw.color);

                if (m_context)
                    m_context->RSSetState(prevRasterizer.Get());
            }
        }

        // Draw settings then about so both stay above HUD; about is top-most when both run (normally exclusive).
        const float viewportW = static_cast<float>(m_spDisplay->Width());
        const float viewportH = static_cast<float>(m_spDisplay->Height());
        const bool settingsVisible = SettingsDialogWin32_RenderIfNeeded(
            m_device.Get(), m_context.Get(), m_renderTargetView.Get(), viewportW, viewportH);
        const bool aboutVisible = AboutDialogWin32_RenderIfNeeded(
            m_device.Get(), m_context.Get(), m_renderTargetView.Get(), viewportW, viewportH);

        EndGpuTimingFrame();

        if (effectiveDrawn || settingsVisible || aboutVisible)
        {
            m_spDisplay->SwapBuffers();
            return true;
        }
        return false;
    }
    EndGpuTimingFrame();
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

void CRendererDX11::DrawTextBatched(const std::shared_ptr<CTextDX11Atlas>& text,
                                    const Base::Math::CVector4& color)
{
    std::lock_guard<std::recursive_mutex> d3dLock(GetD3D11ImmediateContextMutex());
    if (!text || !text->Enabled() || !m_context || !m_spDisplay)
        return;

    auto font = text->Font();
    if (!font)
        return;

    auto atlasTex = font->AtlasTexture();
    if (!atlasTex)
        return;

    if (!EnsureTextBatchResources())
        return;

    auto atlasDx11 = std::dynamic_pointer_cast<CTextureFlatDX11>(atlasTex);
    if (!atlasDx11 || !atlasDx11->GetSRV())
        return;

    auto display = std::dynamic_pointer_cast<DisplayOutput::CDisplayDX11>(m_spDisplay);
    if (!display)
        return;

    const uint32_t dispW = display->Width();
    const uint32_t dispH = display->Height();
    if (dispW == 0u || dispH == 0u)
        return;

    const Base::Math::CRect r = text->GetRect();
    const std::string& body = text->Text();

    // Count drawable glyphs (for buffer sizing).
    uint32_t glyphCount = 0;
    {
        size_t ti = 0;
        while (ti < body.size())
        {
            if (body[ti] == '\r')
            {
                ++ti;
                continue;
            }
            if (body[ti] == '\n')
            {
                ++ti;
                continue;
            }

            uint32_t cp = 0;
            Utf8DecodeNext(body, ti, cp);
            const auto& g = font->GetGlyph(cp);
            if (g.widthPx != 0u && g.heightPx != 0u)
                ++glyphCount;
        }
    }

    if (glyphCount == 0u)
        return;

    // Two quads per glyph (shadow + foreground).
    const uint32_t quadCount = glyphCount * 2u;
    const uint32_t verticesNeeded = quadCount * 4u;
    const uint32_t indicesNeeded = quadCount * 6u;

    auto ensureBuffers = [&](uint32_t vbVerts, uint32_t ibIndices) -> bool {
        if (!m_textBatchVertexBuffer || vbVerts > m_textBatchVertexCapacity)
        {
            m_textBatchVertexCapacity = std::max(vbVerts, std::max(1024u, m_textBatchVertexCapacity * 2u));
            D3D11_BUFFER_DESC desc = {};
            desc.ByteWidth = static_cast<UINT>(m_textBatchVertexCapacity * sizeof(TextBatchVertex));
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            HRESULT hr = m_device->CreateBuffer(&desc, nullptr, &m_textBatchVertexBuffer);
            if (FAILED(hr))
            {
                g_Log->Error("Failed to create text batch VB: %08X", hr);
                return false;
            }
        }

        if (!m_textBatchIndexBuffer || ibIndices > m_textBatchIndexCapacity)
        {
            m_textBatchIndexCapacity = std::max(ibIndices, std::max(1536u, m_textBatchIndexCapacity * 2u));
            // Generate indices on CPU and upload once.
            std::vector<uint16_t> idx;
            idx.resize(m_textBatchIndexCapacity);
            uint32_t q = 0;
            uint32_t oi = 0;
            while (oi + 5u < idx.size())
            {
                const uint16_t base = static_cast<uint16_t>(q * 4u);
                idx[oi + 0] = base + 0;
                idx[oi + 1] = base + 1;
                idx[oi + 2] = base + 2;
                idx[oi + 3] = base + 0;
                idx[oi + 4] = base + 2;
                idx[oi + 5] = base + 3;
                oi += 6u;
                ++q;
                if (base + 3u >= 0xFFFCu)
                    break; // uint16 overflow safety
            }
            D3D11_BUFFER_DESC desc = {};
            desc.ByteWidth = static_cast<UINT>(idx.size() * sizeof(uint16_t));
            desc.Usage = D3D11_USAGE_IMMUTABLE;
            desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
            D3D11_SUBRESOURCE_DATA init = {};
            init.pSysMem = idx.data();
            HRESULT hr = m_device->CreateBuffer(&desc, &init, &m_textBatchIndexBuffer);
            if (FAILED(hr))
            {
                g_Log->Error("Failed to create text batch IB: %08X", hr);
                return false;
            }
        }

        return true;
    };

    if (!ensureBuffers(verticesNeeded, indicesNeeded))
        return;

    // Scissor to the text rect (same behavior as the legacy per-glyph path).
    if (m_rasterizerScissor)
    {
        const float fdw = static_cast<float>(dispW);
        const float fdh = static_cast<float>(dispH);
        D3D11_RECT sc = {};
        sc.left = static_cast<LONG>(std::floor(r.m_X0 * fdw));
        sc.top = static_cast<LONG>(std::floor(r.m_Y0 * fdh));
        sc.right = static_cast<LONG>(std::ceil(r.m_X1 * fdw)) + 1;
        sc.bottom = static_cast<LONG>(std::ceil(r.m_Y1 * fdh)) + 1;
        const LONG dw = static_cast<LONG>(dispW);
        const LONG dh = static_cast<LONG>(dispH);
        sc.left = std::max(0L, std::min(sc.left, dw));
        sc.top = std::max(0L, std::min(sc.top, dh));
        sc.right = std::max(sc.left, std::min(sc.right, dw));
        sc.bottom = std::max(sc.top, std::min(sc.bottom, dh));
        m_context->RSSetState(m_rasterizerScissor.Get());
        m_context->RSSetScissorRects(1, &sc);
    }

    // Fill vertex buffer in one map.
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT mapHr = m_context->Map(m_textBatchVertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(mapHr) || !mapped.pData)
        return;

    auto* verts = reinterpret_cast<TextBatchVertex*>(mapped.pData);
    uint32_t v = 0;

    float penXpx = r.m_X0 * static_cast<float>(dispW);
    float penYpx = r.m_Y0 * static_cast<float>(dispH);
    const float lineHeightPx = static_cast<float>(font->LineHeightPx());
    const float kTextShadowDx = 1.f;
    const float kTextShadowDy = 1.f;

    auto emitQuad = [&](const Base::Math::CRect& dest, const Base::Math::CRect& uv,
                        const Base::Math::CVector4& c)
    {
        // Order matches the existing quad vertex buffer: TL, TR, BR, BL.
        verts[v + 0] = {{dest.m_X0, dest.m_Y0, 0.f}, {uv.m_X0, uv.m_Y0}, {c.m_X, c.m_Y, c.m_Z, c.m_W}};
        verts[v + 1] = {{dest.m_X1, dest.m_Y0, 0.f}, {uv.m_X1, uv.m_Y0}, {c.m_X, c.m_Y, c.m_Z, c.m_W}};
        verts[v + 2] = {{dest.m_X1, dest.m_Y1, 0.f}, {uv.m_X1, uv.m_Y1}, {c.m_X, c.m_Y, c.m_Z, c.m_W}};
        verts[v + 3] = {{dest.m_X0, dest.m_Y1, 0.f}, {uv.m_X0, uv.m_Y1}, {c.m_X, c.m_Y, c.m_Z, c.m_W}};
        v += 4;
    };

    size_t ti = 0;
    while (ti < body.size())
    {
        if (body[ti] == '\r')
        {
            ++ti;
            continue;
        }
        if (body[ti] == '\n')
        {
            penXpx = r.m_X0 * static_cast<float>(dispW);
            penYpx += lineHeightPx;
            ++ti;
            continue;
        }

        uint32_t cp = 0;
        Utf8DecodeNext(body, ti, cp);
        const auto& g = font->GetGlyph(cp);
        if (g.widthPx == 0u || g.heightPx == 0u)
        {
            penXpx += g.advancePx;
            continue;
        }

        const float quadLeftPx = penXpx + g.penOffsetXPx;
        Base::Math::CRect dest(
            quadLeftPx / static_cast<float>(dispW),
            penYpx / static_cast<float>(dispH),
            (quadLeftPx + static_cast<float>(g.widthPx)) / static_cast<float>(dispW),
            (penYpx + static_cast<float>(g.heightPx)) / static_cast<float>(dispH));

        Base::Math::CRect shadowDest(
            (quadLeftPx + kTextShadowDx) / static_cast<float>(dispW),
            (penYpx + kTextShadowDy) / static_cast<float>(dispH),
            (quadLeftPx + kTextShadowDx + static_cast<float>(g.widthPx)) / static_cast<float>(dispW),
            (penYpx + kTextShadowDy + static_cast<float>(g.heightPx)) / static_cast<float>(dispH));

        emitQuad(shadowDest, g.uvRect, Base::Math::CVector4(0.f, 0.f, 0.f, color.m_W));
        emitQuad(dest, g.uvRect, color);

        penXpx += g.advancePx;
    }

    m_context->Unmap(m_textBatchVertexBuffer.Get(), 0);

    // Bind pipeline + issue one draw for all quads.
    ID3D11ShaderResourceView* srv = atlasDx11->GetSRV();
    m_context->PSSetShaderResources(0, 1, &srv);
    ID3D11SamplerState* samp = m_glyphPointSampler ? m_glyphPointSampler.Get() : m_defaultSampler.Get();
    m_context->PSSetSamplers(0, 1, &samp);

    m_context->IASetInputLayout(m_textBatchInputLayout.Get());
    m_context->VSSetShader(m_textBatchVS.Get(), nullptr, 0);
    m_context->PSSetShader(m_textBatchPS.Get(), nullptr, 0);

    // Brightness constant buffer.
    if (m_textBatchUniformBuffer)
    {
        D3D11_MAPPED_SUBRESOURCE cbMap = {};
        HRESULT cbHr = m_context->Map(m_textBatchUniformBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &cbMap);
        if (SUCCEEDED(cbHr) && cbMap.pData)
        {
            float* data = reinterpret_cast<float*>(cbMap.pData);
            data[0] = GetBrightness();
            data[1] = data[2] = data[3] = 0.f;
            m_context->Unmap(m_textBatchUniformBuffer.Get(), 0);
            ID3D11Buffer* cb = m_textBatchUniformBuffer.Get();
            m_context->VSSetConstantBuffers(0, 1, &cb);
            m_context->PSSetConstantBuffers(0, 1, &cb);
        }
    }

    UINT stride = sizeof(TextBatchVertex);
    UINT offset = 0;
    m_context->IASetVertexBuffers(0, 1, m_textBatchVertexBuffer.GetAddressOf(), &stride, &offset);
    m_context->IASetIndexBuffer(m_textBatchIndexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    const uint32_t quadsEmitted = v / 4u;
    const uint32_t indexCount = quadsEmitted * 6u;
    m_context->DrawIndexed(static_cast<UINT>(indexCount), 0, 0);
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

    auto font = std::make_shared<CFontDX11DirectWriteAtlas>(m_device, m_context);
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
    auto font = std::dynamic_pointer_cast<CFontDX11DirectWriteAtlas>(_font);
    if (!font)
        return nullptr;
    auto display = std::dynamic_pointer_cast<DisplayOutput::CDisplayDX11>(m_spDisplay);
    if (!display)
        return nullptr;
    return std::make_shared<CTextDX11Atlas>(
        font, _text, display->Width(), display->Height());
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

    // DX11 decoded-frame shaders: RGBA path + NV12 YUV hw path.
    // Blend uniforms use CB slots 1+ so b0 stays QuadUniforms.
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
    const char* yuvFragmentSource = nullptr;
    if (fragmentName == "drawDecodedFrameNoBlendingFragment") {
        fragmentSource    = kDrawDecodedFrameFragmentHlsl;
        yuvFragmentSource = kDrawDecodedFrameYUVFragmentHlsl;
    } else if (fragmentName == "drawDecodedFrameLinearFrameBlendFragment") {
        fragmentSource    = kDrawDecodedFrameLinearFragmentHlsl;
        yuvFragmentSource = kDrawDecodedFrameLinearYUVFragmentHlsl;
    } else if (fragmentName == "drawDecodedFrameCubicFrameBlendFragment") {
        fragmentSource    = kDrawDecodedFrameCubicFragmentHlsl;
        yuvFragmentSource = kDrawDecodedFrameCubicYUVFragmentHlsl;
    }

    auto shader = std::make_shared<CShaderDX11>(m_device, m_context);
    if (!shader->Build(kQuadPassVertexHlsl, fragmentSource)) {
        g_Log->Error("Failed building DX11 shader pair: %s / %s",
                     vertexName.c_str(), fragmentName.c_str());
        return nullptr;
    }

    const bool blendSlotsOffset =
        (fragmentName == "drawDecodedFrameLinearFrameBlendFragment" ||
         fragmentName == "drawDecodedFrameCubicFrameBlendFragment");
    for (uint32_t i = 0; i < uniforms.size(); ++i) {
        const uint32_t slot = blendSlotsOffset ? (i + 1) : i;
        shader->CreateUniform(uniforms[i].first, uniforms[i].second, slot);
    }

    const bool isDecodedFrame =
        fragmentName == "drawDecodedFrameNoBlendingFragment" ||
        fragmentName == "drawDecodedFrameLinearFrameBlendFragment" ||
        fragmentName == "drawDecodedFrameCubicFrameBlendFragment";
    shader->SetDecodedFrameShader(isDecodedFrame);

    // Build the NV12 YUV variant and register it so DrawTexturedQuad can
    // auto-select it whenever any bound texture is a hw (D3D11VA) frame.
    //
    // Intentionally do NOT call CreateUniform on the YUV variant. The YUV
    // variant is only ever bound mid-draw via the shader swap in
    // DrawTexturedQuad — immediately after the *RGBA* variant's Apply() has
    // already uploaded and bound the correct blend-uniform CB (weights/delta)
    // at slot b1. If we gave the YUV variant its own uniforms they'd be born
    // dirty (CShaderUniform ctor sets m_bDirty=true) with zero-initialized
    // data; the first Apply() on each freshly-created YUV variant would then
    // upload zeros to b1, clobbering the correct weights RGBA just wrote.
    // Cubic blending then produces (0,0,0) for one draw — the black flash at
    // seamless-transition boundaries on Windows D3D11VA (issue #568).
    // After that first draw the uniform is clean and never rebinds b1 again,
    // which is why only the first cubic draw of each new clip was broken.
    if (yuvFragmentSource)
    {
        auto yuvShader = std::make_shared<CShaderDX11>(m_device, m_context);
        if (yuvShader->Build(kQuadPassVertexHlsl, yuvFragmentSource))
        {
            yuvShader->SetDecodedFrameShader(true);
            m_yuvShaderVariants[shader.get()] = yuvShader;
        }
        else
        {
            g_Log->Warning("Failed to build NV12 YUV shader variant for '%s'; hw frames will fall back to Upload path",
                           fragmentName.c_str());
        }
    }

    return shader;
}

// TODO
void CRendererDX11::DrawText(spCBaseText _text, const Base::Math::CVector4& _color) {
    if (!_text)
        return;
    if (m_spDisplay)
    {
        if (auto atlasText = std::dynamic_pointer_cast<CTextDX11Atlas>(_text))
            atlasText->SetDisplaySize(m_spDisplay->Width(), m_spDisplay->Height());
    }
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

void CRendererDX11::DrawTexturedQuad(const Base::Math::CRect& _rect,
                                     const Base::Math::CVector4& _color,
                                     const Base::Math::CRect& _uvRect,
                                     ID3D11SamplerState* _pixelSampler)
{
    std::lock_guard<std::recursive_mutex> d3dLock(GetD3D11ImmediateContextMutex());
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

    const CShaderDX11* selectedShaderDx11 =
        dynamic_cast<CShaderDX11*>(m_spSelectedShader.get());
    const bool applyAspectViewport = selectedShaderDx11 &&
                                     selectedShaderDx11->IsDecodedFrameShader() &&
                                     IsPreserveAspectEnabled() && m_spDisplay;

    Base::Math::CRect drawRect = _rect;
    D3D11_VIEWPORT savedViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
    UINT savedViewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    bool viewportOverridden = false;

    if (applyAspectViewport)
    {
        D3D11_VIEWPORT letterboxVp = {};
        if (ComputeAspectViewport16By9(static_cast<float>(m_spDisplay->Width()),
                                       static_cast<float>(m_spDisplay->Height()),
                                       letterboxVp))
        {
            m_context->RSGetViewports(&savedViewportCount, savedViewports);
            m_context->RSSetViewports(1, &letterboxVp);
            // Draw decoded frame across the computed viewport only.
            drawRect = Base::Math::CRect(0.f, 0.f, 1.f, 1.f);
            viewportOverridden = true;
        }
    }

    auto* uniforms = reinterpret_cast<QuadUniforms*>(mapped.pData);
    uniforms->rect[0] = drawRect.m_X0;
    uniforms->rect[1] = drawRect.m_Y0;
    uniforms->rect[2] = drawRect.Width();
    uniforms->rect[3] = drawRect.Height();

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

    if (!m_spSelectedShader && m_drawTextureShader)
        SetShader(m_drawTextureShader);

    // If any bound texture is a hw YUV frame (D3D11VA NV12), automatically swap to
    // the YUV variant of the current decoded-frame shader.  This keeps FrameDisplay
    // classes oblivious to hw vs. sw decode.
    spCShader savedShaderForYUV;
    {
        bool anyYUV = false;
        for (uint32_t i = 0; i < MAX_TEXUNIT && !anyYUV; ++i)
        {
            auto* texDx11 = dynamic_cast<CTextureFlatDX11*>(m_aspSelectedTextures[i].get());
            if (texDx11 && texDx11->IsYUVTexture())
                anyYUV = true;
        }
        if (anyYUV && m_spSelectedShader)
        {
            auto it = m_yuvShaderVariants.find(m_spSelectedShader.get());
            if (it != m_yuvShaderVariants.end())
            {
                savedShaderForYUV = m_spSelectedShader;
                SetShader(it->second);
            }
        }
    }

    Apply();

    // Restore RGBA shader after the draw so state stays consistent.
    auto restoreShader = [&]() {
        if (savedShaderForYUV)
            SetShader(savedShaderForYUV);
    };

    ID3D11SamplerState* samp = _pixelSampler ? _pixelSampler : m_defaultSampler.Get();
    m_context->PSSetSamplers(0, 1, &samp);

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

    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    m_context->IASetVertexBuffers(0, 1, m_quadVertexBuffer.GetAddressOf(),
                                  &stride, &offset);
    m_context->IASetIndexBuffer(m_quadIndexBuffer.Get(), DXGI_FORMAT_R16_UINT,
                                0);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    m_context->DrawIndexed(6, 0, 0);

    if (viewportOverridden && savedViewportCount > 0)
        m_context->RSSetViewports(savedViewportCount, savedViewports);

    restoreShader();
}

void CRendererDX11::DrawQuad(const Base::Math::CRect& _rect,
                             const Base::Math::CVector4& _color,
                             const Base::Math::CRect& _uvRect)
{
    DrawTexturedQuad(_rect, _color, _uvRect, m_defaultSampler.Get());
}

void CRendererDX11::DrawQuad(const Base::Math::CRect& _rect,
                             const Base::Math::CVector4& _color)
{
    DrawQuad(_rect, _color, Base::Math::CRect(0, 0, 1, 1));
}

} // namespace DisplayOutput