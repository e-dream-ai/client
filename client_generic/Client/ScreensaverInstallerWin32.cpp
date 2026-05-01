#ifdef WIN32

#include "ScreensaverInstallerWin32.h"

#include <windows.h>
#include <shlwapi.h>

#include <cstdlib>
#include <string>

#include "Log.h"
#include "Settings.h"

#pragma comment(lib, "shlwapi.lib")

namespace
{
constexpr const char* kDesktopKey = "Control Panel\\Desktop";
constexpr const char* kBackupKey = "Software\\Infinidream\\Screensaver";

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

void RefreshScreensaverSettings(bool active)
{
    SystemParametersInfoA(SPI_SETSCREENSAVEACTIVE, active ? TRUE : FALSE,
                          nullptr, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
}
} // namespace

void ScreensaverInstallerWin32::SaveOriginalScreensaverSettingsOnce(
    const std::string& workingDir)
{
    HKEY hBackup = nullptr;
    DWORD disposition = 0;

    if (RegCreateKeyExA(HKEY_CURRENT_USER, kBackupKey, 0, nullptr, 0,
                        KEY_READ | KEY_WRITE, nullptr, &hBackup,
                        &disposition) != ERROR_SUCCESS)
        return;

    HKEY hDesktop = nullptr;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, kDesktopKey, 0, KEY_READ, &hDesktop) !=
        ERROR_SUCCESS)
    {
        RegCloseKey(hBackup);
        return;
    }

    const std::string originalExe = ReadRegString(hDesktop, "SCRNSAVE.EXE");
    const std::string originalActive =
        ReadRegString(hDesktop, "ScreenSaveActive");
    const std::string originalTimeout =
        ReadRegString(hDesktop, "ScreenSaveTimeOut");

    const std::string infinidreamScr = BuildScrPath(workingDir);

    if (_stricmp(originalExe.c_str(), infinidreamScr.c_str()) == 0)
    {
        RegCloseKey(hDesktop);
        RegCloseKey(hBackup);
        return;
    }

    WriteRegString(hBackup, "OriginalExe", originalExe);
    WriteRegString(hBackup, "OriginalActive", originalActive);
    WriteRegString(hBackup, "OriginalTimeout", originalTimeout);
    WriteRegString(hBackup, "OriginalSaved", "1");

    RegCloseKey(hDesktop);
    RegCloseKey(hBackup);
}

bool ScreensaverInstallerWin32::RestoreOriginalScreensaverSettings()
{
    HKEY hBackup = nullptr;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, kBackupKey, 0, KEY_READ, &hBackup) !=
        ERROR_SUCCESS)
        return false;

    if (ReadRegString(hBackup, "OriginalSaved") != "1")
    {
        RegCloseKey(hBackup);
        return false;
    }

    const std::string originalExe = ReadRegString(hBackup, "OriginalExe");
    const std::string originalActive = ReadRegString(hBackup, "OriginalActive");
    const std::string originalTimeout =
        ReadRegString(hBackup, "OriginalTimeout");

    RegCloseKey(hBackup);

    HKEY hDesktop = nullptr;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, kDesktopKey, 0, KEY_READ | KEY_WRITE,
                      &hDesktop) != ERROR_SUCCESS)
        return false;

    const bool originalWasNone = originalExe.empty();

    WriteRegString(hDesktop, "SCRNSAVE.EXE",
                   originalWasNone ? "" : originalExe);
    WriteRegString(hDesktop, "ScreenSaveActive",
                   originalWasNone ? "0" : originalActive);

    if (!originalTimeout.empty())
        WriteRegString(hDesktop, "ScreenSaveTimeOut", originalTimeout);

    RegCloseKey(hDesktop);

    RefreshScreensaverSettings(!originalWasNone && originalActive == "1");

    return true;
}

void ScreensaverInstallerWin32::EnsureScreensaverActive(
    const std::string& workingDir)
{
    if (!g_Settings()->Get("settings.app.keep_screensaver_enabled", true))
        return;

    SaveOriginalScreensaverSettingsOnce(workingDir);

    const std::string scrPath = BuildScrPath(workingDir);

    if (!PathFileExistsA(scrPath.c_str()))
        return;

    HKEY hKey = nullptr;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, kDesktopKey, 0, KEY_READ | KEY_WRITE,
                      &hKey) != ERROR_SUCCESS)
        return;

    const std::string currentScr = ReadRegString(hKey, "SCRNSAVE.EXE");
    const std::string currentActive = ReadRegString(hKey, "ScreenSaveActive");
    const std::string currentTimeout = ReadRegString(hKey, "ScreenSaveTimeOut");

    bool changed = false;

    if (!IsPositiveIntegerString(currentTimeout))
    {
        if (WriteRegString(hKey, "ScreenSaveTimeOut", "60"))
            changed = true;
    }

    if (_stricmp(currentScr.c_str(), scrPath.c_str()) != 0)
    {
        if (WriteRegString(hKey, "SCRNSAVE.EXE", scrPath))
            changed = true;
    }

    if (currentActive != "1")
    {
        if (WriteRegString(hKey, "ScreenSaveActive", "1"))
            changed = true;
    }

    RegCloseKey(hKey);

    if (changed)
        RefreshScreensaverSettings(true);
}

#endif