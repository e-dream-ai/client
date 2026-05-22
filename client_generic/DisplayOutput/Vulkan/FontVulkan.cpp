#ifndef WIN32

#include "FontVulkan.h"
#include "RendererVulkan.h"
#include "Log.h"
#include "PlatformUtils.h"

#include <imgui.h>

namespace DisplayOutput
{

// ---------------------------------------------------------------------------
// CreateWithRenderer — register font with ImGui's global atlas.
//
// Loads Lato-Regular.ttf from the binary directory (copied there by CMake).
// Falls back to the embedded ProggyForever font if the file is not found.
// ---------------------------------------------------------------------------
bool CFontImGui::CreateWithRenderer(CRendererVulkan* /*renderer*/)
{
    m_fontSizePx = static_cast<float>(m_FontDescription.Height());
    const bool isBold = m_FontDescription.Style() >= CFontDescription::Bold;

    std::string fontPath = PlatformUtils::GetWorkingDir() + "Lato-Regular.ttf";
    m_imFont = ImGui::GetIO().Fonts->AddFontFromFileTTF(fontPath.c_str(), m_fontSizePx);
    if (m_imFont)
    {
        g_Log->Info("CFontImGui: loaded Lato-Regular.ttf at %.0fpx%s",
                    m_fontSizePx, isBold ? " (bold)" : "");
        return true;
    }

    g_Log->Warning("CFontImGui: Lato-Regular.ttf not found at %s, using embedded font",
                   fontPath.c_str());

    ImFontConfig cfg;
    cfg.SizePixels = m_fontSizePx;
    m_imFont = ImGui::GetIO().Fonts->AddFontDefaultVector(&cfg);
    if (!m_imFont)
    {
        g_Log->Warning("CFontImGui: AddFontDefaultVector also failed at %.0fpx", m_fontSizePx);
        return false;
    }

    g_Log->Info("CFontImGui: registered embedded font at %.0fpx%s",
                m_fontSizePx, isBold ? " (bold)" : "");
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
    const float fontSizePx = spFI->FontSize() * scale;
    ImVec2 sz = font->CalcTextSizeA(fontSizePx, FLT_MAX, 0.f, m_text.c_str());
    return {sz.x / screenW, sz.y / screenH};
}

} // namespace DisplayOutput

#endif // !WIN32
