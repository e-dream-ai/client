#include "GlyphAtlasDX11.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "../../Common/Log.h"
#include "../Image.h"

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
    uint32_t best = 0;
    uint32_t cur = 0;
    size_t i = 0;
    while (i < text.size())
    {
        if (text[i] == '\n')
        {
            best = std::max(best, cur);
            cur = 0;
            ++i;
            continue;
        }

        uint32_t cp = 0;
        Utf8DecodeNext(text, i, cp);
        const SGlyph& g = GetGlyph(cp);
        cur += static_cast<uint32_t>(std::round(g.advancePx));
    }

    return std::max(best, cur);
}

uint32_t CFontDX11DirectWriteAtlas::MeasureTextHeightPx(const std::string& text) const
{
    uint32_t lines = 1;
    for (char ch : text)
    {
        if (ch == '\n')
            ++lines;
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

    pushUnique(0x25CF); // HUD bullet (BLACK CIRCLE)
    pushUnique(0x0020); // space
}

bool CFontDX11DirectWriteAtlas::EnsureCreated()
{
    if (m_spAtlasTexture)
        return true;

    if (!m_device || !m_context)
        return false;

    if (!BuildAtlasGdiOnce())
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

bool CFontDX11DirectWriteAtlas::BuildAtlasGdiOnce()
{
    m_glyphByCodepoint.clear();

    LOGFONTW lf = {};
    lf.lfHeight = -static_cast<LONG>(FontDescription().Height());
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfWeight = FW_NORMAL;
    switch (FontDescription().Style())
    {
    case CFontDescription::Thin:
        lf.lfWeight = FW_THIN;
        break;
    case CFontDescription::Light:
        lf.lfWeight = FW_LIGHT;
        break;
    case CFontDescription::Normal:
        lf.lfWeight = FW_NORMAL;
        break;
    case CFontDescription::Bold:
        lf.lfWeight = FW_BOLD;
        break;
    case CFontDescription::UberBold:
        lf.lfWeight = FW_EXTRABOLD;
        break;
    }
    lf.lfItalic = FontDescription().Italic() ? TRUE : FALSE;
    lf.lfUnderline = FontDescription().Underline() ? TRUE : FALSE;
    lf.lfQuality = FontDescription().AntiAliased() ? ANTIALIASED_QUALITY : NONANTIALIASED_QUALITY;

    const std::string faceUtf8 = FontDescription().TypeFace();
    if (faceUtf8.empty())
    {
        g_Log->Error("Glyph atlas: empty typeface name");
        return false;
    }
    const int conv = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, faceUtf8.c_str(),
                                         -1, lf.lfFaceName, LF_FACESIZE);
    if (conv <= 0)
    {
        MultiByteToWideChar(CP_ACP, 0, faceUtf8.c_str(), -1, lf.lfFaceName, LF_FACESIZE);
    }

    HFONT hFont = CreateFontIndirectW(&lf);
    if (!hFont)
    {
        g_Log->Error("Glyph atlas: failed to create HFONT");
        return false;
    }

    HDC hdc = CreateCompatibleDC(nullptr);
    if (!hdc)
    {
        DeleteObject(hFont);
        return false;
    }
    HGDIOBJ oldFont = SelectObject(hdc, hFont);

    TEXTMETRICW tm = {};
    if (!GetTextMetricsW(hdc, &tm))
    {
        SelectObject(hdc, oldFont);
        DeleteObject(hFont);
        DeleteDC(hdc);
        return false;
    }

    m_ascentPx = static_cast<uint32_t>(tm.tmAscent);
    m_lineHeightPx = static_cast<uint32_t>(tm.tmHeight);
    if (m_lineHeightPx == 0)
        m_lineHeightPx = 1;

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

    // Antialiased strokes bleed past ABC boxes. Tight shelf packing lets each glyph paint
    // into the next atlas column — letters leak badly; punctuation often survives.
    constexpr uint32_t kPackPadX = 2;
    constexpr uint32_t kPackPadY = 1;
    constexpr int kCellPadR = 2; // extra texels inside the UV width for right-side AA

    auto computePlacements = [&](uint32_t tryW, uint32_t tryH, std::vector<Placement>& outPlacements) -> bool {
        outPlacements.clear();
        outPlacements.reserve(glyphCount);
        uint32_t x = 0;
        uint32_t y = 0;
        uint32_t rowH = 0;

        for (uint32_t i = 0; i < glyphCount; ++i)
        {
            const uint32_t cp = codepoints[i];
            const WCHAR wch = static_cast<WCHAR>(cp);

            ABC abc = {};
            if (!GetCharABCWidthsW(hdc, wch, wch, &abc))
            {
                SIZE size = {};
                GetTextExtentPoint32W(hdc, &wch, 1, &size);
                const int extW = std::max(1, static_cast<int>(size.cx));
                const float advancePx = static_cast<float>(extW);
                const uint32_t bw = static_cast<uint32_t>(extW + kCellPadR);
                uint32_t bh = m_lineHeightPx;

                if (x + bw > tryW)
                {
                    y += rowH + kPackPadY;
                    x = 0;
                    rowH = 0;
                }
                if (y + bh > tryH)
                    return false;

                outPlacements.push_back({cp, x, y, bw, bh, advancePx, 0});
                x += bw + kPackPadX;
                rowH = std::max(rowH, bh);
                continue;
            }

            const int a = static_cast<int>(abc.abcA);
            const int b = static_cast<int>(abc.abcB);
            const int abcC = static_cast<int>(abc.abcC);

            const int inkW = std::max(1, b);
            const int fromAbc = (a >= 0) ? (a + inkW) : inkW;
            const int cellW = std::max(1, fromAbc + kCellPadR);

            const uint32_t bw = static_cast<uint32_t>(cellW);
            const uint32_t bh = m_lineHeightPx;
            const float advancePx = static_cast<float>(a + b + abcC);

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
    for (uint32_t pow = 0; pow < 4; ++pow)
    {
        uint32_t tryW = atlasW << pow;
        uint32_t tryH = atlasH << pow;
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
        SelectObject(hdc, oldFont);
        DeleteObject(hFont);
        DeleteDC(hdc);
        return false;
    }

    // Rasterize glyphs into a DIB section.
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = static_cast<LONG>(atlasW);
    bmi.bmiHeader.biHeight = -static_cast<LONG>(atlasH); // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC hdcAtlas = CreateCompatibleDC(nullptr);
    if (!hdcAtlas)
    {
        SelectObject(hdc, oldFont);
        DeleteObject(hFont);
        DeleteDC(hdc);
        return false;
    }

    void* dibBits = nullptr;
    HBITMAP hBmp = CreateDIBSection(hdcAtlas, &bmi, DIB_RGB_COLORS, &dibBits, nullptr, 0);
    if (!hBmp || !dibBits)
    {
        DeleteDC(hdcAtlas);
        SelectObject(hdc, oldFont);
        DeleteObject(hFont);
        DeleteDC(hdc);
        return false;
    }
    HGDIOBJ oldBmp = SelectObject(hdcAtlas, hBmp);
    std::memset(dibBits, 0, static_cast<size_t>(atlasW) * atlasH * 4);

    // Use the same HFONT for drawing.
    HGDIOBJ oldFontAtlas = SelectObject(hdcAtlas, hFont);
    // TRANSPARENT keeps grayscale/ClearType fringes on the black background; OPAQUE can
    // flatten or tint edge pixels in ways that confuse our alpha extraction.
    SetBkMode(hdcAtlas, TRANSPARENT);
    SetTextColor(hdcAtlas, RGB(255, 255, 255));
    PatBlt(hdcAtlas, 0, 0, static_cast<int>(atlasW), static_cast<int>(atlasH), BLACKNESS);

    for (const auto& p : finalPlacements)
    {
        const WCHAR wch = static_cast<WCHAR>(p.codepoint);

        const int drawX = static_cast<int>(p.x) - std::min(0, p.abcA);
        const int drawY = static_cast<int>(p.y + m_ascentPx);
        TextOutW(hdcAtlas, drawX, drawY, &wch, 1);

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
        // Absolute fallback.
        m_spaceGlyph.widthPx = 1;
        m_spaceGlyph.heightPx = m_lineHeightPx;
        m_spaceGlyph.advancePx = 1.0f;
        m_spaceGlyph.penOffsetXPx = 0.0f;
        m_spaceGlyph.uvRect = Base::Math::CRect(0, 0, 1.0f / atlasW, 1.0f / atlasH);
    }

    // Convert BGRA DIB pixels into RGBA8 atlas (white RGB + alpha coverage).
    m_atlasW = atlasW;
    m_atlasH = atlasH;
    m_atlasRGBA.resize(static_cast<size_t>(atlasW) * atlasH * 4);

    const uint8_t* src = static_cast<const uint8_t*>(dibBits);
    uint64_t nonZeroAlphaPixels = 0;
    for (uint32_t y = 0; y < atlasH; ++y)
    {
        for (uint32_t x = 0; x < atlasW; ++x)
        {
            const size_t i = (static_cast<size_t>(y) * atlasW + x);
            const uint8_t b = src[i * 4 + 0];
            const uint8_t g = src[i * 4 + 1];
            const uint8_t r = src[i * 4 + 2];
            // Luminance: ClearType often lights only one channel; max(r,g,b) drops most strokes.
            const unsigned lum =
                (static_cast<unsigned>(r) * 76u + static_cast<unsigned>(g) * 150u +
                 static_cast<unsigned>(b) * 29u) >>
                8u;
            const uint8_t intensity = static_cast<uint8_t>(std::min(255u, lum));

            // RGB is white, alpha comes from glyph coverage.
            m_atlasRGBA[i * 4 + 0] = 255; // R
            m_atlasRGBA[i * 4 + 1] = 255; // G
            m_atlasRGBA[i * 4 + 2] = 255; // B
            m_atlasRGBA[i * 4 + 3] = intensity;
            if (intensity != 0)
                ++nonZeroAlphaPixels;
        }
    }

    if (nonZeroAlphaPixels == 0)
    {
        g_Log->Warning("Glyph atlas rasterized with zero coverage pixels");
    }

    // Cleanup.
    SelectObject(hdcAtlas, oldFontAtlas);
    SelectObject(hdcAtlas, oldBmp);
    DeleteObject(hBmp);
    DeleteDC(hdcAtlas);

    SelectObject(hdc, oldFont);
    DeleteObject(hFont);
    DeleteDC(hdc);

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

