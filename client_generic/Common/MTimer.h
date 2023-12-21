#ifndef _MTIMER_H_
#define _MTIMER_H_

#ifdef _TIMER_H_
#error "MTimer.h included not from Timer.h"
#endif

#include <errno.h>
#include <math.h>
#include <mach/mach_time.h>

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

class CMTimer : public ITimer
{
    double m_Start;
    double m_DeltaStart;

    static inline double realTime()
    {

        mach_timebase_info_data_t tTBI;

        mach_timebase_info(&tTBI);
        double cv = (static_cast<double>(tTBI.numer)) /
                    (static_cast<double>(tTBI.denom));

        uint64_t now = mach_absolute_time();
        double tNS = now * cv;

        return tNS * 1e-9;
    }

  public:
    CMTimer() { Reset(); }

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
        mach_timebase_info_data_t tTBI;

        mach_timebase_info(&tTBI);
        double cv = (static_cast<double>(tTBI.numer)) /
                    (static_cast<double>(tTBI.denom));

        return cv * 1e-9;
    }

    static inline void Wait(double _seconds) { Base::internal::Wait(_seconds); }
};

typedef CMTimer CTimer;

}; // namespace Base

#endif
