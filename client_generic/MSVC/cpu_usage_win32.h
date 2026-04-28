#pragma once
#include "MathBase.h"
#include "base.h"

#include "../msvc/msvc_fix.h"
#include "Timer.h"
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <vector>
#include <string>

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

    // Windows GPU usage via PDH (\GPU Engine(*)\Utilization Percentage).
    // We report max utilization across this process' engines, sampled ~1Hz.
    PDH_HQUERY m_GpuQuery{nullptr};
    std::vector<PDH_HCOUNTER> m_GpuCounters;
    double m_LastGPUCheckTime{0.0};
    double m_LastGpuCounterRebuildTime{0.0};
    int m_CachedGpu{-1};
    bool m_HasCachedGpuSample{false};

    static std::wstring to_wstring(DWORD v)
    {
        wchar_t buf[32]{};
        _snwprintf_s(buf, _TRUNCATE, L"%lu", static_cast<unsigned long>(v));
        return std::wstring(buf);
    }

    void CloseGpuQuery()
    {
        if (m_GpuQuery)
        {
            PdhCloseQuery(m_GpuQuery);
            m_GpuQuery = nullptr;
        }
        m_GpuCounters.clear();
    }

    bool RebuildGpuCounters()
    {
        CloseGpuQuery();

        const PDH_STATUS openSt = PdhOpenQueryW(nullptr, 0, &m_GpuQuery);
        if (openSt != ERROR_SUCCESS || !m_GpuQuery)
            return false;

        DWORD pid = GetCurrentProcessId();
        std::wstring pidToken = L"pid_" + to_wstring(pid) + L"_";

        // Enumerate GPU Engine instances.
        DWORD counterListSize = 0;
        DWORD instanceListSize = 0;
        PDH_STATUS enumSt = PdhEnumObjectItemsW(
            nullptr, nullptr, L"GPU Engine",
            nullptr, &counterListSize,
            nullptr, &instanceListSize,
            PERF_DETAIL_WIZARD, 0);

        if (enumSt != PDH_MORE_DATA || instanceListSize == 0)
            return false;

        std::vector<wchar_t> counterList(counterListSize);
        std::vector<wchar_t> instanceList(instanceListSize);
        enumSt = PdhEnumObjectItemsW(
            nullptr, nullptr, L"GPU Engine",
            counterList.data(), &counterListSize,
            instanceList.data(), &instanceListSize,
            PERF_DETAIL_WIZARD, 0);

        if (enumSt != ERROR_SUCCESS)
            return false;

        // Multi-string: sequence of null-terminated strings, ending with extra null.
        for (const wchar_t* p = instanceList.data(); p && *p; p += wcslen(p) + 1)
        {
            std::wstring inst = p;
            if (inst.find(pidToken) == std::wstring::npos)
                continue;

            // \GPU Engine(<instance>)\Utilization Percentage
            std::wstring path = L"\\GPU Engine(" + inst + L")\\Utilization Percentage";
            PDH_HCOUNTER ctr = nullptr;
            const PDH_STATUS addSt = PdhAddEnglishCounterW(m_GpuQuery, path.c_str(), 0, &ctr);
            if (addSt == ERROR_SUCCESS && ctr)
                m_GpuCounters.push_back(ctr);
        }

        if (m_GpuCounters.empty())
        {
            CloseGpuQuery();
            return false;
        }

        // Prime query to allow formatted reads on next sample.
        (void)PdhCollectQueryData(m_GpuQuery);

        const double now = m_Timer.Time();
        m_LastGPUCheckTime = now;
        m_LastGpuCounterRebuildTime = now;
        return true;
    }

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

    ~ESCpuUsage()
    {
        CloseGpuQuery();
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
        const double now = m_Timer.Time();

        // Cache at ~1Hz to avoid PDH overhead and jitter.
        if ((now - m_LastGPUCheckTime) < 1.0)
        {
            if (m_HasCachedGpuSample)
                return m_CachedGpu;
        }

        // Rebuild counters periodically to catch new engines / resets.
        // (Instances can appear/disappear over time.)
        const bool needRebuild = (!m_GpuQuery || m_GpuCounters.empty() ||
                                  (now - m_LastGpuCounterRebuildTime) > 15.0);

        if (needRebuild)
        {
            if (!RebuildGpuCounters())
            {
                m_LastGPUCheckTime = now;
                m_CachedGpu = -1;
                m_HasCachedGpuSample = true;
                return -1;
            }
            // RebuildGpuCounters primed the query with one PdhCollectQueryData;
            // rate counters need a second collect with elapsed time to produce
            // a value. Hold the previous reading this tick and let the next
            // ~1 Hz call perform the real collect (otherwise the display dips
            // to 0 every time we rebuild).
            return m_HasCachedGpuSample ? m_CachedGpu : -1;
        }

        const PDH_STATUS collectSt = PdhCollectQueryData(m_GpuQuery);
        if (collectSt != ERROR_SUCCESS)
        {
            // Query became invalid; try one rebuild next time.
            CloseGpuQuery();
            m_LastGPUCheckTime = now;
            m_CachedGpu = -1;
            m_HasCachedGpuSample = true;
            return -1;
        }

        double maxPct = 0.0;
        bool hasValid = false;

        for (auto ctr : m_GpuCounters)
        {
            PDH_FMT_COUNTERVALUE v{};
            DWORD type = 0;
            const PDH_STATUS st = PdhGetFormattedCounterValue(ctr, PDH_FMT_DOUBLE, &type, &v);
            if (st != ERROR_SUCCESS)
                continue;
            if (v.CStatus != PDH_CSTATUS_VALID_DATA && v.CStatus != PDH_CSTATUS_NEW_DATA)
                continue;

            if (v.doubleValue >= 0.0)
            {
                hasValid = true;
                if (v.doubleValue > maxPct)
                    maxPct = v.doubleValue;
            }
        }

        int result = -1;
        if (hasValid)
        {
            // PDH can report slightly >100; clamp for UI.
            result = static_cast<int>(maxPct + 0.5);
            result = ::Base::Math::Clamped(result, 0, 100);
        }

        // If we had no valid values, engines may have changed; trigger rebuild sooner.
        if (!hasValid)
            m_LastGpuCounterRebuildTime = 0.0;

        m_LastGPUCheckTime = now;
        m_CachedGpu = result;
        m_HasCachedGpuSample = true;
        return result;
    }

    int GetNumCores()
    {
        SYSTEM_INFO sysinfo{};
        GetSystemInfo(&sysinfo);
        return static_cast<int>(sysinfo.dwNumberOfProcessors);
    }
};
