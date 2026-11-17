#ifdef WIN32

#include "ScreensaverInstallerWin32.h"

#include <windows.h>
#include <shlwapi.h>

#include <string>

#include "Log.h"
#include "Settings.h"

#pragma comment(lib, "shlwapi.lib")

namespace
{
std::string ReadRegString(HKEY hKey, const char* valueName)
{
    char buf[MAX_PATH] = {};
    DWORD type = 0;
    DWORD size = sizeof(buf) - 1;
    const LSTATUS rc = RegQueryValueExA(hKey, valueName, nullptr, &type,
                                        reinterpret_cast<LPBYTE>(buf), &size);
    if (rc != ERROR_SUCCESS || type != REG_SZ)
        return {};
    return std::string(buf);
}

bool WriteRegString(HKEY hKey, const char* valueName, const std::string& value)
{
    const LSTATUS rc = RegSetValueExA(
        hKey, valueName, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(value.c_str()),
        static_cast<DWORD>(value.size() + 1));
    return rc == ERROR_SUCCESS;
}

// The long form of an existing path, or the path unchanged. Screen Saver Settings
// stores SCRNSAVE.EXE in 8.3 form (C:\PROGRA~1\INFINI~1\INFINI~1.SCR) when the user
// clicks Apply; that is still our .scr.
std::string LongPath(const std::string& path)
{
    char buf[MAX_PATH] = {};
    const DWORD n = GetLongPathNameA(path.c_str(), buf, MAX_PATH);
    return (n > 0 && n < MAX_PATH) ? std::string(buf, n) : path;
}
} // namespace

void ScreensaverInstallerWin32::EnsureScreensaverActive(const std::string& workingDir)
{
    if (!g_Settings()->Get("settings.app.keep_screensaver_enabled", true))
    {
        if (g_Log) g_Log->Info("EnsureScreensaverActive: opt-out, skipping");
        return;
    }

    std::string scrPath = workingDir;
    if (!scrPath.empty() && scrPath.back() != '\\' && scrPath.back() != '/')
        scrPath += '\\';
    scrPath += "infinidream.scr";

    if (!PathFileExistsA(scrPath.c_str()))
    {
        if (g_Log) g_Log->Warning("EnsureScreensaverActive: %s missing, skipping",
                                  scrPath.c_str());
        return;
    }

    HKEY hKey = nullptr;
    const LSTATUS rc = RegOpenKeyExA(HKEY_CURRENT_USER, "Control Panel\\Desktop", 0,
                                     KEY_READ | KEY_WRITE, &hKey);
    if (rc != ERROR_SUCCESS)
    {
        if (g_Log) g_Log->Warning("EnsureScreensaverActive: cannot open Desktop key (%ld)",
                                  static_cast<long>(rc));
        return;
    }

    const std::string currentScr = ReadRegString(hKey, "SCRNSAVE.EXE");
    const std::string currentActive = ReadRegString(hKey, "ScreenSaveActive");

    bool changed = false;
    if (_stricmp(LongPath(currentScr).c_str(), LongPath(scrPath).c_str()) != 0)
    {
        if (WriteRegString(hKey, "SCRNSAVE.EXE", scrPath))
        {
            changed = true;
            if (g_Log) g_Log->Info("EnsureScreensaverActive: SCRNSAVE.EXE '%s' -> '%s'",
                                   currentScr.c_str(), scrPath.c_str());
        }
    }

    if (currentActive != "1")
    {
        if (WriteRegString(hKey, "ScreenSaveActive", "1"))
        {
            changed = true;
            if (g_Log) g_Log->Info("EnsureScreensaverActive: ScreenSaveActive '%s' -> '1'",
                                   currentActive.c_str());
        }
    }

    RegCloseKey(hKey);

    if (changed)
    {
        // Nudge Explorer / the screen-saver subsystem to re-read the registry
        // without waiting for a logon. SMTO_ABORTIFHUNG keeps us from blocking
        // on a stuck top-level window.
        SendMessageTimeoutA(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                            reinterpret_cast<LPARAM>("Windows"),
                            SMTO_ABORTIFHUNG, 1000, nullptr);
    }
}

#endif // WIN32
