#ifndef _TIMER_H_

#include "base.h"

#define HIGHRES_UNIX_TIMER

namespace Base
{

//	Timer interface.
class ITimer
{
  public:
    virtual ~ITimer(){};
    virtual void Reset() = PureVirtual;
    virtual double Time() const = PureVirtual;
    virtual double Delta() = PureVirtual;
    virtual double Resolution() = PureVirtual;
    // virtual void	Wait( double seconds ) = PureVirtual;
};

}; // namespace Base

//	Don't include any of these directly, they will typedef themselves to
// CTimer.
#ifdef WIN32
#include "WTimer.h"
#else
#ifdef MAC
#include "MTimer.h"
#else
#ifdef HIGHRES_UNIX_TIMER
#include "XTimer.h"
#else
/*
namespace Base
{

class CTimer : public ITimer
{
        double		m_Time;				//	Current time in
seconds. double		m_Resolution;		//	Timer resolution in
seconds. clock_t	m_TimeCounter;		//	Time counter in clocks.
        clock_t	m_DeltaCounter;		//	Delta counter in clocks.

        public:
                        CTImer()
                        {
                                m_Resolution = 1.0 / CLOCKS_PER_SEC;
                                Reset();
                        }

                        void Reset()
                        {
                                m_Time = 0;
                                m_TimeCounter = clock();
                                m_DeltaCounter = m_TimeCounter;
                        }

                        double	Time()
                        {
                                clock_t counter = std::clock();
                                double	delta = ( counter - m_TimeCounter ) *
m_Resolution; m_TimeCounter = counter; m_Time += delta; return m_Time;
                        }

                        double	Delta()
                        {
                                clock_t counter = std::clock();
                                double delta = ( counter - m_DeltaCounter ) *
m_Resolution; m_DeltaCounter = counter; return delta;
                        }

                        double	Resolution()	{	return m_Resolution;
}

                        static void Wait( double _seconds )
                        {
                                clock_t start = std::clock();
                                clock_t finish = start + clock_t( _seconds /
m_Resolution ); while( std::clock() < finish );
                        }
};

};
*/
#endif

#endif

#endif

#define _TIMER_H_ //	Define here so we can test for include mess.
#endif
