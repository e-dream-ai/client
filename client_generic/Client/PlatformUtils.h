//
//  PlatformUtils.h
//  e-dream
//
//  Created by Tibi Hencz on 12.12.2023.
//

#ifndef PlatformUtils_h
#define PlatformUtils_h

#include "SmartPtr.h"
#include <cstdio>
#include <string>
#include <functional>

#ifdef WIN32
#include <Windows.h>
#include <mutex>
#endif

class PlatformUtils
{
  private:
    PlatformUtils() = delete;
    ~PlatformUtils() = delete;

  public:
    static bool IsInternetReachable();
    static std::string GetBuildDate();
    static std::string GetGitRevision();
    static std::string GetAppVersion();
    static std::string GetPlatformName();
    static std::string GetWorkingDir();
    static void OpenURLExternally(std::string_view _url);
    static void SetCursorHidden(bool _hidden);
    static void
    SetOnMouseMovedCallback(std::function<void(int, int)> _callback);
    static void SetThreadName(std::string_view _name);
    static void DispatchOnMainThread(std::function<void()> _func);
    static std::string GetAppPath();
    static void NotifyError(std::string_view errorMessage);
    static std::string CalculateFileMD5(const std::string& filepath);

    /// Native window handle used for delayed work and input hooks, registered from
    /// the player window (an HWND on Windows). Platforms that route those through
    /// other means implement this as a no-op.
    static void SetNativeMessageWindow(void* _nativeHandle);
    /// Called from the display's mouse-move event, in client coordinates.
    /// Platforms that deliver mouse moves directly implement this as a no-op.
    static void NotifyMouseMoved(int _x, int _y);
};

class CDelayedDispatch
{
    uint64_t m_DispatchTime;
    std::function<void()> m_Func;

#ifdef WIN32
    std::mutex m_TimerMutex;
    void Win32OnTimerFired();
    static void CALLBACK Win32TimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent,
                                        DWORD dwTime);
#endif

  public:
    CDelayedDispatch(std::function<void()> _func);
    void DispatchAfter(uint64_t _seconds);
    void Cancel();
};

MakeSmartPointers(CDelayedDispatch);

#endif /* PlatformUtils_h */
