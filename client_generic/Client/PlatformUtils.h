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
    static void OpenURLExternally(std::string_view _url);
    static void SetCursorHidden(bool _hidden);
    static void
    SetOnMouseMovedCallback(std::function<void(int, int)> _callback);
    static void SetThreadName(std::string_view _name);
    static void DispatchOnMainThread(std::function<void()> _func);
    static std::string GetAppPath();
    static void NotifyError(std::string_view errorMessage);
};

class CDelayedDispatch
{
    uint64_t m_DispatchTime;
    std::function<void()> m_Func;

  public:
    CDelayedDispatch(std::function<void()> _func);
    void DispatchAfter(uint64_t _seconds);
    void Cancel();
};

MakeSmartPointers(CDelayedDispatch);

#endif /* PlatformUtils_h */
