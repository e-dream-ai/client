#ifndef THEMEWIN32_H
#define THEMEWIN32_H

#include <atomic>
#include <windows.h>

// The system light/dark choice (Settings > Personalization > Colors > app mode). The cached value
// is read every frame from the render thread; the window thread refreshes it on WM_SETTINGCHANGE.
namespace ThemeWin32 {

inline bool ReadSystemIsDark()
{
    DWORD useLight = 1;
    DWORD size = sizeof(useLight);
    // Absent on Windows versions without a dark app mode: those are light.
    const LSTATUS rc = RegGetValueW(HKEY_CURRENT_USER,
                                    L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                                    L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &useLight, &size);
    return rc == ERROR_SUCCESS && useLight == 0;
}

inline std::atomic<bool> g_isDark{ReadSystemIsDark()};

inline bool IsDark()
{
    return g_isDark.load(std::memory_order_relaxed);
}

inline void Refresh()
{
    g_isDark.store(ReadSystemIsDark(), std::memory_order_relaxed);
}

} // namespace ThemeWin32

#endif // THEMEWIN32_H
