#include "threads.h"

using namespace handy;

template <typename T>
bool SafeQueue<T>::push(T&& value)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if(isExited)
        return false;

    if(m_capacity > 0 && m_items.size() >= m_capacity)
        return false;

    m_items.push_back(std::move(value));

    // 唤醒一个等待的消费者线程
    m_condReady.notify_one();
    return true;
}

template <typename T>
void SafeQueue<T>::waitReady(std::unique_lock<std::mutex>& lock, int waitTime_ms)
{
    // 当队列未退出且未空时才需要等待
    while(!m_isExited && m_items.empty())
    {
        if(waitTime_ms == kWaitInfinite)
        {
            // 无限等待，直到被唤醒且条件满足
            m_condReady.wait(lock, [this]{return isExited || !m_items.empty();});
        }
        else if(waitTime_ms > 0)
        {
            // 有限等待计算绝对超时时间点
            const auto timeout = chrono::steady_clock::now() + chrono::milliseconds(waitTime_ms);
            // 等待超时或被换唤醒
            const auto waitResult = m_condReady.wait_until(lock, timeout);
            bool ready = m_condReady.wait_until(lock, timeout, 
                [this] { return m_isExited || !m_items.empty(); });
            if (!ready)
                break; // 超时退出等待
        }
        else 
            break; // 非阻塞模式，直接退出
    }
}