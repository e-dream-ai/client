{
const float leftInset = S(138.f);
const float availW    = ImGui::GetContentRegionAvail().x;
const float sliderW   = availW - leftInset - S(8.f);
const float topPad    = S(12.f);
const float rowGap    = S(6.f);

static const char* kCutStyles[]   = {"Hard cut (0.05s)", "Fast dissolve (0.2s)", "Slow dissolve (2.0s)"};
static const char* kMixSources[]  = {"Bass", "Mid", "High", "Centroid", "Kick", "Snare", "Transient", "Beat Phase"};

const auto drawSlider = [&](const char* label, float* val, float vmin, float vmax,
                            const char* key, const char* fmt = "%.2f")
{
    const float labelW = ImGui::CalcTextSize(label).x;
    ImGui::SetCursorPosX(leftInset - labelW - S(8.f));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(0.f, S(8.f));
    ImGui::PushItemWidth(sliderW);
    if (ImGui::SliderFloat((std::string("##") + label).c_str(), val, vmin, vmax, fmt))
        g_Settings()->Set(key, *val);
    ImGui::PopItemWidth();
};

const auto drawSliderInt = [&](const char* label, int* val, int vmin, int vmax, const char* key)
{
    const float labelW = ImGui::CalcTextSize(label).x;
    ImGui::SetCursorPosX(leftInset - labelW - S(8.f));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(0.f, S(8.f));
    ImGui::PushItemWidth(sliderW);
    if (ImGui::SliderInt((std::string("##") + label).c_str(), val, vmin, vmax))
        g_Settings()->Set(key, *val);
    ImGui::PopItemWidth();
};

const auto drawCombo = [&](const char* label, int* val, const char* const* items, int itemCount, const char* key)
{
    const float labelW = ImGui::CalcTextSize(label).x;
    ImGui::SetCursorPosX(leftInset - labelW - S(8.f));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(0.f, S(8.f));
    ImGui::PushItemWidth(sliderW);
    if (ImGui::Combo((std::string("##") + label).c_str(), val, items, itemCount))
        g_Settings()->Set(key, *val);
    ImGui::PopItemWidth();
};

const auto drawCheck = [&](const char* label, bool* val, const char* key)
{
    ImGui::SetCursorPosX(leftInset);
    if (StyledCheckbox(label, val))
        g_Settings()->Set(key, *val);
};

const auto tip = [](const char* text) {
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", text);
};

// Checkbox to the left of a right-aligned label + slider (for FPS weight rows).
// checkTip shown on checkbox hover, sliderTip shown on slider hover (works even when disabled).
const auto drawWeightSlider = [&](const char* label, bool* use, float* val,
                                   const char* useKey, const char* valKey,
                                   const char* checkTip, const char* sliderTip)
{
    const float labelW  = ImGui::CalcTextSize(label).x;
    const float checkSz = ImGui::GetFrameHeight();
    ImGui::SetCursorPosX(leftInset - labelW - S(8.f) - checkSz - S(4.f));
    ImGui::AlignTextToFramePadding();
    if (StyledCheckbox((std::string("##w_") + label).c_str(), use))
        g_Settings()->Set(useKey, *use);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", checkTip);
    ImGui::SameLine(0.f, S(4.f));
    ImGui::BeginDisabled(!(*use));
    ImGui::SetCursorPosX(leftInset - labelW - S(8.f));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(0.f, S(8.f));
    ImGui::PushItemWidth(sliderW);
    if (ImGui::SliderFloat((std::string("##") + label).c_str(), val, 0.0f, 1.0f, "%.2f"))
        g_Settings()->Set(valKey, *val);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", sliderTip);
    ImGui::PopItemWidth();
    ImGui::EndDisabled();
};

ImGui::SetCursorPosY(ImGui::GetCursorPosY() + topPad);
ImGui::SetCursorPosX(leftInset);
if (StyledCheckbox("Enable audio reactive", &g_audioReactive))
    g_Settings()->Set("settings.player.audio_reactive", g_audioReactive);

ImGui::SetCursorPosY(ImGui::GetCursorPosY() + rowGap);
ImGui::Separator();

// ── Analyser ──────────────────────────────────────────────────────────────────
ImGui::SetCursorPosY(ImGui::GetCursorPosY() + rowGap);
ImGui::SetCursorPosX(S(8.f));
ImGui::TextUnformatted("Analyser");
ImGui::SetCursorPosY(ImGui::GetCursorPosY() + rowGap);

drawSlider("Bass mult",       &g_audioBassMult,      0.5f,  3.0f,    "settings.player.audio_bass_mult");
tip("Multiplier on normalised bass energy (30-200 Hz).");
drawSlider("Mid mult",        &g_audioMidMult,       0.5f,  3.0f,    "settings.player.audio_mid_mult");
tip("Multiplier on normalised mid energy (200-3500 Hz).");
drawSlider("High mult",       &g_audioHighMult,      0.5f,  3.0f,    "settings.player.audio_high_mult");
tip("Multiplier on normalised high energy (3.5-20 kHz).");
drawSlider("Peak decay",      &g_audioPeakDecay,     0.95f, 0.99999f,"settings.player.audio_peak_decay", "%.5f");
tip("How slowly per-band peak normalisation decays. Higher = longer memory, slower adaptation to volume changes.");
drawSlider("Dark brightness", &g_audioDarkBrightness,-1.0f, 0.0f,    "settings.player.audio_dark_brightness");
tip("Brightness floor applied when no audio signal is present.");

// ── FPS ───────────────────────────────────────────────────────────────────────
ImGui::SetCursorPosY(ImGui::GetCursorPosY() + rowGap);
ImGui::Separator();
ImGui::SetCursorPosY(ImGui::GetCursorPosY() + rowGap);
ImGui::SetCursorPosX(S(8.f));
ImGui::TextUnformatted("FPS");
ImGui::SetCursorPosY(ImGui::GetCursorPosY() + rowGap);

drawCheck("Enable FPS control", &g_audioFpsEnabled, "settings.player.audio_fps_enabled");
tip("Vary playback frame rate with audio intensity.");
ImGui::SetCursorPosY(ImGui::GetCursorPosY() + rowGap);
drawSlider("Min FPS",    &g_audioFpsMin,  1.0f, 120.0f, "settings.player.audio_fps_min");
tip("Frame rate when audio is quiet or absent.");
drawSlider("Max FPS",    &g_audioFpsMax,  1.0f, 120.0f, "settings.player.audio_fps_max");
tip("Frame rate at peak audio intensity.");
drawWeightSlider("W centroid", &g_audioFpsUseCentroid, &g_audioFpsWeightCentroid,
    "settings.player.audio_fps_use_centroid", "settings.player.audio_fps_weight_centroid",
    "Tick to include spectral centroid in the FPS calculation.\n"
    "Centroid is the weighted average frequency of the spectrum -a measure of perceptual brightness.\n"
    "Unlike fixed bands, it shifts continuously as the tonal balance changes.",
    "Relative influence of spectral centroid vs other active bands.\n"
    "Centroid rises as music gets brighter (more treble) and falls for bass-heavy passages.\n"
    "Good for tracking tonal shifts rather than raw energy level.");
drawWeightSlider("W mid", &g_audioFpsUseMid, &g_audioFpsWeightMid,
    "settings.player.audio_fps_use_mid", "settings.player.audio_fps_weight_mid",
    "Tick to include mid-range energy (200-3500 Hz) in the FPS calculation.\n"
    "Covers vocals, guitars, synths, and most melodic content.",
    "Relative influence of mid-range energy (200-3500 Hz) vs other active bands.\n"
    "Mid is the most presence-heavy band -high during busy sections, lower during breakdowns.\n"
    "Differs from centroid in that it measures raw energy in a fixed range, not tonal balance.");
drawWeightSlider("W high", &g_audioFpsUseHigh, &g_audioFpsWeightHigh,
    "settings.player.audio_fps_use_high", "settings.player.audio_fps_weight_high",
    "Tick to include high-frequency energy (3.5-20 kHz) in the FPS calculation.\n"
    "Covers cymbals, hi-hats, sibilance, and air.",
    "Relative influence of high-frequency energy (3.5-20 kHz) vs other active bands.\n"
    "High tracks cymbal work and brightness -useful for driving FPS with rhythmic hi-hat patterns.\n"
    "Quieter than mid or bass in most music, so weight often needs to be higher to compensate.");
drawWeightSlider("W bass", &g_audioFpsUseBass, &g_audioFpsWeightBass,
    "settings.player.audio_fps_use_bass", "settings.player.audio_fps_weight_bass",
    "Tick to include bass energy (30-200 Hz) in the FPS calculation.\n"
    "Covers kick drums, bass guitar, and sub-bass.",
    "Relative influence of bass energy (30-200 Hz) vs other active bands.\n"
    "Bass is typically the most stable band -provides a steady FPS baseline.\n"
    "Less reactive to melodic dynamics than centroid or mid.");
drawSlider("FPS attack",  &g_audioFpsAttack,  0.01f, 1.0f, "settings.player.audio_fps_attack");
tip("How quickly FPS rises toward the target (0 = never, 1 = instant).");
drawSlider("FPS release", &g_audioFpsRelease, 0.01f, 1.0f, "settings.player.audio_fps_release");
tip("How quickly FPS falls back toward minimum when audio quietens.");

// ── Cuts ──────────────────────────────────────────────────────────────────────
ImGui::SetCursorPosY(ImGui::GetCursorPosY() + rowGap);
ImGui::Separator();
ImGui::SetCursorPosY(ImGui::GetCursorPosY() + rowGap);
ImGui::SetCursorPosX(S(8.f));
ImGui::TextUnformatted("Cuts");
ImGui::SetCursorPosY(ImGui::GetCursorPosY() + rowGap);

drawSlider("Global cooldown", &g_audioCutGlobalCooldown, 0.1f, 10.0f, "settings.player.audio_cut_global_cooldown");
tip("Minimum seconds between any two audio-triggered cuts.");
ImGui::SetCursorPosY(ImGui::GetCursorPosY() + rowGap);

drawCheck("Transient trigger",    &g_audioCutTransientEnabled,   "settings.player.audio_cut_transient_enabled");
tip("Trigger a cut on broadband transients (sudden energy across all bands).");
drawSlider("Transient threshold", &g_audioCutTransientThreshold, 0.1f, 1.0f, "settings.player.audio_cut_transient_threshold");
tip("Smoothed transient level that triggers the cut.");
drawCombo("Transient style",      &g_audioCutTransientStyle,     kCutStyles, 3, "settings.player.audio_cut_transient_style");
tip("Transition style applied when a transient cut fires.");
ImGui::SetCursorPosY(ImGui::GetCursorPosY() + rowGap);

drawCheck("Kick trigger",   &g_audioCutKickEnabled,   "settings.player.audio_cut_kick_enabled");
tip("Trigger a cut on bass kicks (sharp bass energy rise).");
drawSlider("Kick threshold",&g_audioCutKickThreshold, 0.1f, 1.0f, "settings.player.audio_cut_kick_threshold");
tip("Smoothed kick level that triggers the cut.");
drawCombo("Kick style",     &g_audioCutKickStyle,     kCutStyles, 3, "settings.player.audio_cut_kick_style");
tip("Transition style applied when a kick cut fires.");
ImGui::SetCursorPosY(ImGui::GetCursorPosY() + rowGap);

drawCheck("Snare trigger",   &g_audioCutSnareEnabled,   "settings.player.audio_cut_snare_enabled");
tip("Trigger a cut on snare hits (mid-dominant transient -requires more mid than bass rise to filter out kicks).");
drawSlider("Snare threshold",&g_audioCutSnareThreshold, 0.1f, 1.0f, "settings.player.audio_cut_snare_threshold");
tip("Smoothed snare level that triggers the cut.");
drawCombo("Snare style",     &g_audioCutSnareStyle,     kCutStyles, 3, "settings.player.audio_cut_snare_style");
tip("Transition style applied when a snare cut fires.");
ImGui::SetCursorPosY(ImGui::GetCursorPosY() + rowGap);

drawCheck("Beat trigger", &g_audioCutBeatEnabled, "settings.player.audio_cut_beat_enabled");
tip("Trigger a cut every N beats as detected by the tempo tracker.");
drawSliderInt("Beat N",   &g_audioCutBeatN, 1, 8, "settings.player.audio_cut_beat_n");
tip("Number of beats between beat-triggered cuts.");
drawCombo("Beat style",   &g_audioCutBeatStyle, kCutStyles, 3, "settings.player.audio_cut_beat_style");
tip("Transition style applied when a beat cut fires.");
ImGui::SetCursorPosY(ImGui::GetCursorPosY() + rowGap);

drawCheck("Volume trigger",    &g_audioCutVolumeEnabled,   "settings.player.audio_cut_volume_enabled");
tip("Trigger a cut when overall volume crosses a threshold.");
drawSlider("Volume threshold", &g_audioCutVolumeThreshold, 0.1f, 1.0f, "settings.player.audio_cut_volume_threshold");
tip("Volume level that triggers the cut.");
drawCombo("Volume style",      &g_audioCutVolumeStyle,     kCutStyles, 3, "settings.player.audio_cut_volume_style");
tip("Transition style applied when a volume cut fires.");

// ── Mixing ────────────────────────────────────────────────────────────────────
ImGui::SetCursorPosY(ImGui::GetCursorPosY() + rowGap);
ImGui::Separator();
ImGui::SetCursorPosY(ImGui::GetCursorPosY() + rowGap);
ImGui::SetCursorPosX(S(8.f));
ImGui::TextUnformatted("Mixing");
ImGui::SetCursorPosY(ImGui::GetCursorPosY() + rowGap);

drawCheck("Enable mixing", &g_audioMixEnabled, "settings.player.audio_mix_enabled");
tip("Control the blend between two dream sequences using an audio signal.");
ImGui::SetCursorPosY(ImGui::GetCursorPosY() + rowGap);
drawCombo("Mix source", &g_audioMixSource, kMixSources, 8, "settings.player.audio_mix_source");
tip("Audio signal that drives the mix position.");
drawSlider("Mix min",   &g_audioMixMin,    0.0f,  1.0f,  "settings.player.audio_mix_min");
tip("Blend position when the audio source is at zero.");
drawSlider("Mix max",   &g_audioMixMax,    0.0f,  1.0f,  "settings.player.audio_mix_max");
tip("Blend position when the audio source is at peak.");
drawSlider("Smooth",    &g_audioMixSmooth, 0.01f, 1.0f,  "settings.player.audio_mix_smooth");
tip("IIR smoothing applied to the mix position. Lower = smoother but slower to react.");
}
