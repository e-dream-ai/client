static void ApplyDialogPauseState(bool visible)
{
    if (visible)
    {
        const bool wasPaused = g_Player().IsPaused();
        g_wasPausedBeforeDialog.store(wasPaused, std::memory_order_release);
        if (!wasPaused)
        {
            g_Player().SetPaused(true);
            g_pausedBySettingsDialog.store(true, std::memory_order_release);
        }
        else
        {
            g_pausedBySettingsDialog.store(false, std::memory_order_release);
        }
        return;
    }

    if (g_pausedBySettingsDialog.exchange(false, std::memory_order_acq_rel))
        g_Player().SetPaused(g_wasPausedBeforeDialog.load(std::memory_order_acquire));
}

static void CloseDialog(bool saveBeforeClose)
{
    if (saveBeforeClose)
        SaveSettings();
    ApplyDialogPauseState(false);
    g_visible.store(false, std::memory_order_release);
    g_pendingImGuiShutdown.store(true, std::memory_order_release);
}
