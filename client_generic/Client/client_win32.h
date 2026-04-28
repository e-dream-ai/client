#ifndef CLIENT_WIN32_H_INCLUDED
#define CLIENT_WIN32_H_INCLUDED

#ifndef WIN32
#error This file is not supposed to be used for this platform...
#endif

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <string>
#include <tchar.h>
#include <vector>
#include <windows.h>
#include <wrl.h>

#include "clientversion.h"
#include "AboutDialogWin32.h"
#include "FirstTimeSetupWin32.h"
#include "ScreensaverInstallerWin32.h"
#include "SettingsDialogWin32.h"
#include "Exception.h"
#include "Log.h"
#include "MathBase.h"
#include "MonoInstance.h"
#include "Player.h"
#include "../DisplayOutput/D3D11/DisplayDX11.h"
#include "ProcessForker.h"
#include "Settings.h"
#include "base.h"
#include "storage.h"

using Microsoft::WRL::ComPtr;

/* #if defined (WIN32) && defined(_MSC_VER)
#include "../msvc/msvc_fix.h"
#endif
*/

/*
        CElectricSheep_Win32().
        Windows specific client code.
*/
class CElectricSheep_Win32 : public CElectricSheep
{
    CMonoInstance g_SingleInstanceObj;
    enum eScrMode
    {
        eNone,
        eConfig,   //	Forks the config user interface.
        ePassword, //	Currently ignored, handled by windows anyway for XP>.
        ePreview,  //	Will ignore events, renders into small preview window.
        eSaver,    //	Full screensaver mode.
        eFullScreenStandalone, //	Full screen
        eWindowed, //	Windowed mode, will ignore mouse events from exiting.
        eWindowed_AllowMultipleInstances, //	Same as eWindowed, but allows
                                          // multiple instances.
    };

    //	Mode deduced from cmdline parsing.
    eScrMode m_ScrMode;
    ComPtr<ID3D11Device> m_pD3D11;

    //	Previous mouse pos, for movement calcs.
    bool m_bMouseUnknown;
    int32_t m_MouseX, m_MouseY;

    bool m_bAllowFKey;
    bool m_bCtrlDown;

    //	Grab a string from the registry.
    HRESULT RegGetString(HKEY hKey, LPCSTR szValueName, LPSTR* lpszResult)
    {
#define MAXBUF 4096

        //	Given a HKEY and value name returns a string from the registry.
        //	Upon successful return the string should be freed using free().
        //	eg. RegGetString(hKey, TEXT("my value"), &szString);

        DWORD dwType = 0, dwDataSize = 0, dwBufSize = 0;
        LONG lResult;

        //	In case we fail set the return string to null...
        if (lpszResult != NULL)
            *lpszResult = NULL;

        //	Check input parameters...
        if (hKey == NULL || lpszResult == NULL)
            return E_INVALIDARG;

        //	Get the length of the string in bytes (placed in dwDataSize)...
        lResult =
            RegQueryValueExA(hKey, szValueName, 0, &dwType, NULL, &dwDataSize);

        //	Check result and make sure the registry value is a
        // string(REG_SZ)...
        if (lResult != ERROR_SUCCESS)
            return HRESULT_FROM_WIN32(lResult);
        else if (dwType != REG_SZ)
            return DISP_E_TYPEMISMATCH;

        //	Allocate memory for string - We add space for a null terminating
        // character...
        dwBufSize = dwDataSize + (1 * sizeof(CHAR));
        *lpszResult = (CHAR*)malloc(dwBufSize);

        if (*lpszResult == NULL)
            return E_OUTOFMEMORY;

        //	Now get the actual string from the registry...
        lResult = RegQueryValueExA(hKey, szValueName, 0, &dwType,
                                   (LPBYTE)*lpszResult, &dwDataSize);

        //	Check result and type again. If we fail here we must free the
        // memory we
        // allocated...
        if (lResult != ERROR_SUCCESS)
        {
            free(*lpszResult);
            return HRESULT_FROM_WIN32(lResult);
        }
        else if (dwType != REG_SZ)
        {
            free(*lpszResult);
            return DISP_E_TYPEMISMATCH;
        }

        //	We are not guaranteed a null terminated string from
        // RegQueryValueEx so
        // explicitly null terminate the returned string...
        (*lpszResult)[(dwBufSize / sizeof(CHAR)) - 1] = '\0';

        return NOERROR;
    }

