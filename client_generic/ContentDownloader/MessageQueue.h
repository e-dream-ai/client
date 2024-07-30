//
//  MessageQueue.hpp
//  e-dream
//
//  Created by Guillaume Louel on 30/07/2024.
//

#pragma once

#include <queue>
#include <vector>
#include <string>
#include <mutex>
#include <memory>
#include "Timer.h"

namespace Cache {

class CMessageBody {
public:
    CMessageBody(std::string_view _str, double _duration)
    : m_Msg(_str), m_Duration(_duration) {}
    
    std::string m_Msg;
    double m_Duration;
};

class CTimedMessageBody {
public:
    CTimedMessageBody(const std::string& _str, double _duration)
    : m_Msg(_str), m_Duration(_duration), m_Timer() {}
    
    bool TimedOut() const { return m_Timer.Time() > m_Duration; }
    
    Base::CTimer m_Timer;
    std::string m_Msg;
    double m_Duration;
};

class MessageQueue {
public:
    bool QueueMessage(std::string_view _msg, double _duration);
    bool PopMessage(std::string& _dst, double& _duration);
    
    bool AddOverflowMessage(const std::string& _msg);
    bool PopOverflowMessage(std::string& _dst);
    
private:
    std::queue<std::shared_ptr<CMessageBody>> m_MessageQueue;
    std::mutex m_MessageQueueMutex;
    
    std::vector<std::shared_ptr<CTimedMessageBody>> m_OverflowMessageQueue;
    std::mutex m_OverflowMessageQueueMutex;
};

}
