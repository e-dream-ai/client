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

    // Use Display()->Width/Height() — the actual window dimensions — so that
    // font measurements are consistent with the edge/step calculations in
    // StatsConsole and StartupScreen, which also use Display()->Width/Height().
    // Using m_swapExtent here diverges from those calculations during the
    // fullscreen↔windowed transition (swapchain recreates after ConfigureNotify)
    // and produces a mismatched box shape for several frames.
    float screenW = static_cast<float>(m_renderer->Display()->Width());
    float screenH = static_cast<float>(m_renderer->Display()->Height());
    if (screenW <= 0.f || screenH <= 0.f) return {0.f, 0.f};

    static constexpr float kHudReferenceHeight = 1080.f;
    const float scale    = screenH / kHudReferenceHeight;
    const float fontSize = spFI->FontSize() * scale;
    ImVec2 sz = font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, m_text.c_str());
    return {sz.x / screenW, sz.y / screenH};
}

} // namespace DisplayOutput

#endif // !WIN32
