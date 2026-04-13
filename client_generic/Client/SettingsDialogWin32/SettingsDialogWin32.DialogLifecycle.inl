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
    {
        SaveSettings();
        SnapWindowTo16By9IfNeeded();
    }
    ApplyDialogPauseState(false);
    g_visible.store(false, std::memory_order_release);
    g_pendingImGuiShutdown.store(true, std::memory_order_release);
}

/// Used when another Win32 ImGui overlay (e.g. About) needs exclusive use of ImGui_ImplWin32 on the main HWND.
static void DismissSettingsWithoutSaveImpl()
{
    g_showRequested.store(false, std::memory_order_release);
    if (g_visible.load(std::memory_order_acquire))
    {
        ApplyDialogPauseState(false);
        g_visible.store(false, std::memory_order_release);
    }
    if (g_imguiInitialized.load(std::memory_order_acquire))
        ShutdownImGui();
}
