#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>

template<typename T>
class ThreadSafeQueue
{
public:
    void push(const T& someValue)
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_dataQueue.push(someValue);
        m_dataCondition.notify_one();
    }

    void pop(T& retVal)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_dataCondition.wait(lock, [this] { return !m_dataQueue.empty(); });
        retVal = m_dataQueue.front();
        m_dataQueue.pop();
    }

    std::size_t size()
    {
        return m_dataQueue.size();
    }

private:
    std::mutex m_mutex;
    std::queue<T> m_dataQueue;
    std::condition_variable m_dataCondition;
};

template<typename T, std::size_t S>
class ThreadSafeQueueFixedSize
{
public:
    void push(const T& val)
    {
        std::unique_lock<std::mutex> guardPop(m_mutex);
        m_dataConditionPop.wait(guardPop, [this] { return m_size < m_data.size(); });
        auto insertIndex = (m_offset + m_size) % m_data.size();
        m_data[insertIndex] = val;
        m_size++;
        m_dataConditionPush.notify_one();
    }

    void pop(T& retval)
    {
        std::unique_lock<std::mutex> guardPush(m_mutex);
        m_dataConditionPush.wait(guardPush, [this] { return m_size > 0; });
        m_size--;
        retval = m_data[m_offset];
        m_offset = (m_offset + 1) % m_data.size();
        m_dataConditionPop.notify_one();
    }

    std::size_t size() { return m_size; }

    std::size_t capacity() { return m_data.capacity; }

private:
    std::array<T, S> m_data{};
    std::size_t m_size{};
    std::size_t m_offset{};

    std::mutex m_mutex;
    std::condition_variable m_dataConditionPush;
    std::condition_variable m_dataConditionPop;
};
