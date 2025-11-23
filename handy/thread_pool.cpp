/**
 * @file thread_pool.cpp
 * @brief 线程池实现，包含安全队列和线程池核心逻辑，支持任务并发执行、动态启停和优雅退出
 * @details 
 *  该文件实现了两个核心组件：
 *  1. 安全队列（SafeQueue）：
 *     - 基于std::deque实现，提供线程安全的任务存储；
 *     - 支持任务入队（push）、出队等待（popWait）、队列退出（exit）等操作；
 *     - 通过std::mutex保证临界区互斥访问，std::condition_variable实现线程间同步；
 *     - 支持队列容量限制，入队时若容量满则返回失败。
 *  2. 线程池（ThreadPool）：
 *     - 管理一组工作线程，循环从安全队列中获取任务并执行；
 *     - 支持线程池动态启动（start）、退出（exit）和等待（join）；
 *     - 提供任务添加接口（addTask），支持右值引用传递任务以提高性能；
 *     - 工作线程执行任务时捕获异常，避免单个任务崩溃导致整个线程池异常；
 *     - 析构函数自动执行退出和等待操作，确保资源优雅释放。
 * @note 
 *  1. 线程安全：所有公共接口均通过互斥锁或原子操作保证线程安全，可在多线程环境中放心使用；
 *  2. 任务类型：任务需是可调用对象（如函数指针、lambda表达式、std::function），无返回值；
 *  3. 异常处理：工作线程会捕获任务执行过程中的所有异常（std::exception及其子类），并记录错误日志；
 *  4. 优雅退出：调用exit()后，线程池不再接受新任务，已入队任务会继续执行完成，所有工作线程退出后join()返回；
 *  5. 使用限制：线程池启动后才能添加任务，退出后不能再启动或添加任务，否则会抛出异常或返回失败。
 */

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
    // 先检查当前线程池是否退出
    if(m_isExited)
        throw std::logic_error("ThreadPool::start(): thread pool is exited");

    // 再检查是否已启动，确保只启动一次
    if(m_isStarted.exchange(true))
        return;

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
    // 检查线程池状态是否合法
    if (!m_isStarted)
    {
        WARN("ThreadPool::join(): thread pool is not started");
        return;
    }
    
    if (!m_isExited)
    {
        WARN("ThreadPool::join(): thread pool is not exited");
        return;
    }

    // 逐个等待所有线程结束
    for (auto& thread : m_threads)
    {
        if (thread.joinable())
        {
            try {
                thread.join();
            } catch (const std::system_error& e) {
                // 捕获join()可能抛出的系统错误（如线程已被join）
                WARN("ThreadPool::join(): failed to join thread: %s", e.what());
            }
        }
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