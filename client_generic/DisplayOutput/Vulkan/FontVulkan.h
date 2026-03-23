#ifndef _FONTVULKAN_H_
#define _FONTVULKAN_H_

#include "Font.h"
#include "Text.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <memory>
#include <string>
#include <unordered_map>

namespace DisplayOutput
{

class CTextureFlatVulkan;
class CRendererVulkan;

// Per-codepoint glyph metrics stored after atlas build.
struct GlyphInfo
{
    float   u0, v0, u1, v1;     // normalised UV coords in the atlas
    int32_t bearingX, bearingY; // offset from pen baseline to glyph top-left (px)
    int32_t width,   height;    // glyph bitmap dimensions (px)
    int32_t advance;            // horizontal advance (px)
};

/*
    CFontVulkan — FreeType-backed font with a GPU glyph atlas.
*/
class CFontVulkan : public CBaseFont
{
  public:
    CFontVulkan()  = default;
    ~CFontVulkan() override;

    // CBaseFont pure-virtual — no-op until CreateWithRenderer() is called.
    bool Create() override { return true; }

    // Called by CRendererVulkan::GetFont() after setting FontDescription.
    bool CreateWithRenderer(CRendererVulkan* renderer);

    const GlyphInfo* GetGlyph(uint32_t codepoint) const;
    std::shared_ptr<CTextureFlatVulkan> AtlasTexture() const { return m_atlas; }
    int32_t LineHeight() const { return m_lineHeight; }
    int32_t Ascender()   const { return m_ascender; }

  private:
    FT_Library m_ftLib  = nullptr;
    FT_Face    m_ftFace = nullptr;

    int32_t m_lineHeight = 0;
    int32_t m_ascender   = 0;

    std::shared_ptr<CTextureFlatVulkan>     m_atlas;
    std::unordered_map<uint32_t, GlyphInfo> m_glyphs;

    bool findFontFile(const std::string& typeface, std::string& outPath) const;
    bool buildAtlas(CRendererVulkan* renderer);
};

MakeSmartPointers(CFontVulkan);

/*
    CTextVulkan — holds a string and its associated font.
    GetExtent() returns normalised screen-space dimensions [0,1].
*/
class CTextVulkan : public CBaseText
{
  public:
    CTextVulkan(spCBaseFont font, const std::string& text,
                CRendererVulkan* renderer);
    ~CTextVulkan() override = default;

    void                 SetText(const std::string& _text) override { m_text = _text; }
    Base::Math::CVector2 GetExtent() override;
    void                 SetEnabled(bool _enabled) override { m_enabled = _enabled; }

    bool               IsEnabled() const { return m_enabled; }
    spCBaseFont        GetFont()   const { return m_font; }
    const std::string& Text()      const { return m_text; }

  private:
    spCBaseFont      m_font;
    std::string      m_text;
    bool             m_enabled  = true;
    CRendererVulkan* m_renderer = nullptr;
};

MakeSmartPointers(CTextVulkan);

} // namespace DisplayOutput

#endif
