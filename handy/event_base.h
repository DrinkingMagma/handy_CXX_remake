/**
 * @file event_base.h
 * @brief 事件驱动框架核心头文件，包含事件循环、定时器、通道管理等核心组件
*/
#pragma once
#include "handy-imp.h"
#include "poller.h"
#include "utils.h"

namespace handy
{
    typedef std::shared_ptr<TcpConn> TcpConnPtr;   // TCP连接指针
    typedef std::shared_ptr<TCPServer> TcpServerPtr; // TCP服务器指针（管理服务器生命周期）
    typedef std::function<void(const TcpConnPtr&)> TcpCallBack; // TCP连接相关回调（如连接建立/关闭）
    typedef std::function<void(const TcpConnPtr&, Slice)> MsgCallBack;  // 消息处理回调（接受连接与消息切片）
    typedef std::function<void()> Task; // 通用任务回调（无参数无返回值，用于异步任务/事件处理

    /**
     * @class EventBases
     * @brief 事件派发器抽象基类（定义多事件派发器的统一接口）
    */
    class EventBases : private NonCopyAble
    {
        public:
            /**
             * @brief 分配事件派发器
            */
            virtual EventBase* allocBase() = 0;

            virtual ~EventBases() = default;
    };

    /**
     * @class EventBase
     * @brief 单线程事件派发器，管理定时器、I/O事件、异步任务
     * @note 1. 一个EventBase对应一个事件循环线程，非线程安全接口需在循环线程内调用
     *       2. 任务队列支持有界模式（构造时指定容量，0表示无界）
    */
    class EventBase : public EventBases
    {
        public:
            /**
             * @brief 构造函数，初始化事件派发器
             * @param taskCapacity 异步任务队列容量（0表示无界）
            */
            explicit EventBase(int taskCapacity = 0);

            ~EventBase() override;

            /**
             * @brief 执行一次事件循环
             * @param waitTime_ms 等待事件的超时时间（毫秒），-1表示无限等待
             * @note 1. 等待I/O事件
             * @note 2. 处理所有已超时的定时器任务
            */
            void loopOnce(int waitTime_ms);

            /**
             * @brief 启动事件循环（阻塞，直到调用exit()退出）
             * @note 循环调用loopOnce(10000)，每次最多等待10秒，避免永久阻塞
            */
            void loop();

            /**
             * @brief 取消定时器
             * @param timerIdpair 要取消的定时器ID
             * @return bool true：取消成功，false：定时器不存在/已过期
            */
            bool cancel(TimerId timerIdpair);

            /**
             * @brief 在执行时间戳执行任务（支持周期性）
             * @param timestamp_ms 任务执行的时间戳（毫秒级，从epoch开始计算）
             * @param task 要执行的任务（右值引用）
             * @param interval_ms 任务重复执行间隔（毫秒级，0表示不重复）
             * @return TimerId 定时器ID（用于后续取消），事件循环已退出时返回无效ID
            */
            TimerId runAt(int64_t timestamp_ms, Task&& task, int64_t interval_ms = 0);

            /**
             * @brief 在执行时间戳执行任务（支持周期性）
             * @param timestamp_ms 任务执行的时间戳（毫秒级，从epoch开始计算）
             * @param task 要执行的任务（左值引用）
             * @param interval_ms 任务重复执行周期（毫秒级，00表示不重复执行）
             * @return TimerId 定时器ID（用于后续取消），事件循环已退出时返回无效ID
            */
            TimerId runAt(int64_t timestamp_ms,const Task& task, int64_t interval_ms = 0)
            {
                return runAt(timestamp_ms, Task(task), interval_ms);
            };

            /**
             * @brief 在指定延迟后执行任务（支持周期性）
             * @param timestamp_ms 延迟时间（毫秒）
             * @param task 要执行的任务(右值引用)
             * @param interval_ms 任务执行间隔（毫秒），为0则不执行周期性
             * @return TimerId 定时器ID（用于后续取消），事件循环已退出时返回无效ID
            */
            TimerId runAfter(int64_t timestamp_ms, Task&& task, int64_t interval_ms = 0)
            {
                return runAt(utils::timeMilli() + timestamp_ms, std::move(task), interval_ms);
            }

