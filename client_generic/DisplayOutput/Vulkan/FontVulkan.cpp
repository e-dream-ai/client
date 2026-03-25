#ifndef WIN32

#include "FontVulkan.h"
#include "RendererVulkan.h"
#include "Log.h"

#include <imgui.h>

namespace DisplayOutput
{

// ---------------------------------------------------------------------------
// CreateWithRenderer — register font with ImGui's global atlas.
//
// Uses ImGui's built-in embedded ProggyForever font (MIT license, no external
// files required).  Bold is simulated with a small extra advance so key labels
// in the F1 overlay sit visually apart from surrounding normal text.
// ---------------------------------------------------------------------------
bool CFontImGui::CreateWithRenderer(CRendererVulkan* /*renderer*/)
{
    m_fontSize = static_cast<float>(m_FontDescription.Height());
    const bool isBold = m_FontDescription.Style() >= CFontDescription::Bold;

    ImFontConfig cfg;
    cfg.SizePixels = m_fontSize;

    m_imFont = ImGui::GetIO().Fonts->AddFontDefaultVector(&cfg);
    if (!m_imFont)
    {
        g_Log->Warning("CFontImGui: AddFontDefaultVector failed at %.0fpx", m_fontSize);
        return false;
    }

    g_Log->Info("CFontImGui: registered embedded font at %.0fpx%s",
                m_fontSize, isBold ? " (bold)" : "");
    return true;
}

// ---------------------------------------------------------------------------
// CTextImGui
// ---------------------------------------------------------------------------
CTextImGui::CTextImGui(spCBaseFont font, const std::string& text,
                       CRendererVulkan* renderer)
    : m_font(std::move(font)), m_text(text), m_renderer(renderer)
{}

Base::Math::CVector2 CTextImGui::GetExtent()
{
    if (!m_font || !m_renderer) return {0.f, 0.f};

    auto spFI = std::dynamic_pointer_cast<CFontImGui>(m_font);
    if (!spFI) return {0.f, 0.f};

    ImFont* font = spFI->GetImFont();
    if (!font || !font->IsLoaded()) return {0.f, 0.f};

    VkExtent2D ext    = m_renderer->GetSwapExtent();
    float      screenW = static_cast<float>(ext.width);
    float      screenH = static_cast<float>(ext.height);
    if (screenW <= 0.f || screenH <= 0.f) return {0.f, 0.f};

    // Strip inline bold markers (\x01 start-bold, \x02 end-bold) before measuring.
    std::string clean;
    clean.reserve(m_text.size());
    for (char c : m_text)
        if (c != '\x01' && c != '\x02')
            clean += c;

    float  fontSize = spFI->FontSize();
    ImVec2 sz       = font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, clean.c_str());
    return {sz.x / screenW, sz.y / screenH};
}

} // namespace DisplayOutput

#endif // !WIN32
