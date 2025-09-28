#include "thread_pool.h"
#include <chrono>

using namespace handy;

template <typename T>
bool SafeQueue<T>::push(T&& value)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if(m_isExited)
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
    if (waitTime_ms == 0) {
        return; // 非阻塞
    }

    auto pred = [this] { return m_isExited || !m_items.empty(); };

    if (waitTime_ms == kWaitInfinite) {
        m_condReady.wait(lock, pred);
    } else {
        auto timeout = std::chrono::steady_clock::now() + std::chrono::milliseconds(waitTime_ms);
        m_condReady.wait_until(lock, timeout, pred);
    }
}

template <typename T>
bool SafeQueue<T>::popWait(T* value, int waitTime_ms)
{
    if(value == nullptr)
        throw std::invalid_argument("SafeQueue::popWait(): value pointer is null");

    // 支持条件变量等待的锁
    std::unique_lock<std::mutex> lock(m_mutex);

    waitReady(lock, waitTime_ms);

    if(m_items.empty())
        return false;

    // 移动元素到输出参数，然后从队列中移除
    *value = std::move(m_items.front());
    m_items.pop_front();

    return true;
}

template <typename T>
size_t SafeQueue<T>::size() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_items.size();
}

template <typename T>
void SafeQueue<T>::exit()
{
    // 原子操作，确保只执行一次退出逻辑
    if(m_isExited.exchange(true))
        return;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_condReady.notify_all();
}

template <typename T>
bool SafeQueue<T>::isExited() const noexcept
{
    return m_isExited;
}

// 显示实例化任务类型的安全队列
template class SafeQueue<Task>;

ThreadPool::ThreadPool(int threadNum, int taskQueueCapacity, bool isStartImmediately)
    : m_taskQueue(static_cast<size_t>(taskQueueCapacity))
    , m_isStarted(false)
    , m_isExited(false)
{
    if(threadNum <= 0)
        throw std::invalid_argument("ThreadPool::ThreadPool(): threadNum must be greater than 0");

    // 预分配线程容器内存，避免后续扩容开销
    m_threads.reserve(static_cast<size_t>(threadNum));

    if(isStartImmediately)
        start();
}

ThreadPool::~ThreadPool()
{
    exit();
    join();

    // 输出未处理任务警告
    const size_t remainingTaskCount = m_taskQueue.size();
    if(remainingTaskCount > 0)
        WARN("ThreadPool::~ThreadPool(): %zu tasks are not processed before exit", remainingTaskCount);
}

void ThreadPool::start()
{
    if(m_isStarted.exchange(true))
        return;
    
    if(m_isExited)
        throw std::logic_error("ThreadPool::start(): thread pool is exited");

    // 创建工作线程
    const size_t threadNum = m_threads.capacity();
    for(size_t i = 0; i < threadNum; ++i)
    {
        // 直接在容器中构造线程，绑定工作线程
        m_threads.emplace_back(&ThreadPool::workerLoop, this);
    }
}

void ThreadPool::exit()
{
    if(m_isExited.exchange(true))
        return;

    // 通知任务队列退出，唤醒所有等待的工作线程
    m_taskQueue.exit();
}

void ThreadPool::join()
{
    if(!m_isStarted || !m_isExited)
        throw std::logic_error("ThreadPool::join(): thread pool is not started or not exited");

    // 逐个等待所有线程结束
    for(auto& thread : m_threads)
    {
        if(thread.joinable())
            thread.join();
    }

    m_threads.clear();
}

bool ThreadPool::addTask(Task&& task)
{
    if(m_isExited)
        return false;

    if(!m_isStarted)
        throw std::logic_error("ThreadPool::addTask(): thread pool is not started");

    return m_taskQueue.push(std::move(task));
}

bool ThreadPool::addTask(Task& task)
{
    return addTask(std::move(task));
}

size_t ThreadPool::getWaitingTaskCount() const
{
    return m_taskQueue.size();
}

bool ThreadPool::isStarted() const noexcept
{
    return m_isStarted;
}

bool ThreadPool::isExited() const noexcept
{
    return m_isExited;
}

void ThreadPool::workerLoop()
{
    // 循环直到线程池退出
    while(!m_isExited)
    {
        Task task;
        // 从队列获取任务（无限等待）
        if(m_taskQueue.popWait(&task))
        {
            try
            {
                task();
            }
            catch(const std::exception& e)
            {
                ERROR("ThreadPool::workerLoop(): task execution failed; %s", e.what());
            }
            catch(...)
            {
                ERROR("ThreadPool::workerLoop(): task execution failed; unknown exception");
            }
            
        }
    } 
}