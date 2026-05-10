static void CopySettingToBuf(const char* key, std::string_view fallback, char* outBuf, size_t outSize)
{
    const std::string value = g_Settings()->Get(key, std::string(fallback));
    std::strncpy(outBuf, value.c_str(), outSize - 1);
    outBuf[outSize - 1] = '\0';
}

static void LoadSettingsForShow()
{
    CopySettingToBuf("settings.generator.nickname", std::string(), g_nicknameBuf, sizeof g_nicknameBuf);
    g_codeBuf[0] = '\0';
    g_sentCode = false;
    g_previousLoginEmailBuf[0] = '\0';
    g_hasPreviousLoginEmail = false;
    g_versionText = PlatformUtils::GetAppVersion() + " " +
                    PlatformUtils::GetGitRevision() + " " +
                    PlatformUtils::GetBuildDate();

    g_playerFps = g_Settings()->Get("settings.player.player_fps", 23.0);
    g_displayFps = g_Settings()->Get("settings.player.display_fps", 60.0);
    g_vsync = g_Settings()->Get("settings.player.vbl_sync", false);
    g_preserveAR = g_Settings()->Get("settings.player.preserve_AR", false);
    g_blackoutMonitors = g_Settings()->Get("settings.player.blackout_monitors", true);
    g_quietMode = g_Settings()->Get("settings.player.quiet_mode", true);
    g_showAttribution = g_Settings()->Get("settings.app.attributionpng", false);

    CopySettingToBuf("settings.content.sheepdir", std::string(), g_contentDirBuf, sizeof g_contentDirBuf);
    g_unlimitedCache = g_Settings()->Get("settings.content.unlimited_cache", false);
    g_cacheSizeGb = g_Settings()->Get("settings.content.cache_size", 10);
    if (g_cacheSizeGb <= 0)
    {
        g_cacheSizeGb = 10;
        g_unlimitedCache = true;
    }

    g_useProxy = g_Settings()->Get("settings.content.use_proxy", false);
    CopySettingToBuf("settings.content.proxy", std::string(), g_proxyHostBuf, sizeof g_proxyHostBuf);
    CopySettingToBuf("settings.content.proxy_username", std::string(), g_proxyLoginBuf, sizeof g_proxyLoginBuf);
    CopySettingToBuf("settings.content.proxy_password", std::string(), g_proxyPasswordBuf, sizeof g_proxyPasswordBuf);
    g_debugLog = g_Settings()->Get("settings.app.log", false);
    CopySettingToBuf("settings.content.server", ServerConfig::DEFAULT_DREAM_SERVER, g_serverBuf, sizeof g_serverBuf);

    g_keepScreensaverEnabled = g_Settings()->Get("settings.app.keep_screensaver_enabled", true);

    g_audioReactive        = g_Settings()->Get("settings.player.audio_reactive",        true);
    g_audioBassMult        = g_Settings()->Get("settings.player.audio_bass_mult",        0.7f);
    g_audioMidMult         = g_Settings()->Get("settings.player.audio_mid_mult",         0.8f);
    g_audioHighMult        = g_Settings()->Get("settings.player.audio_high_mult",        0.7f);
    g_audioPeakDecay       = g_Settings()->Get("settings.player.audio_peak_decay",       0.999f);
    g_audioPeakFloor       = g_Settings()->Get("settings.player.audio_peak_floor",       0.01f);
    g_audioDarkBrightness  = g_Settings()->Get("settings.player.audio_dark_brightness",  -0.5f);
    g_audioOnsetThreshold  = g_Settings()->Get("settings.player.audio_onset_threshold",  0.001f);

    g_audioFpsEnabled        = g_Settings()->Get("settings.player.audio_fps_enabled",         true);
    g_audioFpsWeightCentroid = g_Settings()->Get("settings.player.audio_fps_weight_centroid",  0.8f);
    g_audioFpsWeightMid      = g_Settings()->Get("settings.player.audio_fps_weight_mid",       0.5f);
    g_audioFpsWeightHigh     = g_Settings()->Get("settings.player.audio_fps_weight_high",      0.3f);
    g_audioFpsWeightBass     = g_Settings()->Get("settings.player.audio_fps_weight_bass",      0.1f);
    g_audioFpsMin            = g_Settings()->Get("settings.player.audio_fps_min",              4.0f);
    g_audioFpsMax            = g_Settings()->Get("settings.player.audio_fps_max",              60.0f);
    g_audioFpsAttack         = g_Settings()->Get("settings.player.audio_fps_attack",           0.3f);
    g_audioFpsRelease        = g_Settings()->Get("settings.player.audio_fps_release",          0.1f);
    g_audioFpsUseCentroid    = g_Settings()->Get("settings.player.audio_fps_use_centroid",     true);
    g_audioFpsUseMid         = g_Settings()->Get("settings.player.audio_fps_use_mid",         true);
    g_audioFpsUseHigh        = g_Settings()->Get("settings.player.audio_fps_use_high",        true);
    g_audioFpsUseBass        = g_Settings()->Get("settings.player.audio_fps_use_bass",        true);

    g_audioCutTransientEnabled   = g_Settings()->Get("settings.player.audio_cut_transient_enabled",   false);
    g_audioCutTransientThreshold = g_Settings()->Get("settings.player.audio_cut_transient_threshold", 0.5f);
    g_audioCutTransientStyle     = g_Settings()->Get("settings.player.audio_cut_transient_style",     0);
    g_audioCutKickEnabled        = g_Settings()->Get("settings.player.audio_cut_kick_enabled",        false);
    g_audioCutKickThreshold      = g_Settings()->Get("settings.player.audio_cut_kick_threshold",      0.5f);
    g_audioCutKickStyle          = g_Settings()->Get("settings.player.audio_cut_kick_style",          0);
    g_audioCutSnareEnabled       = g_Settings()->Get("settings.player.audio_cut_snare_enabled",       false);
    g_audioCutSnareThreshold     = g_Settings()->Get("settings.player.audio_cut_snare_threshold",     0.5f);
    g_audioCutSnareStyle         = g_Settings()->Get("settings.player.audio_cut_snare_style",         1);
    g_audioCutBeatEnabled        = g_Settings()->Get("settings.player.audio_cut_beat_enabled",        false);
    g_audioCutBeatN          = g_Settings()->Get("settings.player.audio_cut_beat_n",          4);
    g_audioCutBeatStyle      = g_Settings()->Get("settings.player.audio_cut_beat_style",      1);
    g_audioCutVolumeEnabled  = g_Settings()->Get("settings.player.audio_cut_volume_enabled",  false);
    g_audioCutVolumeThreshold= g_Settings()->Get("settings.player.audio_cut_volume_threshold",0.8f);
    g_audioCutVolumeStyle    = g_Settings()->Get("settings.player.audio_cut_volume_style",    2);
    g_audioCutGlobalCooldown = g_Settings()->Get("settings.player.audio_cut_global_cooldown", 2.0f);

    g_audioMixEnabled        = g_Settings()->Get("settings.player.audio_mix_enabled",         false);
    g_audioMixSource         = g_Settings()->Get("settings.player.audio_mix_source",          0);
    g_audioMixMin            = g_Settings()->Get("settings.player.audio_mix_min",             0.0f);
    g_audioMixMax            = g_Settings()->Get("settings.player.audio_mix_max",             1.0f);
    g_audioMixSmooth         = g_Settings()->Get("settings.player.audio_mix_smooth",          0.15f);

    g_statusBuf[0] = '\0';
}

