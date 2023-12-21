#ifndef _XTIMER_H_
#define _XTIMER_H_

#ifdef _TIMER_H_
#error "XTimer.h included not from Timer.h"
#endif

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <errno.h>
#include <math.h>
#include <time.h>

// #define	USE_RDTSC

namespace Base
{
namespace internal
{
static void Wait(double _seconds)
{
    const double floorSeconds = ::floor(_seconds);
    const double fractionalSeconds = _seconds - floorSeconds;

    timespec timeOut;
    timeOut.tv_sec = static_cast<time_t>(floorSeconds);
    timeOut.tv_nsec = static_cast<long>(fractionalSeconds * 1e9);

    //	Nanosleep may return earlier than expected if there's a signal
    //	That should be handled by the calling thread.  If it happens,
    //	Sleep again. [Bramz]
    timespec timeRemaining;
    while (true)
    {
        const int32_t ret = nanosleep(&timeOut, &timeRemaining);
        if (ret == -1 && errno == EINTR)
        {
            //	There was only an sleep interruption, go back to sleep.
            timeOut.tv_sec = timeRemaining.tv_sec;
            timeOut.tv_nsec = timeRemaining.tv_nsec;
        }
        else
        {
            //	We're done, or error. =)
            return;
        }
    }
}
} // namespace internal

#ifdef USE_RDTSC

class CXTimer : public ITimer
{
    uint64_t m_Start;
    uint64_t m_DeltaStart;
    double m_Resolution;

    static inline uint64_t Tick()
    {
        //	x86_64
        // uint32_t a, d;
        //__asm__ __volatile__("rdtsc": "=a"(a), "=d"(d));
        // return (static_cast<uint64_t>(d) << 32) | static_cast<uint64_t>(a);

        //	x86_32
        uint64_t val;
        __asm__ __volatile__("rdtsc" : "=A"(val));
        return val;
    }

    static double DetermineResolution()
    {
        FILE* f = fopen("/proc/cpuinfo", "r");
        if (!f)
            return 0.0;

        const int32_t bufferSize = 256;
        char buffer[bufferSize];
        while (fgets(buffer, bufferSize, f))
        {
            float frequency;
            if (sscanf(buffer, "cpu MHz         : %f", &frequency) == 1)
            {
                fclose(f);
                return 1e-6 / static_cast<double>(frequency);
            }
        }
        fclose(f);

        return 0.0;
    }

  public:
    CXTimer() : m_Resolution(DetermineResolution()) { Reset(); }

    void Reset() { m_DeltaStart = m_Start = Tick(); }

    double Time()
    {
        const uint64_t now = Tick();
        return m_Resolution * (now - m_Start);
    }

    double Delta()
    {
        const uint64_t now = Tick();
        const double dt = m_Resolution * (now - m_DeltaStart);
        m_DeltaStart = now;
        return dt;
    }

    double Resolution() { return m_Resolution; }

    static inline void Wait(double _seconds) { internal::Wait(_seconds); }
};

#else

class CXTimer : public ITimer
{
    double m_Start;
    double m_DeltaStart;

    static inline double realTime()
    {
        timespec time;
        if (clock_gettime(CLOCK_REALTIME, &time) != 0)
            return 0.0;

        return time.tv_sec + time.tv_nsec * 1e-9;
    }

  public:
    CXTimer() { Reset(); }

    void Reset() { m_DeltaStart = m_Start = realTime(); }

    double Time() { return realTime() - m_Start; }

    double Delta()
    {
        const double now = realTime();
        const double dt = now - m_DeltaStart;
        m_DeltaStart = now;
        return dt;
    }

    double Resolution()
    {
        timespec res;
        if (clock_getres(CLOCK_REALTIME, &res) != 0)
            return 0.0;

        return res.tv_sec + res.tv_nsec * 1e-9;
    }

    static inline void Wait(double _seconds) { internal::Wait(_seconds); }
};

#endif

typedef CXTimer CTimer;

}; // namespace Base

#endif
