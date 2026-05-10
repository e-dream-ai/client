#ifdef WIN32

#include "AudioPanelWin32.h"
#include "AudioAnalyzer.h"
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
    if (g_Settings()->Storage())
        g_Settings()->Storage()->Commit();
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

    // Unlike modal dialogs, the audio panel is a non-modal overlay. Only consume
    // input when ImGui actually wants it — otherwise native chrome interactions
    // (close/min/max buttons) are swallowed even when the cursor isn't near the panel.
    const ImGuiIO& io = ImGui::GetIO();
    const bool isMouse = (msg == WM_MOUSEMOVE    || msg == WM_LBUTTONDOWN ||
                          msg == WM_LBUTTONUP    || msg == WM_RBUTTONDOWN ||
                          msg == WM_RBUTTONUP    || msg == WM_MBUTTONDOWN ||
                          msg == WM_MBUTTONUP    || msg == WM_MOUSEWHEEL  ||
                          msg == WM_MOUSEHWHEEL);
    const bool isKey   = (msg == WM_KEYDOWN   || msg == WM_KEYUP   ||
                          msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP || msg == WM_CHAR);
    if ((isMouse && !io.WantCaptureMouse) || (isKey && !io.WantCaptureKeyboard))
        return false;

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
        drawSlider("Bass",       &g_audioBassMult,      0.5f,   2.0f,    "settings.player.audio_bass_mult");
        tip("Multiplier on normalised bass energy (20-200 Hz).");
        drawSlider("Mid",        &g_audioMidMult,       0.5f,   2.0f,    "settings.player.audio_mid_mult");
        tip("Multiplier on normalised mid energy (200-4000 Hz).");
        drawSlider("High",       &g_audioHighMult,      0.5f,   2.0f,    "settings.player.audio_high_mult");
        tip("Multiplier on normalised high energy (4-11 kHz).");
        drawSlider("Peak decay", &g_audioPeakDecay,     0.95f,  0.99999f,"settings.player.audio_peak_decay", "%.5f");
        tip("How slowly the per-band peak normalisation decays. Higher = longer memory, slower adaptation.");
        drawSlider("Peak floor", &g_audioPeakFloor,     0.0f,   0.1f,    "settings.player.audio_peak_floor", "%.4f");
        tip("Minimum peak value — prevents normalisation from chasing the noise floor during quiet passages.\n"
            "Raise this if bands read too high during silence or quiet sections.");
        drawSlider("Dark brt",   &g_audioDarkBrightness,-1.0f,  0.0f,    "settings.player.audio_dark_brightness");
        tip("Brightness floor when no audio signal is present.");
    }

    // ── Beat ──────────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Beat", ImGuiTreeNodeFlags_DefaultOpen))
    {
        static bool  s_bpmLocked = false;
        static float s_lockedBpm = 120.0f;

        // Beat count is monotonically incremented by the audio thread on every beat wrap.
        // Deriving beatIndex from it (rather than detecting phase wraps per frame) handles
        // multiple beats per render frame correctly after a dream change drains audio backlog.
        const uint32_t beatCount  = AudioAnalyzer_GetBeatCount();
        const float    phase      = AudioAnalyzer_GetBeatPhase();
        const int      s_beatIndex = static_cast<int>(beatCount % 4);

        // BPM readout + Tap + Reset on one row
        const float liveBpm = AudioAnalyzer_GetCurrentBpm();
        ImGui::Text("Live BPM: %.1f", liveBpm);
        ImGui::SameLine(110.0f);
        const float halfW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (ImGui::Button("Tap##bpm", ImVec2(halfW, 0)))
        {
            const float tapped = AudioAnalyzer_TapTempo();
            if (tapped > 0.0f)
            {
                s_lockedBpm = tapped;
                s_bpmLocked = true;
            }
        }
        tip("Tap to the beat. Locks BPM automatically once two or more taps are recorded.\n"
            "Resets if you stop tapping for more than 3 seconds.");
        ImGui::SameLine();
        if (ImGui::Button("Reset##beat", ImVec2(-1, 0)))
            AudioAnalyzer_ResetBeatPhase();
        tip("Snap beat phase to beat 1 immediately.");

        // 4-box beat visualizer — box 1 is beat 1, pulses on each beat
        {
            const float avail = ImGui::GetContentRegionAvail().x;
            const float gap   = 4.0f;
            const float boxW  = (avail - gap * 3.0f) / 4.0f;
            const float boxH  = 22.0f;

            ImDrawList* dl     = ImGui::GetWindowDrawList();
            const ImVec2 pos   = ImGui::GetCursorScreenPos();

            for (int i = 0; i < 4; ++i)
            {
                const float x     = pos.x + i * (boxW + gap);
                const bool active = (i == s_beatIndex);
                // Pulse: bright at phase=0, half-bright at phase=1
                const float pulse = active ? (1.0f - phase * 0.5f) : 0.0f;
                const int   alpha = active ? (int)(pulse * 200.0f) + 40 : 110;

                ImU32 fillCol;
                if (i == 0)
                    fillCol = active ? IM_COL32(255, 210, 40,  alpha)  // beat 1: gold
                                     : IM_COL32(80,  65,  15,  alpha);
                else
                    fillCol = active ? IM_COL32(140, 140, 255, alpha)  // beats 2-4: blue-white
                                     : IM_COL32(45,  45,  45,  alpha);

                dl->AddRectFilled(ImVec2(x, pos.y), ImVec2(x + boxW, pos.y + boxH),
                                  fillCol, 3.0f);

                char num[4];
                snprintf(num, sizeof(num), "%d", i + 1);
                const ImVec2 tsz = ImGui::CalcTextSize(num);
                dl->AddText(ImVec2(x + (boxW - tsz.x) * 0.5f, pos.y + (boxH - tsz.y) * 0.5f),
                            active ? IM_COL32(255, 255, 255, 255) : IM_COL32(160, 160, 160, 180),
                            num);
            }
            ImGui::Dummy(ImVec2(avail, boxH));
        }

        // Tempo nudge — hold to temporarily shift tempo ±5%; release restores locked BPM exactly.
        // Auto-locks at current BPM on first press if not already locked.
        float nudgeFactor = 0.0f;
        {
            const float nudgeW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            ImGui::Button("< Slow##nudge", ImVec2(nudgeW, 0));
            if (ImGui::IsItemActivated() && !s_bpmLocked)
            {
                s_lockedBpm = AudioAnalyzer_GetCurrentBpm();
                s_bpmLocked = true;
            }
            if (ImGui::IsItemActive()) nudgeFactor = -0.05f;
            tip("Hold to slow tempo 5% — release to restore.\nUse to drift beat phase back into sync.");
            ImGui::SameLine();
            ImGui::Button("Fast >##nudge", ImVec2(-1, 0));
            if (ImGui::IsItemActivated() && !s_bpmLocked)
            {
                s_lockedBpm = AudioAnalyzer_GetCurrentBpm();
                s_bpmLocked = true;
            }
            if (ImGui::IsItemActive()) nudgeFactor = +0.05f;
            tip("Hold to speed tempo 5% — release to restore.\nUse to drift beat phase forward into sync.");
        }

        // Lock BPM row
        if (ImGui::Checkbox("Lock BPM", &s_bpmLocked) && !s_bpmLocked)
            AudioAnalyzer_SetBpmOverride(0.0f);
        ImGui::SameLine(110.0f);
        ImGui::PushItemWidth(-1);
        ImGui::BeginDisabled(!s_bpmLocked);
        if (ImGui::InputFloat("##lockedbpm", &s_lockedBpm, 1.0f, 5.0f, "%.1f"))
            s_lockedBpm = std::max(40.0f, std::min(250.0f, s_lockedBpm));
        ImGui::EndDisabled();
        ImGui::PopItemWidth();
        tip("BPM used when locked. Drag or use +/- to nudge.");

        // nudgeFactor temporarily scales the override while held; s_lockedBpm is never mutated
        AudioAnalyzer_SetBpmOverride(s_bpmLocked ? s_lockedBpm * (1.0f + nudgeFactor) : 0.0f);
    }

    // ── FPS ───────────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("FPS", ImGuiTreeNodeFlags_DefaultOpen))
    {
        drawCheck("Enable##fps", &g_audioFpsEnabled, "settings.player.audio_fps_enabled");
        tip("Vary playback frame rate with audio intensity.");
        drawSlider("Min FPS", &g_audioFpsMin, 1.0f, 240.0f, "settings.player.audio_fps_min");
        tip("Frame rate when audio is quiet or absent.");
        drawSlider("Max FPS", &g_audioFpsMax, 1.0f, 240.0f, "settings.player.audio_fps_max");
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
        drawSlider("Min volume",      &g_audioCutMinVolume,      0.0f,  1.0f, "settings.player.audio_cut_min_volume");
        tip("Cuts only fire when overall volume is at or above this level.");
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
        tip("Trigger a cut every N bars on beat 1.");
        drawSliderInt("Bars",     &g_audioCutBeatN, 1, 8, "settings.player.audio_cut_beat_n");
        tip("Number of bars between beat-triggered cuts. Cuts only fire on beat 1.");
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