static void SaveSettings()
{
    if (g_playerFps < 0.1)
        g_playerFps = 20.0;
    if (g_displayFps < 0.1)
        g_displayFps = 60.0;
    if (g_cacheSizeGb < 1)
        g_cacheSizeGb = 1;

    g_Settings()->Set("settings.player.player_fps", g_playerFps);
    g_Settings()->Set("settings.player.display_fps", g_displayFps);
    g_Settings()->Set("settings.player.DisplayMode", 2);
    g_Settings()->Set("settings.player.vbl_sync", g_vsync);
    g_Settings()->Set("settings.player.preserve_AR", g_preserveAR);
    g_Settings()->Set("settings.player.blackout_monitors", g_blackoutMonitors);
    g_Settings()->Set("settings.player.quiet_mode", g_quietMode);
    g_Settings()->Set("settings.app.attributionpng", g_showAttribution);

    g_Settings()->Set("settings.content.sheepdir", std::string(g_contentDirBuf));
    g_Settings()->Set("settings.content.unlimited_cache", g_unlimitedCache);
    g_Settings()->Set("settings.content.cache_size", g_cacheSizeGb);

    g_Settings()->Set("settings.content.use_proxy", g_useProxy);
    g_Settings()->Set("settings.content.proxy", std::string(g_proxyHostBuf));
    g_Settings()->Set("settings.content.proxy_username", std::string(g_proxyLoginBuf));
    g_Settings()->Set("settings.content.proxy_password", std::string(g_proxyPasswordBuf));
    g_Settings()->Set("settings.app.log", g_debugLog);
    g_Settings()->Set("settings.content.server", std::string(g_serverBuf));

    g_Settings()->Set("settings.app.keep_screensaver_enabled", g_keepScreensaverEnabled);
    g_Settings()->Set("settings.generator.nickname", std::string(g_nicknameBuf));

    g_Settings()->Set("settings.player.audio_reactive",        g_audioReactive);
    g_Settings()->Set("settings.player.audio_bass_mult",       g_audioBassMult);
    g_Settings()->Set("settings.player.audio_mid_mult",        g_audioMidMult);
    g_Settings()->Set("settings.player.audio_high_mult",       g_audioHighMult);
    g_Settings()->Set("settings.player.audio_peak_decay",      g_audioPeakDecay);
    g_Settings()->Set("settings.player.audio_peak_floor",      g_audioPeakFloor);
    g_Settings()->Set("settings.player.audio_dark_brightness", g_audioDarkBrightness);
    g_Settings()->Set("settings.player.audio_onset_threshold", g_audioOnsetThreshold);

    g_Settings()->Set("settings.player.audio_fps_enabled",          g_audioFpsEnabled);
    g_Settings()->Set("settings.player.audio_fps_weight_centroid",   g_audioFpsWeightCentroid);
    g_Settings()->Set("settings.player.audio_fps_weight_mid",        g_audioFpsWeightMid);
    g_Settings()->Set("settings.player.audio_fps_weight_high",       g_audioFpsWeightHigh);
    g_Settings()->Set("settings.player.audio_fps_weight_bass",       g_audioFpsWeightBass);
    g_Settings()->Set("settings.player.audio_fps_min",               g_audioFpsMin);
    g_Settings()->Set("settings.player.audio_fps_max",               g_audioFpsMax);
    g_Settings()->Set("settings.player.audio_fps_attack",            g_audioFpsAttack);
    g_Settings()->Set("settings.player.audio_fps_release",           g_audioFpsRelease);
    g_Settings()->Set("settings.player.audio_fps_use_centroid",      g_audioFpsUseCentroid);
    g_Settings()->Set("settings.player.audio_fps_use_mid",           g_audioFpsUseMid);
    g_Settings()->Set("settings.player.audio_fps_use_high",          g_audioFpsUseHigh);
    g_Settings()->Set("settings.player.audio_fps_use_bass",          g_audioFpsUseBass);

    g_Settings()->Set("settings.player.audio_cut_transient_enabled",   g_audioCutTransientEnabled);
    g_Settings()->Set("settings.player.audio_cut_transient_threshold",g_audioCutTransientThreshold);
    g_Settings()->Set("settings.player.audio_cut_transient_style",    g_audioCutTransientStyle);
    g_Settings()->Set("settings.player.audio_cut_kick_enabled",       g_audioCutKickEnabled);
    g_Settings()->Set("settings.player.audio_cut_kick_threshold",     g_audioCutKickThreshold);
    g_Settings()->Set("settings.player.audio_cut_kick_style",         g_audioCutKickStyle);
    g_Settings()->Set("settings.player.audio_cut_snare_enabled",      g_audioCutSnareEnabled);
    g_Settings()->Set("settings.player.audio_cut_snare_threshold",    g_audioCutSnareThreshold);
    g_Settings()->Set("settings.player.audio_cut_snare_style",        g_audioCutSnareStyle);
    g_Settings()->Set("settings.player.audio_cut_beat_enabled",       g_audioCutBeatEnabled);
    g_Settings()->Set("settings.player.audio_cut_beat_n",           g_audioCutBeatN);
    g_Settings()->Set("settings.player.audio_cut_beat_style",       g_audioCutBeatStyle);
    g_Settings()->Set("settings.player.audio_cut_volume_enabled",   g_audioCutVolumeEnabled);
    g_Settings()->Set("settings.player.audio_cut_volume_threshold", g_audioCutVolumeThreshold);
    g_Settings()->Set("settings.player.audio_cut_volume_style",     g_audioCutVolumeStyle);
    g_Settings()->Set("settings.player.audio_cut_global_cooldown",  g_audioCutGlobalCooldown);

    g_Settings()->Set("settings.player.audio_mix_enabled",  g_audioMixEnabled);
    g_Settings()->Set("settings.player.audio_mix_source",   g_audioMixSource);
    g_Settings()->Set("settings.player.audio_mix_min",      g_audioMixMin);
    g_Settings()->Set("settings.player.audio_mix_max",      g_audioMixMax);
    g_Settings()->Set("settings.player.audio_mix_smooth",   g_audioMixSmooth);

    g_Settings()->Storage()->Commit();

    if (!g_unlimitedCache)
    {
        const std::uintmax_t cacheSize = static_cast<std::uintmax_t>(g_cacheSizeGb) * 1024ull * 1024ull * 1024ull;
        Cache::CacheManager::getInstance().resizeCache(cacheSize);
    }
}

static void ResetFormForShow()
{
    LoadSettingsForShow();
}
