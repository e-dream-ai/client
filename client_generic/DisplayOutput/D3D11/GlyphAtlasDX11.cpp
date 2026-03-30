#include "GlyphAtlasDX11.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

// windows.h (via header above) #defines GetGlyphIndices -> GetGlyphIndicesW. If dwrite.h is
// parsed with that macro active, IDWriteFontFace declares the wrong member name and calls
// won't compile — undef before including dwrite.h.
#pragma push_macro("GetGlyphIndices")
#undef GetGlyphIndices

#include <dwrite.h>

#include "../../Common/Log.h"
#include "../Image.h"

#ifdef GetGlyphIndices
#undef GetGlyphIndices
#endif

namespace DisplayOutput
{

using Microsoft::WRL::ComPtr;

CFontDX11DirectWriteAtlas::CFontDX11DirectWriteAtlas(ComPtr<ID3D11Device> device,
                                                     ComPtr<ID3D11DeviceContext> context)
    : m_device(std::move(device)), m_context(std::move(context))
{
}

CFontDX11DirectWriteAtlas::~CFontDX11DirectWriteAtlas() = default;

bool CFontDX11DirectWriteAtlas::Create() { return EnsureCreated(); }

const CFontDX11DirectWriteAtlas::SGlyph& CFontDX11DirectWriteAtlas::GetGlyph(uint32_t codePoint) const
{
    const auto it = m_glyphByCodepoint.find(codePoint);
    if (it != m_glyphByCodepoint.end())
        return it->second;
    return m_spaceGlyph;
}

uint32_t CFontDX11DirectWriteAtlas::MeasureTextWidthPx(const std::string& text) const
{
    float best = 0.0f;
    float cur = 0.0f;
    size_t i = 0;
    while (i < text.size())
    {
        if (text[i] == '\r')
        {
            ++i;
            continue;
        }
        if (text[i] == '\n')
        {
            best = std::max(best, cur);
            cur = 0.0f;
            ++i;
            continue;
        }

        uint32_t cp = 0;
        Utf8DecodeNext(text, i, cp);
        const SGlyph& g = GetGlyph(cp);
        cur += g.advancePx;
    }

    return static_cast<uint32_t>(std::ceil(std::max(best, cur)));
}

uint32_t CFontDX11DirectWriteAtlas::MeasureTextHeightPx(const std::string& text) const
{
    uint32_t lines = 1;
    for (size_t i = 0; i < text.size();)
    {
        if (text[i] == '\r')
        {
            ++i;
            continue;
        }
        if (text[i] == '\n')
        {
            ++lines;
            ++i;
            continue;
        }
        uint32_t cp = 0;
        Utf8DecodeNext(text, i, cp);
        (void)cp;
    }

    return lines * m_lineHeightPx;
}

static void BuildGlyphCodepointList(std::vector<uint32_t>& out)
{
    out.clear();
    auto pushUnique = [&](uint32_t cp) {
        for (uint32_t x : out)
        {
            if (x == cp)
                return;
        }
        out.push_back(cp);
    };

    for (uint32_t cp = 32; cp <= 126; ++cp)
        pushUnique(cp);
    for (uint32_t cp = 0xA0; cp <= 0x00FF; ++cp)
        pushUnique(cp);
    for (uint32_t cp = 0x100; cp <= 0x017F; ++cp)
        pushUnique(cp);
    // Latin Extended-B (common Eastern European letters)
    for (uint32_t cp = 0x0180; cp <= 0x024F; ++cp)
        pushUnique(cp);
    // Greek + Coptic
    for (uint32_t cp = 0x0370; cp <= 0x03FF; ++cp)
        pushUnique(cp);
    // Cyrillic + Cyrillic Supplement
    for (uint32_t cp = 0x0400; cp <= 0x052F; ++cp)
        pushUnique(cp);
    // General punctuation (dashes, bullets)
    for (uint32_t cp = 0x2000; cp <= 0x206F; ++cp)
        pushUnique(cp);

    pushUnique(0x20AC); // Euro (outside General Punctuation block below)

    pushUnique(0x25CF); // HUD bullet (BLACK CIRCLE)
    pushUnique(0x0020); // space
}

bool CFontDX11DirectWriteAtlas::EnsureCreated()
{
    if (m_spAtlasTexture)
        return true;

    if (!m_device || !m_context)
        return false;

    if (!BuildAtlasDirectWriteOnce())
        return false;

    // Upload CPU atlas as a GPU texture.
    const uint32_t bytesPerPixel = 4;
    if (m_atlasRGBA.empty() || m_atlasRGBA.size() != static_cast<size_t>(m_atlasW) * m_atlasH * bytesPerPixel)
    {
        g_Log->Error("Glyph atlas: invalid RGBA buffer");
        return false;
    }

    auto atlasImg = std::make_shared<CImage>();
    atlasImg->Create(m_atlasW, m_atlasH, eImage_RGBA8, false, false);
    auto& storage = atlasImg->GetStorageBuffer();
    if (!storage || !storage->IsValid())
    {
        g_Log->Error("Glyph atlas: failed to allocate CImage storage");
        return false;
    }
    std::memcpy(storage->GetBufferPtr(), m_atlasRGBA.data(), m_atlasRGBA.size());

    auto tex = std::make_shared<CTextureFlatDX11>(m_device, m_context, 0);
    if (!tex->Upload(atlasImg))
    {
        g_Log->Error("Glyph atlas: texture upload failed");
        return false;
    }
    m_spAtlasTexture = tex;
    return true;
}

void CFontDX11DirectWriteAtlas::ConvertAtlasToRGBA(std::vector<uint8_t>& outRGBA) const
{
    outRGBA = m_atlasRGBA;
}

bool CFontDX11DirectWriteAtlas::BuildAtlasDirectWriteOnce()
{
    m_glyphByCodepoint.clear();

    ComPtr<IDWriteFactory> factory;
    HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                     reinterpret_cast<IUnknown**>(factory.GetAddressOf()));
    if (FAILED(hr) || !factory)
    {
        g_Log->Error("Glyph atlas: DWriteCreateFactory failed %08X", hr);
        return false;
    }

