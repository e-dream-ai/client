#ifdef WIN32

#include "ScreensaverInstallerWin32.h"

#include <windows.h>
#include <shlwapi.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "Log.h"
#include "Settings.h"

#pragma comment(lib, "shlwapi.lib")

namespace
{
constexpr const char* kDesktopKey = "Control Panel\\Desktop";
constexpr const char* kBackupSettingKey = "settings.app.screensaver_backup";

struct ScreensaverBackup
{
    bool valid = false;
    std::string exe;
    std::string active;
    std::string timeout;
};

std::string ReadRegString(HKEY hKey, const char* valueName)
{
    char buf[MAX_PATH] = {};
    DWORD type = 0;
    DWORD size = sizeof(buf);

    const LSTATUS rc = RegQueryValueExA(hKey, valueName, nullptr, &type,
                                        reinterpret_cast<LPBYTE>(buf), &size);

    if (rc != ERROR_SUCCESS)
        return {};

    if (type != REG_SZ && type != REG_EXPAND_SZ)
        return {};

    return std::string(buf);
}

bool WriteRegString(HKEY hKey, const char* valueName, const std::string& value)
{
    const LSTATUS rc =
        RegSetValueExA(hKey, valueName, 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(value.c_str()),
                       static_cast<DWORD>(value.size() + 1));
    return rc == ERROR_SUCCESS;
}

bool IsPositiveIntegerString(const std::string& value)
{
    if (value.empty())
        return false;

    for (char c : value)
    {
        if (c < '0' || c > '9')
            return false;
    }

    return std::atoi(value.c_str()) > 0;
}

std::string BuildScrPath(const std::string& workingDir)
{
    std::string scrPath = workingDir;
    if (!scrPath.empty() && scrPath.back() != '\\' && scrPath.back() != '/')
        scrPath += '\\';
    scrPath += "infinidream.scr";
    return scrPath;
}

std::string NormalizePathForCompare(std::string path)
{
    for (char& c : path)
    {
        if (c == '/')
            c = '\\';
    }

    return path;
}

void RefreshScreensaverSettings(bool active)
{
    SystemParametersInfoA(SPI_SETSCREENSAVEACTIVE, active ? TRUE : FALSE,
                          nullptr, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
}

std::vector<std::string> SplitLines(const std::string& value)
{
    std::vector<std::string> lines;
    std::string current;

    for (char c : value)
    {
        if (c == '\n')
        {
            lines.push_back(current);
            current.clear();
        }
        else if (c != '\r')
        {
            current.push_back(c);
        }
    }

    lines.push_back(current);
    return lines;
}

std::string SerializeBackup(const ScreensaverBackup& backup)
{
    return std::string(backup.valid ? "1" : "0") + "\n" + backup.exe + "\n" +
           backup.active + "\n" + backup.timeout;
}

ScreensaverBackup ReadBackupFromSettings()
{
    ScreensaverBackup backup;

    if (!g_Settings())
        return backup;

    const std::string raw = g_Settings()->Get(kBackupSettingKey, std::string());

    const std::vector<std::string> lines = SplitLines(raw);
    if (lines.size() < 4 || lines[0] != "1")
        return backup;

    backup.valid = true;
    backup.exe = lines[1];
    backup.active = lines[2];
    backup.timeout = lines[3];

    return backup;
}

void SaveBackupToSettings(const ScreensaverBackup& backup)
{
    if (!g_Settings())
        return;
    const std::string serialized = SerializeBackup(backup);

    if (g_Log)
        g_Log->Info("SaveBackupToSettings: writing '%s'", serialized.c_str());

    g_Settings()->Set(kBackupSettingKey, serialized);
    g_Settings()->Storage()->Commit();

    // Verify it survived the commit
    const std::string verify =
        g_Settings()->Get(kBackupSettingKey, std::string("MISSING"));
    if (g_Log)
        g_Log->Info("SaveBackupToSettings: verify read-back '%s'",
                    verify.c_str());
}
} // namespace

void ScreensaverInstallerWin32::SaveOriginalScreensaverSettingsOnce(
    const std::string& workingDir)
{
    HKEY hDesktop = nullptr;
    const LSTATUS desktopRc =
        RegOpenKeyExA(HKEY_CURRENT_USER, kDesktopKey, 0, KEY_READ, &hDesktop);

    if (desktopRc != ERROR_SUCCESS)
    {
        if (g_Log)
            g_Log->Warning("SaveOriginalScreensaverSettingsOnce: cannot open "
                           "Desktop key (%ld)",
                           static_cast<long>(desktopRc));
        return;
    }

    ScreensaverBackup backup;
    backup.valid = true;
    backup.exe = ReadRegString(hDesktop, "SCRNSAVE.EXE");
    backup.active = ReadRegString(hDesktop, "ScreenSaveActive");
    backup.timeout = ReadRegString(hDesktop, "ScreenSaveTimeOut");

    RegCloseKey(hDesktop);

    const std::string infinidreamScr = BuildScrPath(workingDir);

    if (_stricmp(NormalizePathForCompare(backup.exe).c_str(),
                 NormalizePathForCompare(infinidreamScr).c_str()) == 0)
    {
        if (g_Log)
            g_Log->Info(
                "SaveOriginalScreensaverSettingsOnce: current screensaver is "
                "already Infinidream, not overwriting backup");
        return;
    }

    SaveBackupToSettings(backup);

    if (g_Log)
        g_Log->Info("SaveOriginalScreensaverSettingsOnce: saved backup "
                    "exe='%s', active='%s', timeout='%s'",
                    backup.exe.c_str(), backup.active.c_str(),
                    backup.timeout.c_str());
}

bool ScreensaverInstallerWin32::RestoreOriginalScreensaverSettings()
{
    const ScreensaverBackup backup = ReadBackupFromSettings();

    if (!backup.valid)
    {
        if (g_Log)
            g_Log->Warning("RestoreOriginalScreensaverSettings: no valid "
                           "screensaver backup in settings");
        return false;
    }

    if (g_Log)
        g_Log->Info("RestoreOriginalScreensaverSettings: restoring exe='%s', "
                    "active='%s', timeout='%s'",
                    backup.exe.c_str(), backup.active.c_str(),
                    backup.timeout.c_str());

    HKEY hDesktop = nullptr;
    const LSTATUS desktopRc = RegOpenKeyExA(HKEY_CURRENT_USER, kDesktopKey, 0,
                                            KEY_READ | KEY_WRITE, &hDesktop);

    if (desktopRc != ERROR_SUCCESS)
    {
        if (g_Log)
            g_Log->Warning("RestoreOriginalScreensaverSettings: cannot open "
                           "Desktop key (%ld)",
                           static_cast<long>(desktopRc));
        return false;
    }

    const bool originalWasNone = backup.exe.empty();

    WriteRegString(hDesktop, "SCRNSAVE.EXE", originalWasNone ? "" : backup.exe);
    WriteRegString(hDesktop, "ScreenSaveActive",
                   originalWasNone ? "0" : backup.active);

    if (!backup.timeout.empty())
        WriteRegString(hDesktop, "ScreenSaveTimeOut", backup.timeout);

    RegCloseKey(hDesktop);

    RefreshScreensaverSettings(!originalWasNone && backup.active == "1");

    if (g_Log)
        g_Log->Info("RestoreOriginalScreensaverSettings: restored original "
                    "screensaver settings");

    return true;
}

void ScreensaverInstallerWin32::EnsureScreensaverActive(
    const std::string& workingDir)
{
    if (!g_Settings()->Get("settings.app.keep_screensaver_enabled", true))
    {
        if (g_Log)
            g_Log->Info("EnsureScreensaverActive: opt-out, skipping");
        return;
    }

    SaveOriginalScreensaverSettingsOnce(workingDir);

    const std::string scrPath = BuildScrPath(workingDir);

    if (!PathFileExistsA(scrPath.c_str()))
    {
        if (g_Log)
            g_Log->Warning("EnsureScreensaverActive: %s missing, skipping",
                           scrPath.c_str());
        return;
    }

    HKEY hKey = nullptr;
    const LSTATUS desktopRc = RegOpenKeyExA(HKEY_CURRENT_USER, kDesktopKey, 0,
                                            KEY_READ | KEY_WRITE, &hKey);

    if (desktopRc != ERROR_SUCCESS)
    {
        if (g_Log)
            g_Log->Warning(
                "EnsureScreensaverActive: cannot open Desktop key (%ld)",
                static_cast<long>(desktopRc));
        return;
    }

    const std::string currentScr = ReadRegString(hKey, "SCRNSAVE.EXE");
    const std::string currentActive = ReadRegString(hKey, "ScreenSaveActive");
    const std::string currentTimeout = ReadRegString(hKey, "ScreenSaveTimeOut");

    bool changed = false;

    if (!IsPositiveIntegerString(currentTimeout))
    {
        if (WriteRegString(hKey, "ScreenSaveTimeOut", "60"))
        {
            changed = true;
            if (g_Log)
                g_Log->Info(
                    "EnsureScreensaverActive: ScreenSaveTimeOut '%s' -> '60'",
                    currentTimeout.c_str());
        }
    }

    if (_stricmp(currentScr.c_str(), scrPath.c_str()) != 0)
    {
        if (WriteRegString(hKey, "SCRNSAVE.EXE", scrPath))
        {
            changed = true;
            if (g_Log)
                g_Log->Info(
                    "EnsureScreensaverActive: SCRNSAVE.EXE '%s' -> '%s'",
                    currentScr.c_str(), scrPath.c_str());
        }
    }

    if (currentActive != "1")
    {
        if (WriteRegString(hKey, "ScreenSaveActive", "1"))
        {
            changed = true;
            if (g_Log)
                g_Log->Info(
                    "EnsureScreensaverActive: ScreenSaveActive '%s' -> '1'",
                    currentActive.c_str());
        }
    }

    RegCloseKey(hKey);

    if (changed)
        RefreshScreensaverSettings(true);
}

#endif