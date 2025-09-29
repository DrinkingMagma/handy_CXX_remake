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
    typedef std::shared_ptr<TcpConn> tcpConnPtr;   // TCP连接指针
    typedef std::shared_ptr<TCPServer> tCPServerPtr; // TCP服务器指针（管理服务器生命周期）
    typedef std::function<void(const tcpConnPtr&)> tcpCallBack; // TCP连接相关回调（如连接建立/关闭）
    typedef std::function<void(const tcpConnPtr&, Slice msg)> msgCallBack;  // 消息处理回调（接受连接与消息切片）
    typedef std::function<void()> task; // 通用任务回调（无参数无返回值，用于异步任务/事件处理）
    typedef std::pair<int64_t, int64_t> TimerId; // 定时任务ID类型。唯一标识一个定时器
    typedef std::unique_ptr<IdleIdImp> IdleId;   // 管理空闲连接的唯一标识

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
             * @param timerId 要取消的定时器ID
             * @return bool true：取消成功，false：定时器不存在/已过期
            */
            bool cancel(TimerId timerId);

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

        private:
            std::unique_ptr<EventsImp> m_imp; // 事件派发器内部实现对象

            friend struct EventsImp; // 允许Pimpl实现类访问主类私有成员，实现内部协作
            friend class TcpConn;   // 允许TCP连接类直接操作事件派发器内部状态，避免暴露底层接口
    };
} // namespace handy
