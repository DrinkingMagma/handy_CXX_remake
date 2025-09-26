/**
 * @file threads.h
 * @brief 线程安全队列（SafeQueue）与线程池（ThreadPool）的实现
 * @details 1. 提供高并发场景下的任务管理能力，支持线程安全的任务存储、分发与执行
 *          2. 包含完善的错误处理、资源管理与状态控制机制，适用于多线程环境下的任务调度
*/

#pragma once
#include "utils.h"
#include "logger.h"
#include <mutex>
#include "non_copy_able.h"

namespace handy 
{
    /**
     * @class SafeQueue
     * @brief 线程安全的队列容器（模版类）
     * @tparam T 队列存储的元素类型（需支持移动语义以优化性能）
     * @details 1. 基于std::list实现，通过互斥锁与条件变量保证线程安全
     *          2. 支持有限容量限制、超时等待取元素、优雅退出等功能
     *          3. 禁止拷贝与移动以避免线程安全风险
     * @note 适用于多生产者多消费者模型，元素存储采用链表结构，频繁删除效率高
    */
    template <typename T>
    class SafeQueue : private NonCopyAble
    {
        public:
            // 无限等待时间标识（编译器常量）
            // 当popWait函数的waitTime_ms参数为此值时，表示无限等待直到队列就绪(有元素/退出)
            static constexpr int kWaitInfinite = std::numeric_limits<int>::max();

            /**
             * @brief 构造函数(初始化队列容量)
             * @param capacity 队列容量限制（0表示不限制,默认为0）
             * @throw std::invalid_argument 当capacity小于0或超出size_t的最大值时抛出
            */
            explicit SafeQueue(size_t capacity = 0);

            /**
             * @brief 析构函数(优雅释放资源)
             * @details 1. 自动调用Exit()唤醒所有等待线程,避免线程泄露
             *          2. 采用noexecpt修饰,确保析构过程不抛出异常
            */
            ~SafeQueue() noexcept;

            /**
             * @brief 禁止移动构造
             * @details 队列涉及线程同步资源,移动操作可能导致锁状态异常
            */
            SafeQueue(SafeQueue&&) = delete;

            /**
             * @brief 禁止移动赋值
            */
            SafeQueue& operator=(SafeQueue&&) = delete;

            /**
             * @brief 向队列尾部添加元素(线程安全)
             * @param value 待添加的元素(右值引用,支持移动语义以减少拷贝开销)
             * @return bool 添加成功返回true,队列已满/队列已退出返回false
             * @details 1. 加锁后检查队列状态,符合条件则移动元素到队列,并唤醒一个等待的消费者线程
            */
            bool push(T&& value);

            /**
             * @brief 从队列头部取出元素(支持超时等待,线程安全)
             * @param[out] value 存储取出的元素的指针(必须非空,否则抛出异常)
             * @param waitTime_ms 等待时间（毫秒）,默认为kWaitInfinite(无限等待)
             * @return bool 取出成功返回true,超时/队列空/队列已退出返回false
             * @throw std::invalid_argument 当value为空指针时抛出
            */
            bool popWait(T* value, int waitTime_ms = kWaitInfinite);

            /**
             * @brief 获取当前队列中的元素数量(线程安全)
             * @return size_t 队列中的元素数量
            */
            size_t size() const;

            /**
             * @brief 通知队列优雅退出(线程安全)
            */
            void exit();

            /**
             * @brief 查询队列是否已退出(线程安全)
             * @return bool 队列已退出返回true,否则返回false
            */
            bool isExited() const noexcept;

        private:
            // 互斥锁（保护队列操作的线程安全）
            // 采用mutable修饰，允许const成员函数加锁
            mutable std::mutex m_mutex;

            // 条件变量（用于线程间的同步通知）
            std::condition_variable m_condReady;

            // 存储元素的链表容器（适合频繁添加/取出元素）
            std::list<T> m_items;

            // 队列容量限制（0表示不限制）
            const size_t m_capacity;

            // 队列退出标志（原子变量）
            std::atomic<bool> m_isExited;

            /**
             * @brief 等待队列就绪
             * @param[in, out] lock 已加锁的unique_lock对象（支持临时释放锁）
             * @param waitTime_ms 等待时间（毫秒）
             * @details 1. 循环检查队列状态（避免虚假唤醒），根据wati_ms选择无限等待或超时等待
             *          2. 直到队列有元素、队列退出或超时等待
            */
            void waitReady(std::unique_lock<std::mutex>& lock, int waitTime_ms);
    };

    // 任务类型定义(函数对象)
    // 线程池执行的任务需符合此类型,可封装普通函数/lambda表达式/函数指针或具有operator()的类对象
    using Task = std::function<void()>;

    // SafeQueue<Task>的显式实例化声明
    // 避免模版在多个编译单元中重复实例化,减少编译时间与二进制体积
    extern template class SafeQueue<Task>;

    /**
     * @class ThreadPool
     * @brief 线程池类(管理一组线程执行任务)
     * @brief 1. 基于SafeQueue实现任务队列,支持线程的启动/退出/等待以及任务的安全添加,包含完善的状态检查与错误处理
     *        2. 适用于高并发任务调度场景(如网络服务/批量计算)
     * @note 1. 禁止拷贝与移动,确保线程资源的唯一管理
     *       2. 任务执行异常不会导致线程退出
    */
    class ThreadPool : private NonCopyAble
    {
        public:

        private:
            // 任务队列（存储待执行的任务）
            // 复用SafeQueue<Task>实现线程安全的任务存储与分发
            SafeQueue<Task> m_taskQueue;

            // 存储线程对象的容器
            // 构造上时预分配内存，避免后续扩容导致的线程对象拷贝
            std::vector<std::thread> m_threads;

            
    };
} // namespace handy