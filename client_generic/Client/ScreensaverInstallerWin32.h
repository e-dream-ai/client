#pragma once

#ifdef WIN32

#include <string>

namespace ScreensaverInstallerWin32
{
void SaveOriginalScreensaverSettingsOnce(const std::string& workingDir);
bool RestoreOriginalScreensaverSettings();

// Make sure infinidream is the active Windows screensaver, when the user has opted
// in via settings.app.keep_screensaver_enabled. Idempotent — re-reads the registry
// and only writes when values differ. Safe to call on every app launch.
//
// workingDir is the install directory containing infinidream.scr (typically
// %ProgramFiles%\Infinidream from the NSIS installer, or the dev exe directory).
//
// Runs in the user-facing app process, never in the screensaver itself, so HKCU
// resolves to the real user — no elevation/HKCU trap from the installer's run-at-end.
void EnsureScreensaverActive(const std::string& workingDir);
} // namespace ScreensaverInstallerWin32

#endif // WIN32