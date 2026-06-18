#ifndef WIN32

#include "FontVulkan.h"
#include "RendererVulkan.h"
#include "Log.h"

#include <imgui.h>
#include <cstdio>
#include <string>
#include <unistd.h>

namespace DisplayOutput
{

// ---------------------------------------------------------------------------
// CreateWithRenderer — register font with ImGui's global atlas.
//
// Tries to load Lato-Regular.ttf from the executable directory (bundled in
// the build output / Runtime).  Falls back to the embedded ProggyClean font
// when the file is absent.
// ---------------------------------------------------------------------------
bool CFontImGui::CreateWithRenderer(CRendererVulkan* /*renderer*/)
{
    m_fontSize = static_cast<float>(m_FontDescription.Height());

    ImFontConfig cfg;
    cfg.SizePixels  = m_fontSize;
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;

    // Resolve the directory containing the running executable.
    ImFont* loaded = nullptr;
    {
        char exeBuf[4096] = {};
        const ssize_t exeLen = readlink("/proc/self/exe", exeBuf, sizeof(exeBuf) - 1);
        if (exeLen > 0)
        {
            exeBuf[exeLen] = '\0';
            std::string exeDir(exeBuf);
            const auto slash = exeDir.rfind('/');
            if (slash != std::string::npos)
                exeDir = exeDir.substr(0, slash + 1);

            const std::string latoPath = exeDir + "Lato-Regular.ttf";
            if (FILE* f = fopen(latoPath.c_str(), "rb"))
            {
                fclose(f);
                loaded = ImGui::GetIO().Fonts->AddFontFromFileTTF(latoPath.c_str(), m_fontSize, &cfg);
                if (loaded)
                    g_Log->Info("CFontImGui: loaded Lato-Regular.ttf at %.0fpx", m_fontSize);
            }
        }
    }

    if (!loaded)
    {
        loaded = ImGui::GetIO().Fonts->AddFontDefaultVector(&cfg);
        if (loaded)
            g_Log->Info("CFontImGui: Lato-Regular.ttf not found, using embedded font at %.0fpx", m_fontSize);
    }

    m_imFont = loaded;
    if (!m_imFont)
    {
        g_Log->Warning("CFontImGui: font load failed at %.0fpx", m_fontSize);
        return false;
    }
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
