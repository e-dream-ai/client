#ifndef WIN32

#include "FontVulkan.h"
#include "RendererVulkan.h"
#include "TextureFlatVulkan.h"
#include "Image.h"
#include "Log.h"
#include "PlatformUtils.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SYNTHESIS_H

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

namespace DisplayOutput
{

// Printable ASCII range rendered into the atlas.
static constexpr uint32_t GLYPH_FIRST = 32;
static constexpr uint32_t GLYPH_LAST  = 126;
// Pixel gap between glyphs in the atlas.
static constexpr int ATLAS_PAD = 2;

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------
CFontVulkan::~CFontVulkan()
{
    if (m_ftFace) { FT_Done_Face(m_ftFace);    m_ftFace = nullptr; }
    if (m_ftLib)  { FT_Done_FreeType(m_ftLib); m_ftLib  = nullptr; }
}

// ---------------------------------------------------------------------------
// Font file resolution
// ---------------------------------------------------------------------------
bool CFontVulkan::findFontFile(const std::string& typeface,
                               std::string&       outPath) const
{
    // 1. Look for a bundled font next to the binary / in the working dir.
    std::string workDir = PlatformUtils::GetWorkingDir();
    const std::vector<std::string> candidates = {
        workDir + typeface + ".ttf",
        workDir + typeface + "-Regular.ttf",
        workDir + typeface + ".otf",
    };
    for (const auto& p : candidates)
    {
        if (std::filesystem::exists(p))
        {
            outPath = p;
            return true;
        }
    }

    // 2. Ask fontconfig (fc-match) if installed.
    std::string cmd = "fc-match --format='%{file}' '" + typeface + "' 2>/dev/null";
    FILE* fp = popen(cmd.c_str(), "r");
    if (fp)
    {
        char buf[1024] = {};
        if (fgets(buf, sizeof(buf), fp))
        {
            size_t len = strlen(buf);
            while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'
                               || buf[len - 1] == ' '))
                buf[--len] = '\0';
            if (len > 0 && std::filesystem::exists(buf))
            {
                outPath = buf;
                pclose(fp);
                return true;
            }
        }
        pclose(fp);
    }

    // 3. Common system font fallbacks.
    static const char* const kSystemFonts[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
        nullptr,
    };
    for (int i = 0; kSystemFonts[i]; ++i)
    {
        if (std::filesystem::exists(kSystemFonts[i]))
        {
            outPath = kSystemFonts[i];
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// CreateWithRenderer — load font face and build the GPU atlas.
// ---------------------------------------------------------------------------
bool CFontVulkan::CreateWithRenderer(CRendererVulkan* renderer)
{
    const std::string& typeface  = m_FontDescription.TypeFace();
    int                pxHeight  = static_cast<int>(m_FontDescription.Height());

    std::string fontPath;
    if (!findFontFile(typeface, fontPath))
    {
        g_Log->Warning("CFontVulkan: font '%s' not found, text will not render",
                       typeface.c_str());
        return false;
    }

    if (FT_Init_FreeType(&m_ftLib) != 0)
    {
        g_Log->Error("CFontVulkan: FT_Init_FreeType failed");
        return false;
    }

    if (FT_New_Face(m_ftLib, fontPath.c_str(), 0, &m_ftFace) != 0)
    {
        g_Log->Warning("CFontVulkan: FT_New_Face failed for '%s'", fontPath.c_str());
        return false;
    }

    FT_Set_Pixel_Sizes(m_ftFace, 0, static_cast<FT_UInt>(pxHeight));

    m_lineHeight = static_cast<int32_t>(m_ftFace->size->metrics.height   >> 6);
    m_ascender   = static_cast<int32_t>(m_ftFace->size->metrics.ascender >> 6);

    return buildAtlas(renderer);
}

// ---------------------------------------------------------------------------
// buildAtlas — render glyphs into an RGBA8 atlas and upload to the GPU.
//
// Atlas pixel format: R=G=B=255, A=coverage.
// The existing shader computes  texel * fragColor  which yields
// {r, g, b, coverage * a} — correct for alpha-blended coloured text.
// ---------------------------------------------------------------------------
bool CFontVulkan::buildAtlas(CRendererVulkan* renderer)
{
    struct RawGlyph
    {
        uint32_t              codepoint;
        std::vector<uint8_t>  pixels;   // FT_PIXEL_MODE_GRAY bitmap
        int32_t               width, height;
        int32_t               bearingX, bearingY;
        int32_t               advance;
    };

    std::vector<RawGlyph> rawGlyphs;
    rawGlyphs.reserve(GLYPH_LAST - GLYPH_FIRST + 1);

    const bool isBold = (m_FontDescription.Style() >= CFontDescription::Bold);

    int totalW = ATLAS_PAD;
    int maxH   = 0;

    for (uint32_t cp = GLYPH_FIRST; cp <= GLYPH_LAST; ++cp)
    {
        if (isBold)
        {
            if (FT_Load_Char(m_ftFace, cp, FT_LOAD_DEFAULT) != 0) continue;
            FT_GlyphSlot_Embolden(m_ftFace->glyph);
            if (FT_Render_Glyph(m_ftFace->glyph, FT_RENDER_MODE_NORMAL) != 0) continue;
        }
        else
        {
            if (FT_Load_Char(m_ftFace, cp,
                             FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL) != 0)
                continue;
        }

        FT_GlyphSlot gs = m_ftFace->glyph;
        FT_Bitmap&   bm = gs->bitmap;

        RawGlyph rg{};
        rg.codepoint = cp;
        rg.width     = static_cast<int32_t>(bm.width);
        rg.height    = static_cast<int32_t>(bm.rows);
        rg.bearingX  = static_cast<int32_t>(gs->bitmap_left);
        rg.bearingY  = static_cast<int32_t>(gs->bitmap_top);
        rg.advance   = static_cast<int32_t>(gs->advance.x >> 6);
        rg.pixels.assign(bm.buffer, bm.buffer + bm.width * bm.rows);
        rawGlyphs.push_back(std::move(rg));

        totalW += static_cast<int>(bm.width) + ATLAS_PAD;
        maxH    = std::max(maxH, static_cast<int>(bm.rows));
    }

    // Round atlas dimensions up to the next power of two.
    uint32_t atlasW = 1;
    while (atlasW < static_cast<uint32_t>(totalW))
        atlasW <<= 1;

    uint32_t atlasH = 1;
    while (atlasH < static_cast<uint32_t>(maxH + ATLAS_PAD * 2))
        atlasH <<= 1;

    // Blit each glyph into the RGBA8 atlas buffer.
    std::vector<uint8_t> atlasPixels(atlasW * atlasH * 4, 0);

    int penX = ATLAS_PAD;
    for (const auto& rg : rawGlyphs)
    {
        const int y0 = ATLAS_PAD;
        for (int row = 0; row < rg.height; ++row)
        {
            for (int col = 0; col < rg.width; ++col)
            {
                const uint8_t coverage = rg.pixels[row * rg.width + col];
                const int     dstX     = penX + col;
                const int     dstY     = y0   + row;
                const int     idx      = (dstY * static_cast<int>(atlasW) + dstX) * 4;
                atlasPixels[idx + 0]   = 255;
                atlasPixels[idx + 1]   = 255;
                atlasPixels[idx + 2]   = 255;
                atlasPixels[idx + 3]   = coverage;
            }
        }

        GlyphInfo gi{};
        gi.u0       = static_cast<float>(penX)           / static_cast<float>(atlasW);
        gi.v0       = static_cast<float>(y0)             / static_cast<float>(atlasH);
        gi.u1       = static_cast<float>(penX + rg.width) / static_cast<float>(atlasW);
        gi.v1       = static_cast<float>(y0 + rg.height)  / static_cast<float>(atlasH);
        gi.bearingX = rg.bearingX;
        gi.bearingY = rg.bearingY;
        gi.width    = rg.width;
        gi.height   = rg.height;
        gi.advance  = rg.advance;
        m_glyphs[rg.codepoint] = gi;

        penX += rg.width + ATLAS_PAD;
    }

    auto spImg = std::make_shared<CImage>();
    spImg->Create(atlasW, atlasH, eImage_RGBA8);
    if (uint8_t* dst = spImg->GetData(0))
        memcpy(dst, atlasPixels.data(), atlasPixels.size());

    m_atlas = std::make_shared<CTextureFlatVulkan>(renderer, 0);
    if (!m_atlas->Upload(spImg))
    {
        g_Log->Error("CFontVulkan: atlas texture upload failed");
        m_atlas.reset();
        return false;
    }

    g_Log->Info("CFontVulkan: built %ux%u atlas for '%s' at %upx",
                atlasW, atlasH,
                m_FontDescription.TypeFace().c_str(),
                m_FontDescription.Height());
    return true;
}

// ---------------------------------------------------------------------------
// GetGlyph
// ---------------------------------------------------------------------------
const GlyphInfo* CFontVulkan::GetGlyph(uint32_t codepoint) const
{
    auto it = m_glyphs.find(codepoint);
    return (it != m_glyphs.end()) ? &it->second : nullptr;
}

// ---------------------------------------------------------------------------
// CTextVulkan
// ---------------------------------------------------------------------------
CTextVulkan::CTextVulkan(spCBaseFont font, const std::string& text,
                         CRendererVulkan* renderer)
    : m_font(std::move(font)), m_text(text), m_renderer(renderer)
{}

Base::Math::CVector2 CTextVulkan::GetExtent()
{
    if (!m_font || !m_renderer)
        return {0.f, 0.f};

    auto spFV = std::dynamic_pointer_cast<CFontVulkan>(m_font);
    if (!spFV || !spFV->AtlasTexture())
        return {0.f, 0.f};

    VkExtent2D ext    = m_renderer->GetSwapExtent();
    float      screenW = static_cast<float>(ext.width);
    float      screenH = static_cast<float>(ext.height);
    if (screenW <= 0.f || screenH <= 0.f)
        return {0.f, 0.f};

    float maxLineW = 0.f;
    float lineW    = 0.f;
    int   lines    = 1;

    for (unsigned char c : m_text)
    {
        if (c == '\n')
        {
            maxLineW = std::max(maxLineW, lineW);
            lineW    = 0.f;
            ++lines;
            continue;
        }
        if (c == '\t')
        {
            const GlyphInfo* sp = spFV->GetGlyph(' ');
            if (sp)
            {
                float tabW  = static_cast<float>(sp->advance * 8) / screenW;
                float stops = std::floor(lineW / tabW);
                lineW = (stops + 1.0f) * tabW;
            }
            continue;
        }
        const GlyphInfo* gi = spFV->GetGlyph(static_cast<uint32_t>(c));
        if (gi)
            lineW += static_cast<float>(gi->advance) / screenW;
    }
    maxLineW = std::max(maxLineW, lineW);

    float totalH = static_cast<float>(spFV->LineHeight() * lines) / screenH;
    return {maxLineW, totalH};
}

} // namespace DisplayOutput

#endif // !WIN32
