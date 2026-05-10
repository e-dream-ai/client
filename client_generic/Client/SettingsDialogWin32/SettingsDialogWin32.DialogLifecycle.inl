static void ApplyDialogPauseState(bool visible)
{
    if (visible)
    {
        const bool wasPaused = g_Player().IsPaused();
        const bool wasUserPaused = g_Player().IsUserPaused();
        g_wasPausedBeforeDialog.store(wasPaused, std::memory_order_release);
        g_wasUserPausedBeforeDialog.store(wasUserPaused,
                                          std::memory_order_release);
        // Don't pause - keep playback running so audio reactive visuals continue
        g_pausedBySettingsDialog.store(true, std::memory_order_release);
        return;
    }

    if (g_pausedBySettingsDialog.exchange(false, std::memory_order_acq_rel))
    {
        const bool restorePaused = g_wasPausedBeforeDialog.load(std::memory_order_acquire);
        const bool restoreUserPaused = g_wasUserPausedBeforeDialog.load(std::memory_order_acquire);
        if (restorePaused && !restoreUserPaused)
        {
            // Was paused only by buffering (not by user). If buffering has already completed while
            // the dialog was open, the buffering-complete event won't fire again, so just unpause.
            // If buffering is still active, restore the system-only pause so buffering-complete can
            // resume normally (two-step needed because dialog set m_UserPaused=true).
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
    {
        SaveSettings();
        // Defer 16:9 snap to the next message-pump iteration. Calling SnapWindowTo16By9IfNeeded
        // synchronously here would SetWindowPos -> WM_SIZE -> ResizeSwapChain inside the ImGui
        // click handler, invalidating RendererDX11's RTV while RenderIfNeeded still holds a
        // raw pointer to it (crash on the trailing OMSetRenderTargets). The message handler in
        // CDisplayDX11::WndProc runs the snap after the current frame returns.
        if (auto* dx = TryGetDx11Display())
        {
            if (HWND hwnd = dx->GetWindowHandle())
                PostMessageW(hwnd, kSettingsDialogWin32SnapAfterCloseMsg, 0, 0);
        }
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
