#ifndef _FONTVULKAN_H_
#define _FONTVULKAN_H_

#include "Font.h"
#include "Text.h"

#include <memory>
#include <string>

// Forward-declare ImGui types so callers need not include imgui.h.
struct ImFont;

namespace DisplayOutput
{

class CRendererVulkan;

/*
    CFontImGui — ImGui-backed font for Vulkan text rendering.

    The font is registered with ImGui's global atlas via AddFontFromFileTTF().
    The atlas is rebuilt lazily (on the next BeginFrame) by CRendererVulkan
    after new fonts are added.  No separate glyph atlas texture is managed
    here — ImGui owns the GPU font texture through imgui_impl_vulkan.
*/
class CFontImGui : public CBaseFont
{
  public:
    CFontImGui()  = default;
    ~CFontImGui() override = default;

    // CBaseFont pure-virtual — no-op until CreateWithRenderer() is called.
    bool Create() override { return true; }

    // Called by CRendererVulkan::GetFont() after setting FontDescription.
    // Registers the font with ImGui's atlas; the atlas is rebuilt by the
    // renderer before the next rendered frame.
    bool CreateWithRenderer(CRendererVulkan* renderer);

    ImFont* GetImFont() const { return m_imFont; }
    float   FontSize()  const { return m_fontSizePx; }

  private:
    ImFont* m_imFont     = nullptr;
    float   m_fontSizePx = 16.0f;
};

MakeSmartPointers(CFontImGui);

/*
    CTextImGui — holds a string and its associated ImGui font.
    GetExtent() returns normalised screen-space dimensions [0,1].
*/
class CTextImGui : public CBaseText
{
  public:
    CTextImGui(spCBaseFont font, const std::string& text,
               CRendererVulkan* renderer);
    ~CTextImGui() override = default;

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

MakeSmartPointers(CTextImGui);

} // namespace DisplayOutput

#endif