    const std::string faceUtf8 = FontDescription().TypeFace();
    if (faceUtf8.empty())
    {
        g_Log->Error("Glyph atlas: empty typeface name");
        return false;
    }

    std::wstring familyName;
    {
        int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, faceUtf8.data(),
                                    static_cast<int>(faceUtf8.size()), nullptr, 0);
        if (n > 0)
        {
            familyName.resize(static_cast<size_t>(n));
            if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, faceUtf8.data(),
                                    static_cast<int>(faceUtf8.size()), familyName.data(), n) <= 0)
                n = 0;
        }
        if (n <= 0)
        {
            n = MultiByteToWideChar(CP_ACP, 0, faceUtf8.data(), static_cast<int>(faceUtf8.size()),
                                    nullptr, 0);
            if (n > 0)
            {
                familyName.resize(static_cast<size_t>(n));
                MultiByteToWideChar(CP_ACP, 0, faceUtf8.data(), static_cast<int>(faceUtf8.size()),
                                    familyName.data(), n);
            }
        }
    }
    if (familyName.empty())
    {
        g_Log->Error("Glyph atlas: could not convert typeface name to wide string");
        return false;
    }

    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
    switch (FontDescription().Style())
    {
    case CFontDescription::Thin:
        weight = DWRITE_FONT_WEIGHT_THIN;
        break;
    case CFontDescription::Light:
        weight = DWRITE_FONT_WEIGHT_LIGHT;
        break;
    case CFontDescription::Normal:
        weight = DWRITE_FONT_WEIGHT_NORMAL;
        break;
    case CFontDescription::Bold:
        weight = DWRITE_FONT_WEIGHT_BOLD;
        break;
    case CFontDescription::UberBold:
        weight = DWRITE_FONT_WEIGHT_ULTRA_BOLD;
        break;
    }

    const DWRITE_FONT_STYLE style =
        FontDescription().Italic() ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL;
    const DWRITE_FONT_STRETCH stretch = DWRITE_FONT_STRETCH_NORMAL;

    ComPtr<IDWriteFontCollection> collection;
    hr = factory->GetSystemFontCollection(&collection);
    if (FAILED(hr) || !collection)
    {
        g_Log->Error("Glyph atlas: GetSystemFontCollection failed %08X", hr);
        return false;
    }

    UINT32 familyIndex = 0;
    BOOL familyExists = FALSE;
    hr = collection->FindFamilyName(familyName.c_str(), &familyIndex, &familyExists);
    if (FAILED(hr) || !familyExists)
    {
        g_Log->Error("Glyph atlas: DirectWrite could not find font family");
        return false;
    }

    ComPtr<IDWriteFontFamily> fontFamily;
    hr = collection->GetFontFamily(familyIndex, &fontFamily);
    if (FAILED(hr) || !fontFamily)
    {
        g_Log->Error("Glyph atlas: GetFontFamily failed %08X", hr);
        return false;
    }

    ComPtr<IDWriteFont> font;
    hr = fontFamily->GetFirstMatchingFont(weight, stretch, style, &font);
    if (FAILED(hr) || !font)
    {
        g_Log->Error("Glyph atlas: GetFirstMatchingFont failed %08X", hr);
        return false;
    }

    ComPtr<IDWriteFontFace> fontFace;
    hr = font->CreateFontFace(&fontFace);
    if (FAILED(hr) || !fontFace)
    {
        g_Log->Error("Glyph atlas: CreateFontFace failed %08X", hr);
        return false;
    }

    DWRITE_FONT_METRICS fontMetrics = {};
    fontFace->GetMetrics(&fontMetrics);

    const float emSize = static_cast<float>(FontDescription().Height());
    const float designToPx = emSize / static_cast<float>(fontMetrics.designUnitsPerEm);

    const float ascentF = static_cast<float>(fontMetrics.ascent) * designToPx;
    const float descentF = static_cast<float>(fontMetrics.descent) * designToPx;
    const float lineGapF = static_cast<float>(fontMetrics.lineGap) * designToPx;

    m_ascentPx = static_cast<uint32_t>(std::max(1.0f, static_cast<float>(std::round(ascentF))));
    const uint32_t lineH = static_cast<uint32_t>(
        std::max(1.0f, static_cast<float>(std::round(ascentF + descentF + lineGapF))));
    m_lineHeightPx = lineH > 0 ? lineH : 1u;

    std::vector<uint32_t> codepoints;
    BuildGlyphCodepointList(codepoints);
    const uint32_t glyphCount = static_cast<uint32_t>(codepoints.size());
    if (glyphCount == 0)
        return false;

    struct Placement
    {
        uint32_t codepoint = 0;
        uint32_t x = 0;
        uint32_t y = 0;
        uint32_t w = 0;
        uint32_t h = 0;
        float advancePx = 0.0f;
        int abcA = 0;
    };

    uint32_t atlasW = 1024;
    uint32_t atlasH = 1024;

    constexpr uint32_t kPackPadX = 2;
    constexpr uint32_t kPackPadY = 1;
    constexpr int kCellPadR = 2;

    auto computePlacements = [&](uint32_t tryW, uint32_t tryH, std::vector<Placement>& outPlacements) -> bool {
        outPlacements.clear();
        outPlacements.reserve(glyphCount);
        uint32_t x = 0;
        uint32_t y = 0;
        uint32_t rowH = 0;

        for (uint32_t gi = 0; gi < glyphCount; ++gi)
        {
            const uint32_t cp = codepoints[gi];
            UINT32 cp32 = cp;
            UINT16 glyphIndex = 0;
            if (FAILED(fontFace->GetGlyphIndices(&cp32, 1, &glyphIndex)))
                return false;

            DWRITE_GLYPH_METRICS gm = {};
            if (FAILED(fontFace->GetDesignGlyphMetrics(&glyphIndex, 1, &gm, FALSE)))
                return false;

            const float advancePx = static_cast<float>(gm.advanceWidth) * designToPx;
            const int a = static_cast<int>(std::floor(static_cast<float>(gm.leftSideBearing) * designToPx));
            const int inkWpx = std::max(
                1, static_cast<int>(std::ceil(
                       static_cast<float>(gm.advanceWidth - gm.leftSideBearing - gm.rightSideBearing) *
                       designToPx)));
            const int fromAbc = (a >= 0) ? (a + inkWpx) : inkWpx;
            const int cellW = std::max(1, fromAbc + kCellPadR);

            const uint32_t bw = static_cast<uint32_t>(cellW);
            const uint32_t bh = m_lineHeightPx;

            if (x + bw > tryW)
            {
                y += rowH + kPackPadY;
                x = 0;
                rowH = 0;
            }
            if (y + bh > tryH)
                return false;

            outPlacements.push_back({cp, x, y, bw, bh, advancePx, a});
            x += bw + kPackPadX;
            rowH = std::max(rowH, bh);
        }

        return true;
    };

    bool fits = false;
    std::vector<Placement> finalPlacements;
    for (uint32_t pow = 0; pow < 6; ++pow)
    {
        const uint32_t tryW = atlasW << pow;
        const uint32_t tryH = atlasH << pow;
        std::vector<Placement> tmp;
        if (computePlacements(tryW, tryH, tmp))
        {
            atlasW = tryW;
            atlasH = tryH;
            finalPlacements = std::move(tmp);
            fits = true;
            break;
        }
    }
    if (!fits)
    {
        g_Log->Error("Glyph atlas: failed to pack glyph set");
        return false;
    }

    ComPtr<IDWriteGdiInterop> gdiInterop;
    hr = factory->GetGdiInterop(&gdiInterop);
    if (FAILED(hr) || !gdiInterop)
    {
        g_Log->Error("Glyph atlas: GetGdiInterop failed %08X", hr);
        return false;
    }

    ComPtr<IDWriteRenderingParams> renderingParams;
    hr = factory->CreateRenderingParams(&renderingParams);
    if (FAILED(hr))
    {
        g_Log->Error("Glyph atlas: CreateRenderingParams failed %08X", hr);
        return false;
    }

    HDC hdcScreen = GetDC(nullptr);
    ComPtr<IDWriteBitmapRenderTarget> bitmapRt;
    hr = gdiInterop->CreateBitmapRenderTarget(hdcScreen, static_cast<UINT32>(atlasW),
                                              static_cast<UINT32>(atlasH), &bitmapRt);
    ReleaseDC(nullptr, hdcScreen);
    if (FAILED(hr) || !bitmapRt)
    {
        g_Log->Error("Glyph atlas: CreateBitmapRenderTarget failed %08X", hr);
        return false;
    }

    bitmapRt->SetPixelsPerDip(1.0f);

    HDC hdcBrt = bitmapRt->GetMemoryDC();
    PatBlt(hdcBrt, 0, 0, static_cast<int>(atlasW), static_cast<int>(atlasH), BLACKNESS);

    const DWRITE_MEASURING_MODE measureMode = FontDescription().AntiAliased()
                                                   ? DWRITE_MEASURING_MODE_GDI_NATURAL
                                                   : DWRITE_MEASURING_MODE_GDI_CLASSIC;
    const COLORREF textColor = RGB(255, 255, 255);

    for (const auto& p : finalPlacements)
    {
        UINT32 cp32 = p.codepoint;
        UINT16 glyphIndex = 0;
        hr = fontFace->GetGlyphIndices(&cp32, 1, &glyphIndex);
        if (FAILED(hr))
        {
            g_Log->Warning("Glyph atlas: GetGlyphIndices failed for U+%04X: %08X", p.codepoint, hr);
            continue;
        }

        const float advanceDip = p.advancePx;
        DWRITE_GLYPH_OFFSET glyphOffset = {};
        DWRITE_GLYPH_RUN run = {};
        run.fontFace = fontFace.Get();
        run.fontEmSize = emSize;
        run.glyphCount = 1;
        run.glyphIndices = &glyphIndex;
        run.glyphAdvances = &advanceDip;
        run.glyphOffsets = &glyphOffset;
        run.isSideways = FALSE;
        run.bidiLevel = 0;

        const float baselineX = static_cast<float>(p.x) - static_cast<float>(std::min(0, p.abcA));
        const float baselineY = static_cast<float>(p.y) + static_cast<float>(m_ascentPx);

        hr = bitmapRt->DrawGlyphRun(baselineX, baselineY, measureMode, &run, renderingParams.Get(),
                                    textColor, nullptr);
        if (FAILED(hr))
        {
            g_Log->Warning("Glyph atlas: DrawGlyphRun failed for U+%04X: %08X", p.codepoint, hr);
        }

        const float u0 = static_cast<float>(p.x) / static_cast<float>(atlasW);
        const float v0 = static_cast<float>(p.y) / static_cast<float>(atlasH);
        const float u1 = static_cast<float>(p.x + p.w) / static_cast<float>(atlasW);
        const float v1 = static_cast<float>(p.y + p.h) / static_cast<float>(atlasH);

        SGlyph g;
        g.widthPx = p.w;
        g.heightPx = p.h;
        g.advancePx = p.advancePx;
        g.penOffsetXPx = static_cast<float>(std::min(0, p.abcA));
        g.uvRect = Base::Math::CRect(u0, v0, u1, v1);
        m_glyphByCodepoint[p.codepoint] = g;
    }

    const auto spaceIt = m_glyphByCodepoint.find(0x20u);
    if (spaceIt != m_glyphByCodepoint.end())
        m_spaceGlyph = spaceIt->second;
    else if (!m_glyphByCodepoint.empty())
        m_spaceGlyph = m_glyphByCodepoint.begin()->second;
    if (m_spaceGlyph.widthPx == 0 || m_spaceGlyph.heightPx == 0)
    {
        m_spaceGlyph.widthPx = 1;
        m_spaceGlyph.heightPx = m_lineHeightPx;
        m_spaceGlyph.advancePx = 1.0f;
        m_spaceGlyph.penOffsetXPx = 0.0f;
        m_spaceGlyph.uvRect = Base::Math::CRect(0, 0, 1.0f / atlasW, 1.0f / atlasH);
    }

    HBITMAP hBmp = static_cast<HBITMAP>(GetCurrentObject(hdcBrt, OBJ_BITMAP));
    if (!hBmp)
    {
        g_Log->Error("Glyph atlas: no bitmap on DirectWrite render target DC");
        return false;
    }

    BITMAP bm = {};
    if (!GetObjectW(hBmp, sizeof(bm), &bm))
    {
        g_Log->Error("Glyph atlas: GetObject bitmap failed");
        return false;
    }

    m_atlasW = atlasW;
    m_atlasH = atlasH;
    m_atlasRGBA.resize(static_cast<size_t>(atlasW) * atlasH * 4);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = static_cast<LONG>(atlasW);
    bmi.bmiHeader.biHeight = -static_cast<LONG>(atlasH);
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    std::vector<uint8_t> bgra(static_cast<size_t>(atlasW) * atlasH * 4);
    if (!GetDIBits(hdcBrt, hBmp, 0, atlasH, bgra.data(), &bmi, DIB_RGB_COLORS))
    {
        g_Log->Error("Glyph atlas: GetDIBits failed");
        return false;
    }

    uint64_t nonZeroAlphaPixels = 0;
    for (uint32_t row = 0; row < atlasH; ++row)
    {
        for (uint32_t col = 0; col < atlasW; ++col)
        {
            const size_t i = static_cast<size_t>(row) * atlasW + col;
            const uint8_t b = bgra[i * 4 + 0];
            const uint8_t gch = bgra[i * 4 + 1];
            const uint8_t r = bgra[i * 4 + 2];
            const unsigned lum = (static_cast<unsigned>(r) * 76u +
                                  static_cast<unsigned>(gch) * 150u +
                                  static_cast<unsigned>(b) * 29u) >>
                                 8u;
            const uint8_t intensity = static_cast<uint8_t>(std::min(255u, lum));

            m_atlasRGBA[i * 4 + 0] = 255;
            m_atlasRGBA[i * 4 + 1] = 255;
            m_atlasRGBA[i * 4 + 2] = 255;
            m_atlasRGBA[i * 4 + 3] = intensity;
            if (intensity != 0)
                ++nonZeroAlphaPixels;
        }
    }

    if (nonZeroAlphaPixels == 0)
        g_Log->Warning("Glyph atlas rasterized with zero coverage pixels");

    return true;
}

CTextDX11Atlas::CTextDX11Atlas(std::shared_ptr<CFontDX11DirectWriteAtlas> font,
                                 std::string text,
                                 uint32_t displayWidthPx,
                                 uint32_t displayHeightPx)
    : m_font(std::move(font)),
      m_text(std::move(text)),
      m_displayW(displayWidthPx ? displayWidthPx : 1),
      m_displayH(displayHeightPx ? displayHeightPx : 1)
{
}

void CTextDX11Atlas::SetDisplaySize(uint32_t widthPx, uint32_t heightPx)
{
    m_displayW = widthPx ? widthPx : 1;
    m_displayH = heightPx ? heightPx : 1;
}

Base::Math::CVector2 CTextDX11Atlas::GetExtent()
{
    if (!m_font)
        return {};

    uint32_t wPx = m_font->MeasureTextWidthPx(m_text);
    uint32_t hPx = m_font->MeasureTextHeightPx(m_text);

    return Base::Math::CVector2(static_cast<float>(wPx) / static_cast<float>(m_displayW),
                                 static_cast<float>(hPx) / static_cast<float>(m_displayH));
}

} // namespace DisplayOutput

#pragma pop_macro("GetGlyphIndices")
