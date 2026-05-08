#ifdef WIN32

#include "AudioPanelWin32.h"
#include "Settings.h"

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "../DisplayOutput/D3D11/DisplayDX11.h"
#include "Player.h"
#include "SettingsDialogWin32.h"

#include <atomic>
#include <string>



// ── Panel state ───────────────────────────────────────────────────────────────
static std::atomic<bool> g_audioPanelVisible{false};
static bool g_imguiInitialized = false;

void AudioPanelWin32_SetVisible(bool visible)
{
    g_audioPanelVisible.store(visible, std::memory_order_release);
}

// ── ImGui init/shutdown ───────────────────────────────────────────────────────
static void TryInitImGui(ID3D11Device* device, ID3D11DeviceContext* ctx)
{
    if (g_imguiInitialized)
        return;
    auto* dx =
        dynamic_cast<DisplayOutput::CDisplayDX11*>(g_Player().Display().get());
    if (!dx)
        return;
    HWND hwnd = dx->GetWindowHandle();
    if (!hwnd)
        return;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(device, ctx);
    g_imguiInitialized = true;
}

static void ShutdownImGui()
{
    if (!g_imguiInitialized)
        return;
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    g_imguiInitialized = false;
}

bool AudioPanelWin32_TryConsumeWndProc(HWND hWnd, UINT msg, WPARAM wParam,
                                       LPARAM lParam, LRESULT* outResult)
{
    if (!g_audioPanelVisible.load(std::memory_order_acquire) ||
        !g_imguiInitialized)
        return false;

    extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
        HWND, UINT, WPARAM, LPARAM);
    const LRESULT r = ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
    if (r != 0)
    {
        if (outResult)
            *outResult = r;
        return true;
    }
    return false;
}

