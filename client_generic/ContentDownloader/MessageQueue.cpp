//
//  MessageQueue.cpp
//  e-dream
//
//  Created by Guillaume Louel on 30/07/2024.
//

#include "MessageQueue.h"

namespace Cache {

bool MessageQueue::QueueMessage(std::string_view _msg, double _duration) {
    std::lock_guard<std::mutex> lock(m_MessageQueueMutex);
    m_MessageQueue.emplace(std::make_shared<CMessageBody>(_msg, _duration));
    return true;
}

bool MessageQueue::PopMessage(std::string& _dst, double& _duration) {
    std::lock_guard<std::mutex> lock(m_MessageQueueMutex);
    if (!m_MessageQueue.empty()) {
        auto msg = m_MessageQueue.front();
        _dst = msg->m_Msg;
        _duration = msg->m_Duration;
        m_MessageQueue.pop();
        return true;
    }
    return false;
}

bool MessageQueue::AddOverflowMessage(const std::string& _msg) {
    std::lock_guard<std::mutex> lock(m_OverflowMessageQueueMutex);
    m_OverflowMessageQueue.emplace_back(std::make_shared<CTimedMessageBody>(_msg, 60.0));
    return true;
}

bool MessageQueue::PopOverflowMessage(std::string& _dst) {
    std::lock_guard<std::mutex> lock(m_OverflowMessageQueueMutex);
    if (m_OverflowMessageQueue.size() > 10) {
        m_OverflowMessageQueue.erase(
                                     m_OverflowMessageQueue.begin(),
                                     m_OverflowMessageQueue.begin() + static_cast<std::vector<std::shared_ptr<CTimedMessageBody>>::difference_type>(m_OverflowMessageQueue.size()) - 10
                                     );
    }
    if (!m_OverflowMessageQueue.empty()) {
        _dst.clear();
        bool del = false;
        size_t deleteStart = 0;
        size_t deleteEnd = 0;
        for (size_t i = 0; i < m_OverflowMessageQueue.size(); ++i) {
            auto& msg = m_OverflowMessageQueue[i];
            _dst += msg->m_Msg;
            if (msg->TimedOut()) {
                if (!del) {
                    deleteStart = deleteEnd = i;
                } else {
                    deleteEnd = i;
                }
                del = true;
            }
            _dst += "\n";
        }
        if (!_dst.empty()) {
            _dst.pop_back(); // Remove last newline
        }
        if (del) {
            m_OverflowMessageQueue.erase(
                                         m_OverflowMessageQueue.begin() + static_cast<std::vector<std::shared_ptr<CTimedMessageBody>>::difference_type>(deleteStart),
                                         m_OverflowMessageQueue.begin() + static_cast<std::vector<std::shared_ptr<CTimedMessageBody>>::difference_type>(deleteEnd) + 1
                                         );
        }
        return true;
    }
    return false;
}

}