            /**
             * @brief 在指定延迟时间后执行任务（支持周期性）
             * @param timestamp_ms 延迟时间（毫秒）
             * @param task 任务（左值引用）
             * @param interval_ms 周期性执行间隔（毫秒），为0则不执行周期性
             * @return TimerId 定时器ID（用于后续取消），事件循环已退出时返回无效ID
            */
            TimerId runAfter(int64_t timestamp_ms, const Task& task, int64_t interval_ms = 0)
            {
                return runAt(utils::timeMilli() + timestamp_ms, Task(task), interval_ms);
            }

            /**
             * @brief 退出事件循环（线程安全）
             * @return EventBase& 返回自身引用
             * @details 设置退出标志，唤醒事件循环，确保loop()快速退出
            */
            EventBase& exit();

            /**
             * @brief 检查事件循环是否已经退出（线程安全）
             * @return bool true:已退出，false:未退出
            */
            bool exited();

            /**
             * @brief 唤醒事件循环（线程安全）
             * @details 向唤醒管道写入数据，触发Poller返回，打破loopOnce()的阻塞
            */
            void wakeup();

            /**
             * @brief 投递异步任务（线程安全，右值引用）
             * @param task 要投递的任务（加入任务队列，由事件循环线程执行）
             * @details 任务投递后唤醒事件循环，确保任务及时执行
            */
            void safeCall(Task&& task);

            /**
             * @brief 投递异步任务（线程安全，左值引用）
             * @param task 要投递的任务（加入任务队列，由事件循环线程执行）
            */
            void safeCall(const Task& task)
            {
                safeCall(Task(task));
            }

            /**
             * @brief 分配事件派发器（返回自身，单线程场景使用）
             * @return EventBase* 指向当前对象的指针（非空）
            */
            EventBase* allocBase() override
            {
                return this;
            }

            /**
             * @brief 获取poller
            */
            PollerBase* getPoller() const;

            /**
             * @brief 获取EventsImp对象指针
            */
            EventsImp* getImp() const
            {
                return m_imp.get();
            }
        private:
            std::unique_ptr<EventsImp> m_imp; // 事件派发器内部实现对象

            friend struct EventsImp; // 允许Pimpl实现类访问主类私有成员，实现内部协作
            friend class TcpConn;   // 允许TCP连接类直接操作事件派发器内部状态，避免暴露底层接口
    };

    /**
     * @class MultiBase
     * @brief 多线程事件派发器（管理多个EventBase，实现负载均衡）
     * @details 1. 内部维护多个EventBase实例，每个对应一个事件循环线程
     *          2. 通过轮询算法分配EventBase，实现任务的负载均衡
     *          3. 支持批量控制所有事件循环（如批量退出）
     * @note 线程安全：allocBase()、ecit()为线程安全接口，loop()须在主线程调用
    */
    class MultiBase : public EventBases
    {
        public:
            /**
             * @brief 初始化多线程事件派发器
             * @param sz EventBase数量（即事件循环线程数，sz <= 0时，sz = 1）
            */
            explicit MultiBase(int sz);

            ~MultiBase() override;

            /**
             * @brief 启动所有事件循环线程（阻塞，直到所有线程退出）
            */
            void loop();

            /**
             * @brief 退出所有事件循环线程（线程安全）
             * @return MutliBase& 返回自身引用
            */
            MultiBase& exit();

            /**
             * @brief 分配事件派发器（轮询方式，负载均衡）
             * @return EventBase* 分配的EventBase指针（非空）
            */
            EventBase* allocBase() override;
        private:
            std::atomic<int> m_id;  // 计数器，用于轮询分配EventBase
            std::vector<EventBase> m_bases; // 存储所有EventBase对象
            std::vector<std::thread> m_threads; // 存储所有事件循环线程(大小为m_bases.size() - 1，主线程运行最后一个EventBase)
    };

    /**
     * @class Channel
     * @brief I/O事件通道类（封装文件描述符与事件处理逻辑）
     * @details 1. 关联一个文件描述符fd和一个事件派发器EventBase
     *          2. 支持注册读/写事件回调，启用/禁用事件监听
     *          3. 自动管理fd的生命周期，析构时关闭fd并清理事件
     * @note 1. 不支持多Channel共享fd
     * @note 2. 所有接口需在EventBase对应的循环线程内调用（非线程安全）
    */
    class Channel : private NonCopyAble
    {
        public:
            /**
             * @brief 初始化I/O事件通道
             * @param base 关联的事件派发器
             * @param fd 关联的文件描述符
             * @param events 初始监听的事件类型
             * @throw std::runtime_error 设置为非阻塞模式失败
            */
            Channel(EventBase* base, int fd, int events);