  public:
    CElectricSheep_Win32()
        : CElectricSheep(),
#if defined(_DEBUG) || defined(DEBUG)
          g_SingleInstanceObj("Global\\{" CLIENT_VERSION_PRETTY "-stage}")
#else
          g_SingleInstanceObj("Global\\{" CLIENT_VERSION_PRETTY "}")
#endif
    {
        printf("CElectricSheep_Win32()\n");
        m_bAllowFKey = false;
        m_bCtrlDown = false;
        //m_pD3D12 = NULL;


        //	Windows spawns a screensaver process with idle priority, which
        // will
        // compete with forked generators.
        if (SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS))
            printf("Changed priority class to normal\n");
        else
            printf("Failed to change priority class\n");
    }

    virtual ~CElectricSheep_Win32()
    {
        if (m_pD3D11 != NULL)
        {
            m_pD3D11->Release();
        }
        if (m_ScrMode == eSaver || m_ScrMode == eFullScreenStandalone)
        {
            // BOOL bUnused;
            // SystemParametersInfo( SPI_SCREENSAVERRUNNING, FALSE, &bUnused,
            // WM_SETTINGCHANGE );
        }
        // SystemParametersInfo( SPI_SETSCREENSAVEACTIVE, TRUE, NULL,
        // WM_SETTINGCHANGE );
    }

    //
    virtual bool Startup()
    {
        //	Check for multiple instances.

        m_ScrMode = eNone;

        //	Some very ugly win32 screensaver cmdline parsing.
        char* c = GetCommandLineA();
        if (*c == '\"')
        {
            c++;
            while (*c != 0 && *c != '\"')
                c++;
        }
        else
        {
            while (*c != 0 && *c != ' ')
                c++;
        }

        if (*c != 0)
            c++;

        while (*c == ' ')
            c++;

        HWND hwnd = NULL;

        if (*c == 0)
        {
            char szModule[MAX_PATH] = {};
            const DWORD got = GetModuleFileNameA(NULL, szModule, MAX_PATH);
            const char* const ext =
                (got > 0) ? PathFindExtensionA(szModule) : nullptr;

            // Preserve classic .scr behavior: no args means "configure".
            // For the standalone .exe, default to windowed mode.
            if (ext != nullptr && _stricmp(ext, ".scr") == 0)
                m_ScrMode = eConfig;
            else
                m_ScrMode = eWindowed;
            hwnd = NULL;
        }
        else
        {
            if (*c == '-' || *c == '/')
                c++;

            if (*c == 'p' || *c == 'P' || *c == 'l' || *c == 'L')
            {
                c++;
                while (*c == ' ' || *c == ':')
                    c++;

                hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(
                    strtoull(c, nullptr, 10)));
                m_ScrMode = ePreview;
            }
            else if (*c == 's' || *c == 'S')
            {
                // SystemParametersInfo( SPI_SETSCREENSAVEACTIVE, FALSE, NULL,
                // WM_SETTINGCHANGE );
                m_ScrMode = eSaver;
            }
            else if (*c == 'c' || *c == 'C')
            {
                c++;
                while (*c == ' ' || *c == ':')
                    c++;

                m_ScrMode = eConfig;
            }
            else if (*c == 'a' || *c == 'A')
            {
                c++;
                while (*c == ' ' || *c == ':')
                    c++;

                hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(
                    strtoull(c, nullptr, 10)));
                m_ScrMode = ePassword;
            }
            //	Windowed test mode.
            else if (*c == 't' || *c == 'T')
            {
                m_ScrMode = eWindowed;
            }
            //	Windowed test mode which allows multiple instances.
            else if (*c == 'x' || *c == 'X')
            {
                m_ScrMode = eWindowed_AllowMultipleInstances;
                m_bAllowFKey = true;
            }
            // fullscreen with working F keys
            else if (*c == 'r' || *c == 'R')
            {
                // SystemParametersInfo( SPI_SETSCREENSAVEACTIVE, FALSE, NULL,
                // WM_SETTINGCHANGE );
                m_ScrMode = eFullScreenStandalone;
                m_bAllowFKey = true;
            }
        }

        //	Check for multiple instances if we're not specifically asked not
        // to.
        if (m_ScrMode != eWindowed_AllowMultipleInstances &&
            m_ScrMode != eSaver && m_ScrMode != eConfig &&
            m_ScrMode != eFullScreenStandalone)
        {
            if (g_SingleInstanceObj.IsAnotherInstanceRunning())
            {
                // return false; // we could also exit here to prevent second
                // instance
                m_MultipleInstancesMode = true;
            }
        }
        else
        {
            //	Instance check taken care of so revert back to eWindowed.
            if (g_SingleInstanceObj.IsAnotherInstanceRunning() == false)
            {
                if (m_ScrMode != eSaver && m_ScrMode != eConfig &&
                    m_ScrMode != eFullScreenStandalone)
                    m_ScrMode = eWindowed; // treat first instance normally
            }
            else
                m_MultipleInstancesMode = true;
        }

        // Per-user app data lives in %LOCALAPPDATA%\Infinidream. Using LocalAppData (rather
        // than the previous %ProgramData%) sidesteps an ACL trap: ProgramData's default ACL
        // gives Users only RX on files inside, so files first written by the installer's
        // (admin) run-at-end were unwritable for subsequent standard-user runs and the app
        // crashed on startup with no log entry. LocalAppData is owned by the running user,
        // so creator/owner rights make every file writable.
        char szPath[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, szPath)))
        {
#if defined(_DEBUG) || defined(DEBUG)
            const char* const kDataFolder = "Infinidream-stage";
#else
            const char* const kDataFolder = "Infinidream";
#endif
            PathAppendA(szPath, kDataFolder);
            PathAddBackslashA(szPath);
            m_AppData = szPath;
        }

        //	Get the application installation directory from the registry.
        //(stored by
        // the installer)
        m_WorkingDir = ".\\";

        HKEY key;
        if (!RegOpenKeyA(HKEY_LOCAL_MACHINE, "Software\\Infinidream", &key))
        {
            LPSTR temp;
            if (RegGetString(key, "InstallDir", &temp) == NOERROR)
            {
                m_WorkingDir = temp;
                free(temp);
            }
            RegCloseKey(key);
        }

        // Dev/runnable builds often have no installer registry key; in that case
        // use the executable directory so relative asset loads work regardless
        // of the terminal's current directory.
        if (m_WorkingDir == ".\\" || m_WorkingDir == "./" || m_WorkingDir == ".")
        {
            const std::string appDir = PlatformUtils::GetAppPath();
            if (!appDir.empty())
                m_WorkingDir = appDir;
        }

        //	If the exe is renamed to .scr, the path lacks trailing slashes
        // for some
        // bizarre reason...
        size_t len = m_WorkingDir.size();
        if (len > 0 && m_WorkingDir[len - 1] != '\\' && m_WorkingDir[len - 1] != '/')
        {
            m_WorkingDir.append("\\");
        }

        // Full screensaver (.scr /S): busy mode only when another instance holds
        // the global mutex (see CMonoInstance block above), matching macOS
        // .instance-lock behavior — solo saver runs auth/WebSocket; do not force
        // ForceMultipleInstancesMode here.

        if (InitStorage(m_MultipleInstancesMode) == false)
        {
            MessageBox(NULL, L"Unable to initialize scripts. Try reinstalling.",
                       L"Error!", MB_OK | MB_ICONERROR);
            return false;
        }

        if (g_SingleInstanceObj.IsAnotherInstanceRunning() == false)
            AttachLog();

        std::string tmp = "Working dir: " + m_WorkingDir;
        g_Log->Info(tmp.c_str());
        g_Log->Info("Commandline: %s", GetCommandLineA());

        _chdir(m_WorkingDir.c_str());

        // Mirror the Mac behavior: when the user-facing app launches, reassert
        // ourselves as the active screensaver if the preference is on. Done
        // here (not in the installer) so HKCU resolves to the actual user, not
        // the elevated installer's admin token. Skipped for screensaver/preview
        // modes — only the desktop app should be writing this preference.
        if (m_ScrMode == eWindowed ||
            m_ScrMode == eWindowed_AllowMultipleInstances ||
            m_ScrMode == eFullScreenStandalone)
        {
            ScreensaverInstallerWin32::EnsureScreensaverActive(m_WorkingDir);
        }

        if (m_ScrMode == eConfig)
        {
            char szCfgModule[MAX_PATH] = {};
            const DWORD cfgGot = GetModuleFileNameA(NULL, szCfgModule, MAX_PATH);
            const char* const cfgExt =
                (cfgGot > 0) ? PathFindExtensionA(szCfgModule) : nullptr;
            const bool launchedFromScr =
                cfgExt != nullptr && _stricmp(cfgExt, ".scr") == 0;

            if (launchedFromScr)
            {
                g_Log->Info("Configure/settings from .scr: show message to use standalone app.");
                MessageBoxA(
                    NULL,
                    "You cannot change settings from the screensaver (.scr).\n\n"
                    "Run infinidream (the desktop program, infinidream.exe) and open "
                    "Settings from there.",
                    "infinidream - Settings",
                    MB_OK | MB_ICONINFORMATION);
            }
            else
            {
                g_Log->Info("Running config (no GUI; edit JSON settings).");
                MessageBoxA(
                    NULL,
                    "The old wxWidgets settings application has been removed.\n\n"
                    "Edit the JSON settings file (" CLIENT_SETTINGS
                    ".json) in your data folder, next to this program, or wherever "
                    "the client stores its configuration.\n\n"
                    "Then run the screensaver or player normally.",
                    "infinidream - Settings",
                    MB_OK | MB_ICONINFORMATION);
            }
            m_bConfigMode = true;
            return false;
        }

        //	Exit if we're not supposed to render anything...
        if (m_ScrMode != eSaver && m_ScrMode != eFullScreenStandalone &&
            m_ScrMode != ePreview && m_ScrMode != eWindowed &&
            m_ScrMode != eWindowed_AllowMultipleInstances)
            return false;

        //	Set preview flag and force busy mode for preview (same as Mac)
        SetIsPreview(m_ScrMode == ePreview);
        if (m_ScrMode == ePreview) {
            ForceMultipleInstancesMode(true);
        }

        const bool allowOverlays = (m_ScrMode != eSaver && m_ScrMode != ePreview);
        FirstTimeSetupWin32_SetOverlayAllowed(allowOverlays);
        AboutDialogWin32_SetOverlayAllowed(allowOverlays);
        SettingsDialogWin32_SetOverlayAllowed(allowOverlays);
        FirstTimeSetupWin32_Register();
        SettingsDialogWin32_Register();

        //	A window was provided, let's use it.
        if (hwnd)
            g_Player().SetHWND(hwnd);

        //	To prevent ctrl+alt+del stuff.
        // #warning NOTE (Keffo#1#): This is just annoying until all bugs are
        // gone!
        if (m_ScrMode == eSaver || m_ScrMode == eFullScreenStandalone)
        {
            // BOOL bUnused;
            // SystemParametersInfo( SPI_SCREENSAVERRUNNING, TRUE, &bUnused,
            // WM_SETTINGCHANGE );
        }

        //	User wants windowed? (preview is always embedded child window)
        if (m_ScrMode == eWindowed ||
            m_ScrMode == eWindowed_AllowMultipleInstances ||
            m_ScrMode == ePreview)
            g_Player().Fullscreen(false);

        uint32_t monnum = g_Settings()->Get("settings.player.screen", 0);

        // m_pD3D9 = Direct3DCreate9(D3D_SDK_VERSION);

        const uint32_t requestedScreen =
            g_Settings()->Get("settings.player.screen", 0);
        const bool wantAllMonitors = (m_ScrMode == eSaver);
        const bool blankOtherDisplays =
            !wantAllMonitors &&
            (m_ScrMode != eFullScreenStandalone) && (m_ScrMode != eWindowed);

        if (m_ScrMode == eSaver)
            g_Player().SetIsScreenSaverRun(true);
        else
            g_Player().SetIsScreenSaverRun(false);

        auto countMonitors = []() -> uint32_t {
            struct Ctx
            {
                uint32_t count = 0;
            } ctx;
            auto cb = [](HMONITOR, HDC, LPRECT, LPARAM lp) -> BOOL {
                auto* c = reinterpret_cast<Ctx*>(lp);
                if (c) c->count++;
                return TRUE;
            };
            EnumDisplayMonitors(nullptr, nullptr, cb, reinterpret_cast<LPARAM>(&ctx));
            return ctx.count ? ctx.count : 1u;
        };

        if (wantAllMonitors)
        {
            const uint32_t monitorCount = countMonitors();
            g_Log->Info("Screensaver multi-monitor mode: creating %u windows", monitorCount);

            bool anyOk = false;
            // Start with the requested screen (if in range), then create the rest.
            auto addOne = [&](uint32_t idx) {
                if (g_Player().AddDisplay(idx, /*blankOtherDisplays*/ false) != -1)
                {
                    anyOk = true;
                    g_Log->Info("AddDisplay succeeded for screen %u", idx);
                }
                else
                {
                    g_Log->Error("AddDisplay failed for screen %u", idx);
                }
            };

            if (requestedScreen < monitorCount)
                addOne(requestedScreen);
            for (uint32_t i = 0; i < monitorCount; ++i)
            {
                if (i == requestedScreen)
                    continue;
                addOne(i);
            }

            if (!anyOk)
                return false;
        }
        else if (g_Player().AddDisplay(requestedScreen, blankOtherDisplays) == -1)
        {
            bool foundfirstmon = false;
            g_Log->Error("AddDisplay failed for screen %d", monnum);
            g_Log->Info("Trying to autodetect usable monitors...");
            monnum = 0;
            while (monnum < 9)
            {
                g_Log->Info("Trying monitor %d", monnum);
                if (g_Player().AddDisplay(monnum, blankOtherDisplays) != -1)
                {
                    foundfirstmon = true;
                    g_Log->Info("Monitor %d ok", monnum);
                    break;
                }
                else
                {
                    g_Log->Error("Monitor %d failed", monnum);
                    ++monnum;
                }
            }
            if (foundfirstmon == false)
                return false;
        }
        else
            g_Log->Info("AddDisplay succeeded for screen %d",
                        g_Settings()->Get("settings.player.screen", 0));
        /* if (m_ScrMode != eWindowed &&
            m_ScrMode != eWindowed_AllowMultipleInstances &&
            g_Settings()->Get("settings.player.MultiDisplayMode", 0) !=
                CPlayer::kMDSingleScreen)
            for (DWORD dw = (monnum + 1);
                 dw < g_Player().Display()->GetNumMonitors(); ++dw)
            {
                if (g_Player().AddDisplay(dw, m_pD3D9) == true)
                    g_Log->Info("AddDisplay succeeded for screen %d", dw);
                else
                    g_Log->Error("AddDisplay failed for screen %d", dw);
            }*/
        //
        if (CElectricSheep::Startup() == false)
            return false;

        // Keep the dream rendering while a menu bar popup is open.
        // Windows enters a modal menu-tracking loop on the main thread when the
        // user clicks a menu, which blocks our Run() loop entirely.  We install
        // a ~60 fps WM_TIMER that fires during that modal loop so the display
        // continues to update visually.
        if (m_ScrMode != ePreview && m_ScrMode != eSaver)
        {
            if (auto spDisplay = g_Player().Display())
            {
                if (auto* pDX11 = dynamic_cast<DisplayOutput::CDisplayDX11*>(spDisplay.get()))
                {
                    pDX11->SetMenuLoopRenderCallback([this]() {
                        CElectricSheep_Win32::Update();
                    });
                }
            }
        }

        //	Reset mouse calcs.
        m_bMouseUnknown = true;

        return true;
    }

    // @TODO : Might no longer be needed, need to triple check as there is a lot of code moved to client already
    
    virtual bool HandleOneEvent(DisplayOutput::spCEvent& _event)
    {
        auto toggleFullscreenInPlace = [this]() -> bool
        {
            DisplayOutput::spCDisplayOutput spDisplay = g_Player().Display();
            if (!spDisplay)
            {
                g_Log->Warning("Fullscreen toggle requested but display is null");
                return false;
            }

            if (!spDisplay->ToggleFullscreen())
            {
                g_Log->Warning("Fullscreen toggle failed");
                return false;
            }

            const bool nowFullscreen = spDisplay->IsFullscreen();
            g_Client()->SetIsFullScreen(nowFullscreen);
            if (nowFullscreen)
                m_ScrMode = eFullScreenStandalone;
            else
                m_ScrMode = m_MultipleInstancesMode ? eWindowed_AllowMultipleInstances : eWindowed;
            return true;
        };

        //	Handle events.
        if (_event->Type() == DisplayOutput::CEvent::Event_Power)
        {
            g_Log->Info("Power broadcast event, closing...");
            return false;
        }
        else
            //	Key events.
            if (_event->Type() == DisplayOutput::CEvent::Event_KEY)
            {
                if (SettingsDialogWin32_HasPendingOrVisible() || FirstTimeSetupWin32_IsWizardVisible())
                    return true;

                DisplayOutput::spCKeyEvent spKey =
                    std::dynamic_pointer_cast<DisplayOutput::CKeyEvent>(_event);
                m_bCtrlDown = ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0);

                switch (spKey->m_Code)
                {
                case DisplayOutput::CKeyEvent::KEY_Esc:
                    if (m_ScrMode == eSaver || m_ScrMode == ePreview)
                        return false;
                    {
                        DisplayOutput::spCDisplayOutput spDisplay = g_Player().Display();
                        if (spDisplay && spDisplay->IsFullscreen())
                            return toggleFullscreenInPlace();
                    }
                    return false;
                    break;

                case DisplayOutput::CKeyEvent::KEY_F:
                    if (m_ScrMode != eSaver && m_bCtrlDown)
                        return toggleFullscreenInPlace();
                    if (m_bAllowFKey == true)
                        return toggleFullscreenInPlace();
                    break;
                case DisplayOutput::CKeyEvent::KEY_F11:
                    if (m_ScrMode != eSaver)
                        return toggleFullscreenInPlace();
                    break;
                case DisplayOutput::CKeyEvent::KEY_TAB:
                    if (m_ScrMode != eWindowed &&
                        m_ScrMode != eWindowed_AllowMultipleInstances &&
                        m_ScrMode != eFullScreenStandalone)
                        return false;
                    break;
                case DisplayOutput::CKeyEvent::KEY_LALT:
                case DisplayOutput::CKeyEvent::KEY_MENU:
                case DisplayOutput::CKeyEvent::KEY_CTRL:
                    m_bCtrlDown = ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0);
                    break;

                //	All other keys close...
                default:
                {
                    // Keep legacy "any key closes" behavior for saver/preview.
                    if (m_ScrMode == eSaver || m_ScrMode == ePreview)
                    {
                        g_Log->Info("Unhandled key event in saver/preview, closing");
                        return false;
                    }
                    // In app/player modes, let the core client command handler
                    // process hotkeys first.
                    return false;
                }
                }
                return true;
            }
            else
                //	Process mouse events (ignored in eStandalone mode).
                if (_event->Type() == DisplayOutput::CEvent::Event_Mouse)
                {

                    if (m_ScrMode != eWindowed &&
                        m_ScrMode != eWindowed_AllowMultipleInstances &&
                        m_ScrMode != ePreview &&
                        m_ScrMode != eFullScreenStandalone)
                    {
                        DisplayOutput::spCMouseEvent spMouse =
                            std::dynamic_pointer_cast<DisplayOutput::CMouseEvent>(_event);

                        if (spMouse->m_Code ==
                            DisplayOutput::CMouseEvent::Mouse_MOVE)
                        {
                            float dx = (float)spMouse->m_X - (float)m_MouseX;
                            float dy = (float)spMouse->m_Y - (float)m_MouseY;
                            float dist = dx * dx + dy * dy;

                            //	Store for next run.
                            m_MouseX = spMouse->m_X;
                            m_MouseY = spMouse->m_Y;

                            if (!m_bMouseUnknown)
                            {
                                //	Exit if mouse moved > 20 pixels...
                                if (dist > 20 * 20)
                                {
                                    g_Log->Info(
                                        "Mouse moved too far(%f), closing...",
                                        Base::Math::Sqrt(dist));
                                    return false;
                                }
                            }
                            else
                                g_Log->Info("(ignored) Mouse move (%f)",
                                            Base::Math::Sqrt(dist));

                            m_bMouseUnknown = false;
                            return true;
                        }
                        else
                        {
                            //	All other mouse events exit.
                            return false;
                        }
                    }
                    return true;
                }
        return false;
    }
    
    virtual bool HandleEvents()
    {
        DisplayOutput::spCDisplayOutput spDisplay = g_Player().Display();
        if (!spDisplay)
            return true;

        //	Handle events.
        DisplayOutput::spCEvent spEvent;
        while (spDisplay->GetEvent(spEvent))
        {
            if (HandleOneEvent(spEvent) == false)
            {
                if (CElectricSheep::HandleOneEvent(spEvent) == false)
                {
                    if (spEvent->Type() == DisplayOutput::CEvent::Event_KEY &&
                        (m_ScrMode == eWindowed ||
                         m_ScrMode == eWindowed_AllowMultipleInstances ||
                         m_ScrMode == eFullScreenStandalone))
                    {
                        // In app/player modes, ignore truly unmapped keys.
                        continue;
                    }
                    return false;
                }
            }
        }

        ProcessCommandQueue();

        return true;
    }
    //

    virtual bool Update()
    {
        //g_Player().Framerate(m_CurrentFps);

        if (!CElectricSheep::Update())
            return false;

        DisplayOutput::spCDisplayOutput spDisplay = g_Player().Display();
        if (!spDisplay)
            return true;

        //	We ignore events in preview mode.
        if (m_ScrMode == ePreview)
        {
            spDisplay->ClearEvents();
            return true;
        }

        //static const float voteDelaySeconds = 1;
        return HandleEvents();
    } 

    // TODO : Never called ?
    virtual bool Update(int _displayIdx)
    {
        //g_Player().Framerate(m_CurrentFps);

        // We don't use a separate update/render thread on Win32.
        // Ignore barriers and perform a normal per-display update.
        if (!CElectricSheep::Update(_displayIdx))
            return false;

        DisplayOutput::spCDisplayOutput spDisplay = g_Player().Display();
        if (!spDisplay)
            return true;

        //	We ignore events in preview mode.
        if (m_ScrMode == ePreview)
        {
            spDisplay->ClearEvents();
            return true;
        }

        //static const float voteDelaySeconds = 1;
        // @TODO: Might need to return true here and not HandleEvents, like mac client does now?
        return HandleEvents();
    }

    virtual std::string GetVersion() { return CLIENT_VERSION; }

    virtual int GetACLineStatus()
    {
        SYSTEM_POWER_STATUS systemPowerStatus = {0};
        if (GetSystemPowerStatus(&systemPowerStatus) != 0)
        {
            if ((systemPowerStatus.BatteryFlag && 128 != 0) ||
                (systemPowerStatus.BatteryFlag && 255 != 0))
                return -1; // no battery
            return systemPowerStatus.ACLineStatus;
        }
        return -1;
    }
};

#endif // CLIENT_H_INCLUDED
