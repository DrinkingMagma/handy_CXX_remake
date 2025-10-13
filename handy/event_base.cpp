#include "event_base.h"
#include "conn.h"
#include "logger.h"
#include "poller.h"
#include "thread_pool.h"
#include "conn.h"
#include <map>
#include <set>
#include <fcntl.h>

namespace handy
{
    // 内部结构体定义（匿名命名空间，隐藏实现）
    namespace 
    {
        // 可重复定时器结构体（存储重读定时器的核心信息）
        struct TimerRepeatable
        {
            int64_t at;             // 下一次超时时间戳（毫秒）
            int64_t interval_ms;    // 定时器重复间隔（毫秒）
            TimerIdPair timerIdPair;        // 当前周期的定时器ID（用于取消）
            Task task;              // 定时器触发时执行的任务（回调函数）
        };

        // 空闲连接节点结构体（存储单个空闲连接的管理信息）
        struct IdleNode
        {
            TcpConnPtr conn;        // 空闲连接的智能指针（需确保连接不被提前释放）
            int64_t lastUpdatedTimestamp_s;  // 最后一次活跃时间戳（单位：秒）
            TcpCallBack cb;         // 空闲超时触发的回调函数（如关闭连接/发送心跳）
        };
    } // namespace 
    
    /**
     * @brief 空闲连接ID实现结构体（封装空闲连接的列表迭代器）
    */
    struct IdleIdImp
    {
        // 空闲连接列表迭代器类型
        using Iter = std::list<IdleNode>::iterator;

        /**
         * @brief 构造函数，初始化空闲连接ID
         * @param lst 空闲连接列表指针（非空，生命周期需长于IdleIdImp）
         * @param iter 列表中的迭代器（指向具体的空闲连接节点）
        */
        IdleIdImp(std::list<IdleNode>* lst, Iter iter)
            : m_lst(lst), m_iter(iter) {}

        std::list<IdleNode>* m_lst;     // 空闲连接列表指针
        Iter m_iter;                    // 列表迭代器(指向当前空闲的连接节点)
    };

    /**
     * @brief 事件派发器内部实现结构体（Pimpl模式，隐藏EventBase的具体逻辑）
    */
    struct EventsImp
    {
        EventBase* m_base;          // 关联的EventBase对象（非空）
        PollerBase* m_poller;       // I/O多路复用器（epoll/kqueue）
        std::atomic<bool> m_exited; // 事件循环退出标志（原子操作，线程安全）
        int m_wakeupFds[2];          // 唤醒事件循环的管道（0：读端，1：写端）
        int m_nextTimeout_ms;          // 下一个定时器的超时时间（毫秒，用于Poller等待）
        SafeQueue<Task> m_tasks;    // 异步任务队列（线程安全，支持跨线程投递）

        std::map<TimerIdPair, TimerRepeatable> m_timerReps;     // 可重复定时器映射
        std::map<TimerIdPair, Task> m_timers;                   // 一次性任务定时器映射
        std::atomic<int64_t> m_timerSeq;                    // 定时器序列号（用于生成定时器ID）

        std::map<int, std::list<IdleNode>> m_idleConns;     // 空闲连接映射（key：空闲超时时间，单位:s；list按照最后活跃时间排序，最早超时的连接在前面）
        std::set<TcpConnPtr> m_reconnectConns;              // 重连连接集合（需要互斥锁保护）
        bool m_idleEnabled;                                 // 空闲连接管理的启用标志
        std::mutex m_reconnectMutex;                        // 重连连接集合的互斥锁

        /**
         * @brief 构造函数：初始化时间派发器内部实现
         * @param base 关联的EventBase对象（非空）
         * @param taskCap 异步任务队列容量（0： 无容量限制）
         * @throw std::runtime_error 唤醒管道创建失败
        */
        EventsImp(EventBase* base, int taskCap)
            : m_base(base)
            , m_poller(createPoller())
            , m_exited(false)
            , m_tasks(taskCap)
            , m_timerSeq(0)
            , m_idleEnabled(false)
        {
            // 创建唤醒管道（用于跨线程唤醒事件循环）
            int r = pipe(m_wakeupFds);
            if(r < 0)
            {
                throw::std::runtime_error(utils::format(
                    "Wakeup pipe create failed: errno=%d, msg=%s",
                    errno, strerror(errno)));
            }

            // 设置管道为非阻塞+FD_CLOEXEC（避免进程替换后残留）
            Net::setNonBlock(m_wakeupFds[0]);
            Net::setNonBlock(m_wakeupFds[1]);
            utils::addFdFlag(m_wakeupFds[0], FD_CLOEXEC);
            utils::addFdFlag(m_wakeupFds[1], FD_CLOEXEC);

            TRACE("Wakeup pipe initialized: readFd=%d, writeFd=%d",
                m_wakeupFds[0], m_wakeupFds[1]);
        }

