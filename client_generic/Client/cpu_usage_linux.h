#ifndef _CPU_USAGE_LINUX_H_
#define _CPU_USAGE_LINUX_H_

#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>
#include <cstdio>
#include <cstdint>

class ESCpuUsage
{
    Base::CTimer m_Timer;
    double   m_LastCPUCheckTime;
    double   m_LastESTime;

    uint64_t m_LastUser;
    uint64_t m_LastNice;
    uint64_t m_LastSystem;
    uint64_t m_LastIdle;

    bool ReadProcStat(uint64_t& user, uint64_t& nice,
                      uint64_t& system, uint64_t& idle)
    {
        FILE* f = fopen("/proc/stat", "r");
        if (!f) return false;
        int matched = fscanf(f, "cpu %lu %lu %lu %lu", &user, &nice, &system, &idle);
        fclose(f);
        return matched == 4;
    }

  public:
    ESCpuUsage()
        : m_LastCPUCheckTime(0), m_LastESTime(0),
          m_LastUser(0), m_LastNice(0), m_LastSystem(0), m_LastIdle(0)
    {
        m_Timer.Reset();
        m_LastCPUCheckTime = m_Timer.Time();

        struct rusage r_usage;
        if (!getrusage(RUSAGE_SELF, &r_usage))
        {
            m_LastESTime = (double)r_usage.ru_utime.tv_sec +
                           (double)r_usage.ru_utime.tv_usec * 1e-6;
            m_LastESTime += (double)r_usage.ru_stime.tv_sec +
                            (double)r_usage.ru_stime.tv_usec * 1e-6;
        }

        ReadProcStat(m_LastUser, m_LastNice, m_LastSystem, m_LastIdle);
    }

    virtual ~ESCpuUsage() {}

    bool GetCpuUsage(int& _total, int& _es)
    {
        double newtime = m_Timer.Time();
        double period  = newtime - m_LastCPUCheckTime;

        if (period > 0.)
        {
            // Per-process CPU usage
            struct rusage r_usage;
            if (!getrusage(RUSAGE_SELF, &r_usage))
            {
                double utime = (double)r_usage.ru_utime.tv_sec +
                               (double)r_usage.ru_utime.tv_usec * 1e-6;
                utime += (double)r_usage.ru_stime.tv_sec +
                         (double)r_usage.ru_stime.tv_usec * 1e-6;
                _es = int((utime - m_LastESTime) * 100.0 / period);
                m_LastESTime = utime;
            }

            // System-wide CPU usage via /proc/stat
            uint64_t user, nice, system, idle;
            if (ReadProcStat(user, nice, system, idle))
            {
                uint64_t dUser   = user   - m_LastUser;
                uint64_t dNice   = nice   - m_LastNice;
                uint64_t dSystem = system - m_LastSystem;
                uint64_t dIdle   = idle   - m_LastIdle;
                uint64_t dBusy   = dUser + dNice + dSystem;
                uint64_t dTotal  = dBusy + dIdle;
                _total = (dTotal > 0) ? (int)(dBusy * 100 / dTotal) : 0;

                m_LastUser   = user;
                m_LastNice   = nice;
                m_LastSystem = system;
                m_LastIdle   = idle;
            }
        }
        else
        {
            _es    = 0;
            _total = 0;
        }

        _es    = ::Base::Math::Clamped(_es,    0, 100);
        _total = ::Base::Math::Clamped(_total, 0, 100);

        m_LastCPUCheckTime = newtime;
        return true;
    }

    bool GetAppCpuUsage(int& _es, int& _total)
    {
        return GetCpuUsage(_total, _es);
    }

    float GetGpuUsage() { return 0.f; }

    int GetNumCores()
    {
        long n = sysconf(_SC_NPROCESSORS_ONLN);
        return n > 0 ? (int)n : 1;
    }
};

#endif