            /**
             * @brief 关闭fd并清理事件
            */
            ~Channel();

            /**
             * @brief 获取关联的事件派发器
             * @return EventBase* 关联的EventBase指针（非空）
            */
            EventBase* getBase() const
            {
                return m_base;
            }

            /**
             * @brief 获取关联的文件描述符
             * @return int 关联的文件描述符（非负，-1表示已关闭）
            */
            int getFd() const
            {
                return m_fd;
            }

            /**
             * @brief 获取通道的唯一ID
             * @return int64_t 通道唯一ID（全局原子生成， 由静态原子变量生成）
            */
            int64_t getId() const
            {
                return m_id;
            }

            /**
             * @brief 获取当前关注的事件
             * @return short 事件掩码
            */
            short getEvents() const
            {
                return m_events;
            }

            /**
             * @brief 关闭通道（关闭fd并从poller中删除）
            */
            void close();

            /**
             * @brief 注册读事件回调函数（右值引用）
             * @param readcb 读事件触发时执行的回调（非空）
            */
            void onRead(Task&& readcb)
            {
                m_readCB = std::move(readcb);
            }

            /**
             * @brief 注册读事件回调函数（左值引用）
             * @param readcb 读事件触发时执行的回调（非空）
            */
            void onRead(const Task& readcb)
            {
                m_readCB = readcb;
            }

            /**
             * @brief 注册写事件回调函数（右值引用）
             * @param writecb 写事件触发时执行的回调（非空）
            */
            void onWrite(Task&& writecb)
            {
                m_writeCB = std::move(writecb);
            }

            /**
             * @brief 注册写事件回调函数（左值引用）
             * @param writecb 写事件触发时执行的回调（非空）
            */
            void onWrite(const Task& writecb)
            {
                m_writeCB = writecb;
            }

            /**
             * @brief 启用/禁用读事件监听
             * @param enable true: 启用；false: 禁用
            */
            void enableRead(bool enable);

            /**
             * @brief 启用/禁用写事件监听
             * @param enable true: 启用；false: 禁用
            */
            void enableWrite(bool enable);

            /**
             * @brief 同时控制读/写事件监听
             * @param readEnable true: 启用读事件监听；false: 禁用读事件监听
             * @param writeEnable true: 启用写事件监听；false: 禁用写事件监听
            */
            void enableReadWrite(bool readEnable, bool writeEnable);

            /**
             * @brief 检查是否启用读事件监听
             * @return bool true: 已启用；false: 未启用
            */
            bool isReadEnabled() const;

            /**
             * @brief 检查是否启用写事件监听
             * @return bool true: 已启用；false: 未启用
            */
            bool isWritable() const;

            /**
             * @brief 处理读事件（调用注册的读事件回调函数）
             * @note 仅在Poller检测到可读事件时调用，需确保m_readcb非空
            */
            void handleRead()
            {
                if(m_readCB)
                    m_readCB();
            }

            /**
             * @brief 处理写事件（调用注册的写事件回调函数）
             * @note 仅在Poller检测到可写事件时调用，需确保m_writecb非空
            */
            void handleWrite()
            {
                if(m_writeCB)
                    m_writeCB();
            }

        private:
            EventBase* m_base;      // 关联的事件派发器（非空）
            PollerBase* m_poller;   // 关联的轮询器（从EventBase中获取）
            int m_fd;               // 关联的文件描述符（非负，-1标识已关闭）
            short m_events;         // 当前关注的事件掩码（EPOLLIN/EPOLLOUT等）
            int64_t m_id;           // 通道唯一ID（全局原子生成）
            Task m_readCB;          // 读事件回调
            Task m_writeCB;         // 写事件回调
            Task m_errorcb;         // 错误事件回调

            friend class PollerEpoll;
            friend class PollerKqueue;
    };

    /**
     * @brief 注销空闲连接（委托给EventBase实现）
     * @param base 关联的事件派发器（非空）
     * @param idleIdPtr 要注销的空闲连接（非空）
    */
    void handleUnregisterIdle(EventBase* base, const IdleId& idleIdPtr);

    /**
     * @brief 更新空闲连接状态（委托给EventBase实现）
     * @param base 关联的事件派发器（非空）
     * @param idleId 要更新的空闲连接（非空）
     * @details 重置空闲连接的最后活跃事件，避免被判定为超时
    */
    void handleUpdateIdle(EventBase* base, const IdleId& idleIdPtr);
} // namespace handy