        /**
         * @brief 析构函数：释放资源
        */
        ~EventsImp()
        {
            ::close(m_wakeupFds[0]);
            ::close(m_wakeupFds[1]);

            delete m_poller;

            std::lock_guard<std::mutex> lock(m_reconnectMutex);
            for(const auto& conn : m_reconnectConns)
                conn->cleanup(conn);
        }

        /**
         * @brief 初始化：注册唤醒管道的读事件
         * @details 创建唤醒管道的Channel，处理异步任务投递
        */
        void init()
        {
            // 创建唤醒管道的Channel（关注读事件）
            Channel* wakeupCh = new Channel(m_base, m_wakeupFds[0], kReadEvent);
            wakeupCh->onRead([this, wakeupCh]()
            {
                char buf[1024];
                // 读取唤醒管道数据（清空管道，避免重复唤醒)
                ssize_t r = ::read(m_wakeupFds[0], buf, sizeof(buf));
                if(r > 0)
                {
                    // 处理所有异步任务（捕获异常，避免单个任务崩溃影响循环）
                    Task task;
                    while (m_tasks.popWait(&task, 0))
                    {
                        try
                        {
                            task();
                        }
                        catch(const std::exception& e)
                        {
                            ERROR("async task execute failed: %s", e.what());
                        }
                    }
                }
                // 管道写端关闭，删除Channel，避免野指针
                else if(r == 0)
                {
                    delete wakeupCh;
                }
                // 非中断/阻塞错误，记录致命错误
                else if(errno != EINTR && errno != EAGAIN)
                {
                    FATAL("Wakeup channel read error: r=%d, errno=%d, msg=%s",
                        r, errno, strerror(errno));
                }
            });
        }

        /**
         * @brief 定期见检查所有注册的空闲连接，处理空闲连接超时（遍历所有空闲连接，触发超时回调）
        */
        void callIdles()
        {
            if(!m_idleEnabled)
                return;

            int64_t now_s = utils::timeMilli() / 1000;
            for(auto& [idle_s, connList] : m_idleConns)
            {
                while(!connList.empty())
                {
                    IdleNode& node = connList.front();
                    // 若没超时，退出当前循环
                    if(node.lastUpdatedTimestamp_s + idle_s > now_s)
                        break;
                    
                    // 更新节点时间并移动到列表末尾
                    node.lastUpdatedTimestamp_s = now_s;
                    connList.splice(connList.end(), connList, connList.begin());

                    // 触发空闲回调
                    try
                    {
                        node.cb(node.conn);
                    }
                    catch(const std::exception& e)
                    {
                        ERROR("idle connection callback failed: %s", e.what());
                    }
                    
                }
            }

            // 重现注册下一次空闲检查（周期性执行，1s一次）
            m_base->runAfter(1000, [this]()
            {
                callIdles();
            });
        }

        /**
         * @brief 注册空闲连接（加入空闲连接管理）
         * @param idle_s 空闲超时时间（单位：秒）
         * @param conn 要管理的TCP连接（非空）
         * @param cb 空闲超时回调（非空）
         * @return IdleIdPtr 空闲连接ID的智能指针（用于后续更新/注销）
        */
        IdleIdPtr registerIdle(int idle_s, const TcpConnPtr& conn, const TcpCallBack& cb)
        {
            if(idle_s <= 0 || !conn || !cb)
                throw std::invalid_argument(
                    "registerIdle: invalid parameter (idle_s <= 0 or conn/cb is null)");
            
            // 首次注册时启用空闲连接管理（启动周期性检查）
            if(!m_idleEnabled)
            {
                m_base->runAfter(1000, [this]() { callIdles(); });
                m_idleEnabled = true;
            }

            // 添加空闲连接到对应列表
            auto& connList = m_idleConns[idle_s];
            connList.push_back({conn, utils::timeMilli() / 1000, cb});

            TRACE("Idle connection registered: idle_s=%d", idle_s);
            return IdleIdPtr(new IdleIdImp(&connList, --connList.end()));
        }

        /**
         * @brief 注销空闲连接（从空闲管理中移除）
         * @param idleIdPtr 要注销的空闲连接ID指针（非空）
        */
        void unregisterIdle(const IdleIdPtr& idleIdPtr)
        {
            if(!idleIdPtr)
                return;

            // 从当前连接列表中移除对应的空闲连接节点
            idleIdPtr->m_lst->erase(idleIdPtr->m_iter);
            TRACE("Idle connection unregistered");
        }

