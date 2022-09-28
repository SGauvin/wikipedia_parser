#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>

template<typename T, std::size_t S>
class ThreadSafeQueueFixedSize
{
public:
    bool push(const T& val)
    {
        std::unique_lock<std::mutex> guardPop(m_mutex);
        m_dataConditionPop.wait(guardPop, [this] { return m_size < m_data.size() || m_quit == true; });
        if (m_quit == true)
        {
            return true;
        }

        auto insertIndex = (m_offset + m_size) % m_data.size();
        m_data[insertIndex] = val;
        m_size++;
        m_dataConditionPush.notify_one();

        return false;
    }

    bool pop(T& retval)
    {
        std::unique_lock<std::mutex> guardPush(m_mutex);
        m_dataConditionPush.wait(guardPush, [this] { return m_size > 0 || m_quit == true; });
        
        if (m_size > 0)
        {
            m_size--;
            retval = m_data[m_offset];
            m_offset = (m_offset + 1) % m_data.size();
            m_dataConditionPop.notify_one();
        }

        return m_quit;
    }

    bool pop(std::vector<T>& retval)
    {
        std::unique_lock<std::mutex> guardPush(m_mutex);
        m_dataConditionPush.wait(guardPush, [this, &retval] { return m_size >= retval.size() || m_quit == true; });
        if (m_quit == true)
        {
            return true;
        }

        for (auto i = 0UL; i < retval.size(); i++)
        {
            m_size--;
            retval[i] = m_data[m_offset];
            m_offset = (m_offset + 1) % m_data.size();
        }
        m_dataConditionPop.notify_all();

        return false;
    }

    void quit()
    {
        m_quit = true;
        m_dataConditionPop.notify_all();
        m_dataConditionPush.notify_all();
    }

    std::size_t size() { return m_size; }

    std::size_t capacity() { return S; }

private:
    std::atomic<bool> m_quit = false;
    std::array<T, S> m_data{};
    std::atomic<std::size_t> m_size{};
    std::size_t m_offset{};

    std::mutex m_mutex;
    std::condition_variable m_dataConditionPush;
    std::condition_variable m_dataConditionPop;
};
