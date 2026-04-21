#ifndef _WTIMER_H_
#define _WTIMER_H_

#ifdef _TIMER_H_
#error "WTimer.h included not from Timer.h"
#endif
#include <cstdint>
#include <Windows.h>

namespace Base
{

/*
        CWTimer.
*/
class CWTimer : public ITimer
{
    double m_Time;          //	Current time in seconds.
    int64_t m_TimeCounter;  //	Raw 64bit timer counter for time.
    int64_t m_DeltaCounter; //	Raw 64bit timer counter for delta.
    int64_t m_Frequency;    //	Raw 64bit timer frequency.

  public:
    CWTimer()
    {
        QueryPerformanceFrequency((LARGE_INTEGER*)&m_Frequency);
        Reset();
    }

    void Reset()
    {
        QueryPerformanceCounter((LARGE_INTEGER*)&m_TimeCounter);
        m_DeltaCounter = m_TimeCounter;
        m_Time = 0;
    }
    double Time() const 
    {
        int64_t counter;
        QueryPerformanceCounter((LARGE_INTEGER*)&counter);
        const_cast<CWTimer*>(this)->m_Time +=
            (double)(counter - m_TimeCounter) / (double)m_Frequency;
        const_cast<CWTimer*>(this)->m_TimeCounter = counter;
        return m_Time;
    }
    // TODO: somewhere this got changed to const, make sure this still works under Windows
    /* double Time() 
    {
        int64_t counter;
        QueryPerformanceCounter((LARGE_INTEGER*)&counter);
        m_Time += (double)(counter - m_TimeCounter) / (double)m_Frequency;
        m_TimeCounter = counter;
        return m_Time;
    }*/

    double Delta()
    {
        int64_t counter;
        QueryPerformanceCounter((LARGE_INTEGER*)&counter);
        m_DeltaCounter = counter;
        return (double)(counter - m_DeltaCounter) / (double)m_Frequency;
    }

    double Resolution() { return 1.0 / (double)m_Frequency; }

    static void Wait(const double _seconds) { Sleep(int32_t(_seconds * 1000)); }
};

typedef CWTimer CTimer;

}; // namespace Base

#endif
