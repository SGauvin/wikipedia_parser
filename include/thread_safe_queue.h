#pragma once

#include <array>
#include <mutex>
#include <condition_variable>

template <typename T, std::size_t S>
class ThreadSafeQueue
{
public:
    void pop(T& retval) {
        std::unique_lock<std::mutex> guardPush(m_mutex);
        m_dataConditionPush.wait(guardPush, [this]{ return m_size > 0; });
        m_size--;
        retval = m_data[m_offset];
        m_offset = (m_offset + 1) % m_data.size();
        m_dataConditionPop.notify_one();
    }

    void push(const T& val)
    {
        std::unique_lock<std::mutex> guardPop(m_mutex);
        m_dataConditionPop.wait(guardPop, [this]{ return m_size < m_data.size(); });
        auto insertIndex = (m_offset + m_size) % m_data.size();
        m_data[insertIndex] = val;
        m_size++;
        m_dataConditionPush.notify_one();
    }

    std::size_t size()
    {
        return m_size;
    }

    std::size_t capacity()
    {
        return m_data.capacity;
    }

private:
    std::array<T, S> m_data{};
    std::size_t m_size{};
    std::size_t m_offset{};

    std::mutex m_mutex;
    std::condition_variable m_dataConditionPush;
    std::condition_variable m_dataConditionPop;
};
