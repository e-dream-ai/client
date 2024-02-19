#ifndef __BLOCKING_QUEUE__
#define __BLOCKING_QUEUE__

#include <shared_mutex>
#include <deque>

#include "boost/thread/condition.hpp"
#include "boost/thread/thread.hpp"

namespace Base
{

template <typename T> class CBlockingQueue
{
  public:
    typedef std::shared_lock<std::shared_mutex> reader_lock;
    typedef std::unique_lock<std::shared_mutex> writer_lock;

    // typedef std::scoped_lock   scoped_lock;

    CBlockingQueue() { m_maxQueueElements = 0xFFFFFFFF; }

    typename std::deque<T>::iterator begin() { return m_queue.begin(); }
    typename std::deque<T>::const_iterator begin() const
    {
        return m_queue.begin();
    }

    typename std::deque<T>::iterator end() { return m_queue.end(); }
    typename std::deque<T>::const_iterator end() const { return m_queue.end(); }

    bool push(const T& el, bool pushBack = true, bool checkMax = true)
    {
        writer_lock lock(m_mutex);

        if (checkMax && m_queue.size() == m_maxQueueElements)
        {
            while (m_queue.size() == m_maxQueueElements)
                m_fullCond.wait(lock);
        }

        if (pushBack)
            m_queue.push_back(el);
        else
            m_queue.push_front(el);

        m_emptyCond.notify_all();
        return true;
    }

    bool peek(T& el, bool wait = false, bool peekFront = true) const
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);

        if (wait)
        {
            while (m_queue.empty())
            {
                lock.unlock();
                std::unique_lock<std::shared_mutex> wlock(m_mutex);
                m_emptyCond.wait(m_mutex);
            }
        }
        else
        {
            if (m_queue.empty())
                return false;
        }

        if (peekFront)
        {
            el = m_queue.front();
            // m_queue.pop_front();
        }
        else
        {
            el = m_queue.back();
            // m_queue.pop_back();
        }

        if (m_queue.size() < m_maxQueueElements)
            m_fullCond.notify_all();

        if (m_queue.empty())
            m_nonEmptyCond.notify_all();

        return true;
    }

    void peekn(T& el, size_t n)
    {
        reader_lock lock(m_mutex);

        el = m_queue[n];
    }

    bool pop(bool wait = false, bool popFront = true)
    {
        T el;
        return pop(el, wait, popFront);
    }

    bool pop(T& el, bool wait = false, bool popFront = true)
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);

        if (wait)
        {
            while (m_queue.empty())
            {
                lock.unlock();
                std::unique_lock<std::shared_mutex> wlock(m_mutex);
                m_emptyCond.wait(m_mutex);
            }
        }
        else
        {
            if (m_queue.empty())
            {
                m_nonEmptyCond.notify_all();
                return false;
            }
        }

        lock.unlock();
        std::unique_lock<std::shared_mutex> wlock(m_mutex);

        if (popFront)
        {
            el = m_queue.front();
            m_queue.pop_front();
        }
        else
        {
            el = m_queue.back();
            m_queue.pop_back();
        }

        if (m_queue.size() < m_maxQueueElements)
            m_fullCond.notify_all();

        if (m_queue.empty())
            m_nonEmptyCond.notify_all();

        return true;
    }

    bool empty() const
    {
        reader_lock lock(m_mutex);
        return m_queue.empty();
    }

    size_t size() const
    {
        reader_lock lock(m_mutex);
        return m_queue.size();
    }

    void clear(size_t leave)
    {
        writer_lock lock(m_mutex);

        if (leave == 0)
        {
            m_queue.clear();
            m_nonEmptyCond.notify_all();
        }
        else
        {
            size_t sz = m_queue.size();

            while (sz > leave)
            {
                m_queue.pop_back();

                sz--;
            }
        }

        if (leave < m_maxQueueElements)
            m_fullCond.notify_all();
    }

    void setMaxQueueElements(size_t max)
    {
        writer_lock lock(m_mutex);

        m_maxQueueElements = max;
    }

    size_t getMaxQueueElements() const
    {
        reader_lock lock(m_mutex);
        return m_maxQueueElements;
    }

    bool waitForEmpty()
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);

        if (m_queue.empty())
            return true;

        lock.unlock();
        std::unique_lock<std::shared_mutex> wlock(m_mutex);

        m_nonEmptyCond.wait(m_mutex);

        return true;
    }

  private:
    mutable std::shared_mutex m_mutex;
    boost::condition m_fullCond;
    boost::condition m_emptyCond;
    boost::condition m_nonEmptyCond;
    size_t m_maxQueueElements;
    std::deque<T> m_queue;
};

}; // namespace Base

#endif //__BLOCKING_QUEUE__
