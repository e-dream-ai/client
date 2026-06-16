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
    g_rotatePortrait = g_Settings()->Get("settings.player.rotate_portrait", false);
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
    g_Settings()->Set("settings.player.rotate_portrait", g_rotatePortrait);
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
