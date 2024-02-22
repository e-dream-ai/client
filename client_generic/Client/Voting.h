#ifndef _VOTING_H_
#define _VOTING_H_

#include <string>
#include <string_view>

#include "BlockingQueue.h"
#include "Timer.h"
#include "base.h"

/*
        CVote.
        Small class to handle voting of sheep.
*/
class CVote
{
    typedef struct
    {
        std::string uuid;
        uint8_t vtype;
    } VotingInfo;

    boost::thread* m_pThread;
    Base::CTimer m_Timer;

    Base::CBlockingQueue<VotingInfo> m_Votings;

    void ThreadFunc();

    double m_Clock;

  public:
    CVote();
    virtual ~CVote();

    bool Vote(std::string_view _uuid, const uint8_t _type,
              const float _duration);
};

#endif