        /**
         * @brief 更新空闲连接状态（重置最后活跃时间）
         * @param idleIdPtr 要更新的空闲连接ID指针（非空）
         * @note 更新后会将连接放入空闲连接列表的末尾
        */
        void updateIdle(const IdleIdPtr& idleIdPtr)
        {
            if(!idleIdPtr)
                return;

            idleIdPtr->m_iter->lastUpdatedTimestamp_s = utils::timeMilli() / 1000;
            idleIdPtr->m_lst->splice(idleIdPtr->m_lst->end(), *idleIdPtr->m_lst, idleIdPtr->m_iter);

            TRACE("Idle connection updated: updateTime=%lld", idleIdPtr->m_iter->lastUpdatedTimestamp_s);
        }

        /**
         * @brief 处理定时器超时（执行所有已超时的定时器任务）
        */
        void handleTimeoutTimers()
        {
            int64_t now_ms = utils::timeMilli();
            TimerIdPair maxTimerId{now_ms, std::numeric_limits<int64_t>::max()};

            // 处理一次性定时器（按时间戳排序，遍历已超时的任务）
            auto it = m_timers.begin();
            while(it != m_timers.end() && it->first < maxTimerId)
            {
                Task task = std::move(it->second);
                m_timers.erase(it++);

                // 执行定时器任务
                try
                {
                    task();
                }
                catch(const std::exception& e)
                {
                    ERROR("One-shot timer callback failed: %s", e.what());
                }
            }

            // 刷新下一个定时器的超时时间（用于Poller等待）
            refreshNearestTimer();
        }

        /**
         * @brief 刷新下一个定时器的超时时间
         * @param  tip 可选参数：新添加的定时器ID，优化刷新逻辑
        */
        void refreshNearestTimer(const TimerIdPair* tip = nullptr)
        {
            if(m_timers.empty())
            {
                // 无定时器，设置超时时间为最大值
                m_nextTimeout_ms = 1 << 30;
                return;
            }

            // 获取最早超时的定时器
            const auto& earlistTimerIdPair = m_timers.begin()->first;
            int64_t now_ms = utils::timeMilli();
            m_nextTimeout_ms = std::max(earlistTimerIdPair.first - now_ms, int64_t{0});

            TRACE("Nearest timer refreshed: m_nextTimeout=%d ms", m_nextTimeout_ms);
        }

        /**
         * @brief 处理可重复定时器超时（更新时间并重新注册）
         * @param tr 可重复器定时器ID指针（非空，需确保在m_timerReps中存在）
        */
        void repeatableTimeout(TimerRepeatable* tr)
        {
            if(!tr)
                return;

            // 通过自定义ID检查定时器是否已被取消
            auto repIt = m_timerReps.find({-tr->at, tr->timerIdPair.second});
            if(repIt == m_timerReps.end())
                return;

            // 更新下一次超时时间并重新注册
            tr->at += tr->interval_ms;
            tr->timerIdPair = {tr->at, ++m_timerSeq};
            m_timers[tr->timerIdPair] = [this, tr](){ repeatableTimeout(tr); };

            // 刷新下一个定时器时间
            refreshNearestTimer(&tr->timerIdPair);

            // 执行定时器回调
            try
            {
                tr->task();
            }
            catch(const std::exception& e)
            {
                ERROR("repeatable timer callback failed: %s", e.what());
            }
        }

        /**
         * @brief 注册定时器（支持一次性/周期性定时器）
         * @param timestamp_ms 定时器超时时间戳（毫秒）
         * @param task 定时器回调函数（非空）
         * @param interval_ms 定时器间隔时间戳（毫秒；0：一次性定时器，>0：周期性定时器）
         * @return TimerIdPair 定时器ID（事件循环已退出时返回无效的定时器ID）
        */
        TimerIdPair runAt(int64_t timestamp_ms, Task&& task, int64_t interval_ms)
        {
            // 已退出或任务为空，返回无效ID
            if(m_exited || !task)
                return TimerIdPair();

            // 处理可重复定时器
            if(interval_ms > 0)
            {
                // 生成可重复定时器的自定义ID（first为负数，区分于一次性定时器）
                TimerIdPair rep{-timestamp_ms, ++m_timerSeq};
                auto [it, inserted] = m_timerReps.emplace(
                    rep, TimerRepeatable{
                        timestamp_ms, interval_ms, {timestamp_ms, ++m_timerSeq}, std::move(task)});
                if(!inserted)
                    return TimerIdPair();

                TimerRepeatable* tr = &it->second;
                // 注册当前周期的一次性定时器
                m_timers[tr->timerIdPair] = [this, tr]() { repeatableTimeout(tr); };
                refreshNearestTimer(&tr->timerIdPair);

                TRACE("Repeatable timer registered: repTimerIdPair={%lld, %lld}, interval=%lld ms",
                    rep.first, rep.second, interval_ms);
                return rep;
            }

            TimerIdPair tid{timestamp_ms, ++m_timerSeq};
            m_timers.emplace(tid, std::move(task));
            refreshNearestTimer(&tid);

            TRACE("One-shot timer registered: timerIdPair={%lld, %lld}",
                tid.first, tid.second);
            return tid;
        }
    };
}   // namespace handy