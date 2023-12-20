//
//  PlatformUtils.h
//  e-dream
//
//  Created by Tibi Hencz on 12.12.2023.
//

#ifndef PlatformUtils_h
#define PlatformUtils_h

#include <cstdio>
#include <string>
#include <functional>

class PlatformUtils
{
  public:
    static bool IsInternetReachable();
    static std::string GetBuildDate();
    static std::string GetGitRevision();
    static std::string GetAppVersion();
    static void SetCursorHidden(bool _hidden);
    static void
    SetOnMouseMovedCallback(std::function<void(int, int)> _callback);
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