// ── Main render function ──────────────────────────────────────────────────────
bool AudioPanelWin32_RenderIfNeeded(ID3D11Device* device,
                                    ID3D11DeviceContext* ctx,
                                    ID3D11RenderTargetView* rtv,
                                    float viewportW, float viewportH)
{
    const bool visible = g_audioPanelVisible.load(std::memory_order_acquire);

    if (!visible)
    {
        if (g_imguiInitialized)
            ShutdownImGui();
        return false;
    }

    TryInitImGui(device, ctx);
    if (!g_imguiInitialized)
        return false;

    ctx->OMSetRenderTargets(1, &rtv, nullptr);

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    const float margin = 16.0f;
    const float panelW = 300.0f;
    ImGui::SetNextWindowPos(ImVec2(margin, margin), ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(panelW, 0), ImVec2(panelW, viewportH - margin * 2));
    ImGui::SetNextWindowBgAlpha(0.6f);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_AlwaysAutoResize;

    ImGui::Begin("##AudioPanel", nullptr, flags);

    ImGui::TextUnformatted("Audio Reactive");
    ImGui::Separator();

    // ── Helpers ───────────────────────────────────────────────────────────────

    static const char* kCutStyles[]  = {"Hard (0.05s)", "Fast (0.2s)", "Slow (2.0s)"};
    static const char* kMixSources[] = {"Bass", "Mid", "High", "Centroid", "Kick", "Snare", "Transient", "Beat Phase"};

    const auto drawSlider = [](const char* label, float* val, float vmin, float vmax,
                               const char* key, const char* fmt = "%.2f")
    {
        ImGui::TextUnformatted(label);
        ImGui::SameLine(110.0f);
        ImGui::PushItemWidth(-1);
        if (ImGui::SliderFloat((std::string("##") + label).c_str(), val, vmin, vmax, fmt))
            g_Settings()->Set(key, *val);
        ImGui::PopItemWidth();
    };

    const auto drawSliderInt = [](const char* label, int* val, int vmin, int vmax, const char* key)
    {
        ImGui::TextUnformatted(label);
        ImGui::SameLine(110.0f);
        ImGui::PushItemWidth(-1);
        if (ImGui::SliderInt((std::string("##") + label).c_str(), val, vmin, vmax))
            g_Settings()->Set(key, *val);
        ImGui::PopItemWidth();
    };

    const auto drawCombo = [](const char* label, int* val,
                              const char* const* items, int n, const char* key)
    {
        ImGui::TextUnformatted(label);
        ImGui::SameLine(110.0f);
        ImGui::PushItemWidth(-1);
        if (ImGui::Combo((std::string("##") + label).c_str(), val, items, n))
            g_Settings()->Set(key, *val);
        ImGui::PopItemWidth();
    };

    const auto drawCheck = [](const char* label, bool* val, const char* key)
    {
        if (ImGui::Checkbox(label, val))
            g_Settings()->Set(key, *val);
    };

    // ── Analyser ──────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Analyser", ImGuiTreeNodeFlags_DefaultOpen))
    {
        drawSlider("Bass",         &g_audioBassMult,       0.5f,   3.0f,   "settings.player.audio_bass_mult");
        drawSlider("Mid",          &g_audioMidMult,        0.5f,   3.0f,   "settings.player.audio_mid_mult");
        drawSlider("High",         &g_audioHighMult,       0.5f,   3.0f,   "settings.player.audio_high_mult");
        drawSlider("Peak decay",   &g_audioPeakDecay,      0.990f, 0.9999f,"settings.player.audio_peak_decay", "%.4f");
        drawSlider("Onset thresh", &g_audioOnsetThreshold, 0.0001f,0.01f,  "settings.player.audio_onset_threshold", "%.4f");
        drawSlider("Dark brt",     &g_audioDarkBrightness, -1.0f,  0.0f,   "settings.player.audio_dark_brightness");
    }

    // ── FPS ───────────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("FPS", ImGuiTreeNodeFlags_DefaultOpen))
    {
        drawCheck("Enable", &g_audioFpsEnabled, "settings.player.audio_fps_enabled");
        drawSlider("Min FPS",    &g_audioFpsMin,            1.0f, 60.0f, "settings.player.audio_fps_min");
        drawSlider("Max FPS",    &g_audioFpsMax,            1.0f, 60.0f, "settings.player.audio_fps_max");
        drawSlider("W centroid", &g_audioFpsWeightCentroid, 0.0f, 1.0f,  "settings.player.audio_fps_weight_centroid");
        drawSlider("W mid",      &g_audioFpsWeightMid,      0.0f, 1.0f,  "settings.player.audio_fps_weight_mid");
        drawSlider("W high",     &g_audioFpsWeightHigh,     0.0f, 1.0f,  "settings.player.audio_fps_weight_high");
        drawSlider("W bass",     &g_audioFpsWeightBass,     0.0f, 1.0f,  "settings.player.audio_fps_weight_bass");
        drawSlider("Attack",     &g_audioFpsAttack,         0.01f,1.0f,  "settings.player.audio_fps_attack");
        drawSlider("Release",    &g_audioFpsRelease,        0.01f,1.0f,  "settings.player.audio_fps_release");
    }

    // ── Cuts ──────────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Cuts"))
    {
        drawSlider("Global cooldown", &g_audioCutGlobalCooldown, 0.1f, 10.0f, "settings.player.audio_cut_global_cooldown");
        ImGui::Spacing();

        drawCheck("Transient",      &g_audioCutTransientEnabled,   "settings.player.audio_cut_transient_enabled");
        drawSlider("Trans thresh",  &g_audioCutTransientThreshold, 0.1f, 1.0f, "settings.player.audio_cut_transient_threshold");
        drawCombo("Trans style",    &g_audioCutTransientStyle,     kCutStyles, 3, "settings.player.audio_cut_transient_style");
        ImGui::Spacing();

        drawCheck("Kick",           &g_audioCutKickEnabled,   "settings.player.audio_cut_kick_enabled");
        drawSlider("Kick thresh",   &g_audioCutKickThreshold, 0.1f, 1.0f, "settings.player.audio_cut_kick_threshold");
        drawCombo("Kick style",     &g_audioCutKickStyle,     kCutStyles, 3, "settings.player.audio_cut_kick_style");
        ImGui::Spacing();

        drawCheck("Snare",          &g_audioCutSnareEnabled,   "settings.player.audio_cut_snare_enabled");
        drawSlider("Snare thresh",  &g_audioCutSnareThreshold, 0.1f, 1.0f, "settings.player.audio_cut_snare_threshold");
        drawCombo("Snare style",    &g_audioCutSnareStyle,     kCutStyles, 3, "settings.player.audio_cut_snare_style");
        ImGui::Spacing();

        drawCheck("Beat trigger",   &g_audioCutBeatEnabled, "settings.player.audio_cut_beat_enabled");
        drawSliderInt("Beat N",     &g_audioCutBeatN, 1, 8, "settings.player.audio_cut_beat_n");
        drawCombo("Beat style",     &g_audioCutBeatStyle, kCutStyles, 3, "settings.player.audio_cut_beat_style");
        ImGui::Spacing();

        drawCheck("Volume trigger", &g_audioCutVolumeEnabled, "settings.player.audio_cut_volume_enabled");
        drawSlider("Vol thresh",    &g_audioCutVolumeThreshold, 0.1f, 1.0f, "settings.player.audio_cut_volume_threshold");
        drawCombo("Vol style",      &g_audioCutVolumeStyle, kCutStyles, 3, "settings.player.audio_cut_volume_style");
    }

    // ── Mixing ────────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Mixing"))
    {
        drawCheck("Enable",     &g_audioMixEnabled, "settings.player.audio_mix_enabled");
        drawCombo("Source",     &g_audioMixSource, kMixSources, 8, "settings.player.audio_mix_source");
        drawSlider("Mix min",   &g_audioMixMin, 0.0f, 1.0f, "settings.player.audio_mix_min");
        drawSlider("Mix max",   &g_audioMixMax, 0.0f, 1.0f, "settings.player.audio_mix_max");
    }

    ImGui::End();

    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    return true;
}

#endif
