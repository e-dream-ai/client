#pragma once
#include "MathBase.h"
#include "base.h"

#include "../msvc/msvc_fix.h"
#include "Timer.h"
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>

class ESCpuUsage
{
  private:
    FILETIME m_LastIdleTime{};
    FILETIME m_LastKernelTime{};
    FILETIME m_LastUserTime{};
    FILETIME m_LastESKernelTime{};
    FILETIME m_LastESUserTime{};
    Base::CTimer m_Timer;
    double m_LastCPUCheckTime{};
    int m_CachedEs{-1};
    int m_CachedTotal{-1};
    bool m_HasCachedSample{false};

    static LONGLONG fileTimeLl(FILETIME ft)
    {
        LARGE_INTEGER u;
        u.LowPart = ft.dwLowDateTime;
        u.HighPart = ft.dwHighDateTime;
        return u.QuadPart;
    }

  public:
    ESCpuUsage() : m_LastCPUCheckTime(0)
    {
        m_Timer.Reset();
        m_LastCPUCheckTime = m_Timer.Time();
        GetSystemTimes(&m_LastIdleTime, &m_LastKernelTime, &m_LastUserTime);
        FILETIME crea{}, exit{};
        GetProcessTimes(GetCurrentProcess(), &crea, &exit, &m_LastESKernelTime,
                        &m_LastESUserTime);
    }

    // _total = system CPU %, _es = this process % (same semantics as macOS path).
    // Samples at most once per second to match macOS and avoid frame-to-frame jitter.
    bool GetCpuUsage(int& _total, int& _es)
    {
        FILETIME crea{}, exit{};
        FILETIME idleTime{}, kernelTime{}, userTime{};
        FILETIME esKernel{}, esUser{};

        SYSTEM_INFO sysinfo{};
        GetSystemInfo(&sysinfo);
        const DWORD nproc = sysinfo.dwNumberOfProcessors > 0
                                ? sysinfo.dwNumberOfProcessors
                                : 1;

        const double newtime = m_Timer.Time();
        const double period = newtime - m_LastCPUCheckTime;

        if (period < 1.0)
        {
            if (m_HasCachedSample)
            {
                _total = m_CachedTotal;
                _es = m_CachedEs;
                return true;
            }
            _total = -1;
            _es = -1;
            return true;
        }

        if (GetProcessTimes(GetCurrentProcess(), &crea, &exit, &esKernel,
                            &esUser) == 0)
            return false;
        if (GetSystemTimes(&idleTime, &kernelTime, &userTime) == 0)
            return false;

        const LONGLONG dEsUser =
            fileTimeLl(esUser) - fileTimeLl(m_LastESUserTime);
        const LONGLONG dEsKernel =
            fileTimeLl(esKernel) - fileTimeLl(m_LastESKernelTime);
        const LONGLONG dProc = dEsUser + dEsKernel;

        if (period > 0. && dProc >= 0)
        {
            _es = static_cast<int>(dProc * 100. / (period * 1e7) /
                                   static_cast<double>(nproc));
        }
        else
            _es = 0;
        _es = ::Base::Math::Clamped(_es, 0, 100);

        const LONGLONG dIdle =
            fileTimeLl(idleTime) - fileTimeLl(m_LastIdleTime);
        const LONGLONG dKern =
            fileTimeLl(kernelTime) - fileTimeLl(m_LastKernelTime);
        const LONGLONG dUsr =
            fileTimeLl(userTime) - fileTimeLl(m_LastUserTime);
        const LONGLONG totalSys = dKern + dUsr;

        if (totalSys > 0)
        {
            _total = static_cast<int>((totalSys - dIdle) * 100 / totalSys);
        }
        else
            _total = 0;
        _total = ::Base::Math::Clamped(_total, 0, 100);

        m_LastESUserTime = esUser;
        m_LastESKernelTime = esKernel;
        m_LastIdleTime = idleTime;
        m_LastKernelTime = kernelTime;
        m_LastUserTime = userTime;
        m_LastCPUCheckTime = newtime;
        m_CachedEs = _es;
        m_CachedTotal = _total;
        m_HasCachedSample = true;
        return true;
    }

    void GetAppCpuUsage(int& _es, int& _total)
    {
        (void)GetCpuUsage(_total, _es);
    }

    int GetGpuUsage()
    {
        return -1;
    }

    int GetNumCores()
    {
        SYSTEM_INFO sysinfo{};
        GetSystemInfo(&sysinfo);
        return static_cast<int>(sysinfo.dwNumberOfProcessors);
    }
};
