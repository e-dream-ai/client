// Open/close + playback-pause lifecycle — mirrors SettingsDialogWin32.DialogLifecycle.inl.

static void ApplyDialogPauseState(bool visible)
{
    if (visible)
    {
        const bool wasPaused = g_Player().IsPaused();
        const bool wasUserPaused = g_Player().IsUserPaused();
        g_wasPausedBeforeDialog.store(wasPaused, std::memory_order_release);
        g_wasUserPausedBeforeDialog.store(wasUserPaused, std::memory_order_release);
        g_Player().SetPaused(true, /*isUserInitiated=*/true);
        g_pausedBySettingsDialog.store(true, std::memory_order_release);
        return;
    }

    if (g_pausedBySettingsDialog.exchange(false, std::memory_order_acq_rel))
    {
        const bool restorePaused = g_wasPausedBeforeDialog.load(std::memory_order_acquire);
        const bool restoreUserPaused = g_wasUserPausedBeforeDialog.load(std::memory_order_acquire);
        if (restorePaused && !restoreUserPaused)
        {
            if (g_Player().IsPausedForBuffering())
            {
                g_Player().SetPaused(false, false);
                g_Player().SetPaused(true, false);
            }
            else
            {
                g_Player().SetPaused(false, false);
            }
        }
        else
        {
            g_Player().SetPaused(restorePaused, restoreUserPaused);
        }
    }
}

static void CloseDialog(bool saveBeforeClose)
{
    if (saveBeforeClose)
        SaveSettings();
    ApplyDialogPauseState(false);
    g_visible.store(false, std::memory_order_release);
}
