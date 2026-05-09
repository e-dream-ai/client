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

bool AudioPanelWin32_IsVisible()
{
    return g_audioPanelVisible.load(std::memory_order_acquire);
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

    const auto tip = [](const char* text) {
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", text);
    };

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

    // Checkbox + slider on one row; slider is greyed when checkbox is off.
    // checkTip shown on checkbox hover, sliderTip shown on slider hover (works even when disabled).
    const auto drawWeightRow = [&](const char* checkId, bool* use, const char* useKey,
                                   const char* label, float* val, const char* valKey,
                                   const char* checkTip, const char* sliderTip)
    {
        if (ImGui::Checkbox(checkId, use))
            g_Settings()->Set(useKey, *use);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", checkTip);
        ImGui::SameLine(0, 2);
        ImGui::BeginDisabled(!(*use));
        ImGui::TextUnformatted(label);
        ImGui::SameLine(110.0f);
        ImGui::PushItemWidth(-1);
        if (ImGui::SliderFloat((std::string("##") + label).c_str(), val, 0.0f, 1.0f, "%.2f"))
            g_Settings()->Set(valKey, *val);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", sliderTip);
        ImGui::PopItemWidth();
        ImGui::EndDisabled();
    };

    // ── Analyser ──────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Analyser", ImGuiTreeNodeFlags_DefaultOpen))
    {
        drawSlider("Bass",       &g_audioBassMult,      0.5f,   3.0f,    "settings.player.audio_bass_mult");
        tip("Multiplier on normalised bass energy (30-200 Hz).");
        drawSlider("Mid",        &g_audioMidMult,       0.5f,   3.0f,    "settings.player.audio_mid_mult");
        tip("Multiplier on normalised mid energy (200-3500 Hz).");
        drawSlider("High",       &g_audioHighMult,      0.5f,   3.0f,    "settings.player.audio_high_mult");
        tip("Multiplier on normalised high energy (3.5-20 kHz).");
        drawSlider("Peak decay", &g_audioPeakDecay,     0.95f,  0.99999f,"settings.player.audio_peak_decay", "%.5f");
        tip("How slowly the per-band peak normalisation decays. Higher = longer memory, slower adaptation.");
        drawSlider("Dark brt",   &g_audioDarkBrightness,-1.0f,  0.0f,    "settings.player.audio_dark_brightness");
        tip("Brightness floor when no audio signal is present.");
    }

    // ── FPS ───────────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("FPS", ImGuiTreeNodeFlags_DefaultOpen))
    {
        drawCheck("Enable##fps", &g_audioFpsEnabled, "settings.player.audio_fps_enabled");
        tip("Vary playback frame rate with audio intensity.");
        drawSlider("Min FPS", &g_audioFpsMin, 1.0f, 120.0f, "settings.player.audio_fps_min");
        tip("Frame rate when audio is quiet or absent.");
        drawSlider("Max FPS", &g_audioFpsMax, 1.0f, 120.0f, "settings.player.audio_fps_max");
        tip("Frame rate at peak audio intensity.");
        drawWeightRow("##useC", &g_audioFpsUseCentroid, "settings.player.audio_fps_use_centroid",
                      "W centroid", &g_audioFpsWeightCentroid, "settings.player.audio_fps_weight_centroid",
                      "Tick to include spectral centroid in the FPS calculation.\n"
                      "Centroid is the weighted average frequency of the spectrum -a measure of perceptual brightness.\n"
                      "Unlike fixed bands, it shifts continuously as the tonal balance changes.",
                      "Relative influence of spectral centroid vs other active bands.\n"
                      "Centroid rises as music gets brighter (more treble) and falls for bass-heavy passages.\n"
                      "Good for tracking tonal shifts rather than raw energy level.");
        drawWeightRow("##useM", &g_audioFpsUseMid, "settings.player.audio_fps_use_mid",
                      "W mid", &g_audioFpsWeightMid, "settings.player.audio_fps_weight_mid",
                      "Tick to include mid-range energy (200-3500 Hz) in the FPS calculation.\n"
                      "Covers vocals, guitars, synths, and most melodic content.",
                      "Relative influence of mid-range energy (200-3500 Hz) vs other active bands.\n"
                      "Mid is the most presence-heavy band -high during busy sections, lower during breakdowns.\n"
                      "Differs from centroid in that it measures raw energy in a fixed range, not tonal balance.");
        drawWeightRow("##useH", &g_audioFpsUseHigh, "settings.player.audio_fps_use_high",
                      "W high", &g_audioFpsWeightHigh, "settings.player.audio_fps_weight_high",
                      "Tick to include high-frequency energy (3.5-20 kHz) in the FPS calculation.\n"
                      "Covers cymbals, hi-hats, sibilance, and air.",
                      "Relative influence of high-frequency energy (3.5-20 kHz) vs other active bands.\n"
                      "High tracks cymbal work and brightness -useful for driving FPS with rhythmic hi-hat patterns.\n"
                      "Quieter than mid or bass in most music, so weight often needs to be higher to compensate.");
        drawWeightRow("##useB", &g_audioFpsUseBass, "settings.player.audio_fps_use_bass",
                      "W bass", &g_audioFpsWeightBass, "settings.player.audio_fps_weight_bass",
                      "Tick to include bass energy (30-200 Hz) in the FPS calculation.\n"
                      "Covers kick drums, bass guitar, and sub-bass.",
                      "Relative influence of bass energy (30-200 Hz) vs other active bands.\n"
                      "Bass is typically the most stable band -it provides a steady baseline FPS.\n"
                      "Less reactive to melodic dynamics than centroid or mid.");
        drawSlider("Attack",  &g_audioFpsAttack,  0.01f, 1.0f, "settings.player.audio_fps_attack");
        tip("How quickly FPS rises toward the target (0 = never, 1 = instant).");
        drawSlider("Release", &g_audioFpsRelease, 0.01f, 1.0f, "settings.player.audio_fps_release");
        tip("How quickly FPS falls back toward minimum when audio quietens.");
    }

    // ── Cuts ──────────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Cuts"))
    {
        drawSlider("Global cooldown", &g_audioCutGlobalCooldown, 0.1f, 10.0f, "settings.player.audio_cut_global_cooldown");
        tip("Minimum seconds between any two audio-triggered cuts.");
        ImGui::Spacing();

        drawCheck("Transient",     &g_audioCutTransientEnabled,   "settings.player.audio_cut_transient_enabled");
        tip("Trigger a cut on broadband transients (sudden energy across all bands).");
        drawSlider("Trans thresh", &g_audioCutTransientThreshold, 0.1f, 1.0f, "settings.player.audio_cut_transient_threshold");
        tip("Smoothed transient level that triggers the cut.");
        drawCombo("Trans style",   &g_audioCutTransientStyle,     kCutStyles, 3, "settings.player.audio_cut_transient_style");
        tip("Transition style applied when a transient cut fires.");
        ImGui::Spacing();

        drawCheck("Kick",        &g_audioCutKickEnabled,   "settings.player.audio_cut_kick_enabled");
        tip("Trigger a cut on bass kicks (sharp bass energy rise).");
        drawSlider("Kick thresh",&g_audioCutKickThreshold, 0.1f, 1.0f, "settings.player.audio_cut_kick_threshold");
        tip("Smoothed kick level that triggers the cut.");
        drawCombo("Kick style",  &g_audioCutKickStyle,     kCutStyles, 3, "settings.player.audio_cut_kick_style");
        tip("Transition style applied when a kick cut fires.");
        ImGui::Spacing();

        drawCheck("Snare",        &g_audioCutSnareEnabled,   "settings.player.audio_cut_snare_enabled");
        tip("Trigger a cut on snare hits (mid-dominant transient, more mid than bass rise).");
        drawSlider("Snare thresh",&g_audioCutSnareThreshold, 0.1f, 1.0f, "settings.player.audio_cut_snare_threshold");
        tip("Smoothed snare level that triggers the cut.");
        drawCombo("Snare style",  &g_audioCutSnareStyle,     kCutStyles, 3, "settings.player.audio_cut_snare_style");
        tip("Transition style applied when a snare cut fires.");
        ImGui::Spacing();

        drawCheck("Beat trigger", &g_audioCutBeatEnabled, "settings.player.audio_cut_beat_enabled");
        tip("Trigger a cut every N beats as detected by the tempo tracker.");
        drawSliderInt("Beat N",   &g_audioCutBeatN, 1, 8, "settings.player.audio_cut_beat_n");
        tip("Number of beats between beat-triggered cuts.");
        drawCombo("Beat style",   &g_audioCutBeatStyle, kCutStyles, 3, "settings.player.audio_cut_beat_style");
        tip("Transition style applied when a beat cut fires.");
        ImGui::Spacing();

        drawCheck("Volume trigger", &g_audioCutVolumeEnabled, "settings.player.audio_cut_volume_enabled");
        tip("Trigger a cut when overall volume crosses a threshold.");
        drawSlider("Vol thresh",    &g_audioCutVolumeThreshold, 0.1f, 1.0f, "settings.player.audio_cut_volume_threshold");
        tip("Volume level that triggers the cut.");
        drawCombo("Vol style",      &g_audioCutVolumeStyle, kCutStyles, 3, "settings.player.audio_cut_volume_style");
        tip("Transition style applied when a volume cut fires.");
    }

    // ── Mixing ────────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Mixing"))
    {
        drawCheck("Enable##mix", &g_audioMixEnabled, "settings.player.audio_mix_enabled");
        tip("Control the blend between two dream sequences using an audio signal.");
        drawCombo("Source",      &g_audioMixSource, kMixSources, 8, "settings.player.audio_mix_source");
        tip("Audio signal that drives the mix position.");
        drawSlider("Mix min",    &g_audioMixMin, 0.0f, 1.0f, "settings.player.audio_mix_min");
        tip("Blend position when the audio source is at zero.");
        drawSlider("Mix max",    &g_audioMixMax, 0.0f, 1.0f, "settings.player.audio_mix_max");
        tip("Blend position when the audio source is at peak.");
        drawSlider("Smooth",     &g_audioMixSmooth, 0.01f, 1.0f, "settings.player.audio_mix_smooth");
        tip("IIR smoothing applied to the mix position. Lower = smoother but slower to react.");
    }

    ImGui::End();

    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    return true;
}

#endif
