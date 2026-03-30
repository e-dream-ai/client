#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <wrl/client.h>

#include "../Renderer/Font.h"
#include "../Renderer/Text.h"
#include "TextureFlatDX11.h"
#include "../../Common/Math/Rect.h"

#ifdef WIN32
#include <windows.h>
#endif

namespace DisplayOutput
{

// Minimal UTF-8 scanner for HUD strings (BMP code points; invalid bytes -> U+003F).
inline bool Utf8DecodeNext(const std::string& s, size_t& i, uint32_t& outCp)
{
    if (i >= s.size())
        return false;
    const unsigned char b0 = static_cast<unsigned char>(s[i]);
    if (b0 < 0x80u)
    {
        outCp = b0;
        ++i;
        return true;
    }
    if ((b0 & 0xE0u) == 0xC0u && i + 1 < s.size())
    {
        const unsigned b1 = static_cast<unsigned char>(s[i + 1]);
        outCp = (uint32_t(b0 & 0x1Fu) << 6) | uint32_t(b1 & 0x3Fu);
        if (outCp >= 0x80u)
        {
            i += 2;
            return true;
        }
    }
    else if ((b0 & 0xF0u) == 0xE0u && i + 2 < s.size())
    {
        const unsigned b1 = static_cast<unsigned char>(s[i + 1]);
        const unsigned b2 = static_cast<unsigned char>(s[i + 2]);
        outCp = (uint32_t(b0 & 0x0Fu) << 12) | (uint32_t(b1 & 0x3Fu) << 6) | uint32_t(b2 & 0x3Fu);
        if (outCp >= 0x800u &&
            !(outCp >= 0xD800u && outCp <= 0xDFFFu)) // not UTF-16 surrogate
        {
            i += 3;
            return true;
        }
    }
    else if ((b0 & 0xF8u) == 0xF0u && i + 3 < s.size())
    {
        const unsigned b1 = static_cast<unsigned char>(s[i + 1]);
        const unsigned b2 = static_cast<unsigned char>(s[i + 2]);
        const unsigned b3 = static_cast<unsigned char>(s[i + 3]);
        outCp = (uint32_t(b0 & 0x07u) << 18) | (uint32_t(b1 & 0x3Fu) << 12) |
                (uint32_t(b2 & 0x3Fu) << 6) | uint32_t(b3 & 0x3Fu);
        if (outCp >= 0x10000u && outCp <= 0x10FFFFu)
        {
            i += 4;
            return true;
        }
    }
    outCp = '?';
    ++i;
    return true;
}

// CPU-built RGBA glyph atlas + GPU texture (DX11) used to render HUD text
// via quads inside the normal DX11 render pass (no per-frame GDI draws).
//
// Note: class name follows the plan; internally the glyph rasterization is
// done once during atlas creation and then reused for all frames.
class CFontDX11DirectWriteAtlas final : public CBaseFont
{
public:
    // _device/_context are used to create the atlas GPU texture.
    CFontDX11DirectWriteAtlas(Microsoft::WRL::ComPtr<ID3D11Device> device,
                               Microsoft::WRL::ComPtr<ID3D11DeviceContext> context);
    ~CFontDX11DirectWriteAtlas() override;

    bool Create() override;

    // Ensures the CPU atlas + GPU texture are built.
    bool EnsureCreated();

    uint32_t LineHeightPx() const { return m_lineHeightPx; }

    // For renderer: GPU texture containing the atlas.
    spCTextureFlat AtlasTexture() const { return m_spAtlasTexture; }

    // Returns glyph metrics for a Unicode code point (BMP). Missing glyphs resolve to space.
    struct SGlyph
    {
        uint32_t widthPx = 0;
        uint32_t heightPx = 0;
        float advancePx = 0.0f; // how far to move pen in pixels
        float penOffsetXPx = 0.0f; // quad left = pen + offset (min(0, abcA) from GDI)
        Base::Math::CRect uvRect; // atlas uv: (u0,v0,u1,v1) in normalized [0..1]
    };

    const SGlyph& GetGlyph(uint32_t codePoint) const;

    // Measures text extent in pixels (supports '\n').
    uint32_t MeasureTextWidthPx(const std::string& text) const;
    uint32_t MeasureTextHeightPx(const std::string& text) const;

private:
    bool BuildAtlasGdiOnce(); // rasterizes glyphs once into atlas pixels
    void ConvertAtlasToRGBA(std::vector<uint8_t>& outRGBA) const;

private:
    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;

    // CPU atlas pixels (RGBA8).
    std::vector<uint8_t> m_atlasRGBA;
    uint32_t m_atlasW = 0;
    uint32_t m_atlasH = 0;

    std::unordered_map<uint32_t, SGlyph> m_glyphByCodepoint;
    SGlyph m_spaceGlyph;

    // Font metrics for positioning.
    uint32_t m_ascentPx = 0;
    uint32_t m_lineHeightPx = 0;

    // GPU texture for the atlas.
    spCTextureFlat m_spAtlasTexture;
};

// Text object holds the current string + rect in normalized coords.
class CTextDX11Atlas final : public CBaseText
{
public:
    CTextDX11Atlas(std::shared_ptr<CFontDX11DirectWriteAtlas> font,
                    std::string text,
                    uint32_t displayWidthPx,
                    uint32_t displayHeightPx);

    ~CTextDX11Atlas() override = default;

    void SetText(const std::string& text) override { m_text = text; }
    Base::Math::CVector2 GetExtent() override;

    void SetEnabled(bool enabled) override { m_enabled = enabled; }
    bool Enabled() const { return m_enabled; }

    const std::string& Text() const { return m_text; }
    std::shared_ptr<CFontDX11DirectWriteAtlas> Font() const { return m_font; }
    uint32_t DisplayWidthPx() const { return m_displayW; }
    uint32_t DisplayHeightPx() const { return m_displayH; }

    void SetDisplaySize(uint32_t widthPx, uint32_t heightPx);
    void SyncLayoutDisplay(uint32_t widthPx, uint32_t heightPx) override
    {
        SetDisplaySize(widthPx, heightPx);
    }

private:
    std::shared_ptr<CFontDX11DirectWriteAtlas> m_font;
    std::string m_text;
    bool m_enabled = true;

    // Used for normalized->pixel conversion and back.
    uint32_t m_displayW = 1;
    uint32_t m_displayH = 1;
};

} // namespace DisplayOutput

