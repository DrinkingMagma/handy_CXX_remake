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
    /**
     * @brief 事件派发器内部实现结构体（Pimpl模式，隐藏EventBase的具体逻辑）
    */
    struct EventsImp
    {
        PollerBase* m_poller;       // I/O多路复用器（epoll/kqueue）
        EventBase* m_base;          // 关联的EventBase对象（非空）
        std::atomic<bool> m_exit; // 事件循环退出标志（原子操作，线程安全）
        int m_wakeupFds[2];          // 唤醒事件循环的管道（0：读端，1：写端）
        int m_nextTimeout_ms;          // 下一个定时器的超时时间（毫秒，用于Poller等待）
        SafeQueue<Task> m_tasks;    // 异步任务队列（线程安全，支持跨线程投递）

        std::map<TimerId, TimerRepeatable> m_timerReps;     // 可重复定时器映射
        std::map<TimerId, Task> m_timers;                   // 一次性任务定时器映射
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
            : m_poller(createPoller())
            , m_base(base)
            , m_exit(false)
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
            TRACE("Ready to delete m_poller");
            delete m_poller;
            TRACE("Rm_poller have been deleted");

            ::close(m_wakeupFds[0]);
            ::close(m_wakeupFds[1]);
            TRACE("readFd=%d, writeFd=%d closed", m_wakeupFds[0], m_wakeupFds[1]);

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
                ssize_t r = wakeupCh->getFd() >= 0 ? ::read(wakeupCh->getFd(), buf, sizeof(buf)) : 0;
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
         * @return IdleId 空闲连接ID的智能指针（用于后续更新/注销）
        */
        IdleId registerIdle(int idle_s, const TcpConnPtr& conn, const TcpCallBack& cb)
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
            return IdleId(new IdleIdImp(&connList, --connList.end()));
        }

        /**
         * @brief 注销空闲连接（从空闲管理中移除）
         * @param idleIdPtr 要注销的空闲连接ID指针（非空）
        */
        void unregisterIdle(const IdleId& idleIdPtr)
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
        void updateIdle(const IdleId& idleIdPtr)
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
            TimerId maxTimerId{now_ms, std::numeric_limits<int64_t>::max()};

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
        void refreshNearestTimer(const TimerId* tip = nullptr)
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
         * @brief 取消定时器（支持一次性/周期性定时器）
         * @param timerIdPair 要取消的定时器ID
         * @return bool true: 成功取消定时器，false: 定时器不存在
        */
        bool cancel(TimerId timerIdPair)
        {
            // 处理周期性定时器
            if(timerIdPair.first < 0)
            {
                auto repIt = m_timerReps.find(timerIdPair);
                if(repIt == m_timerReps.end())
                    return false;

                // 移除当前周期的一次性定时器
                m_timers.erase(repIt->second.timerIdPair);
                // 移除周期性定时器的管理信息
                m_timerReps.erase(repIt);
                refreshNearestTimer();
                return true;
            }

            // 处理一次性定时器
            auto timerIt = m_timers.find(timerIdPair);
            if(timerIt == m_timers.end())
                return false;

            m_timers.erase(timerIt);
            refreshNearestTimer();
            return true;
        }

        /**
         * @brief 注册定时器（支持一次性/周期性定时器）
         * @param timestamp_ms 定时器超时时间戳（毫秒）
         * @param task 定时器回调函数（非空）
         * @param interval_ms 定时器间隔时间戳（毫秒；0：一次性定时器，>0：周期性定时器）
         * @return TimerId 定时器ID（事件循环已退出时返回无效的定时器ID）
        */
        TimerId runAt(int64_t timestamp_ms, Task&& task, int64_t interval_ms)
        {
            // 已退出或任务为空，返回无效ID
            if(m_exit || !task)
                return TimerId();

            // 处理可重复定时器
            if(interval_ms > 0)
            {
                // 生成可重复定时器的自定义ID（first为负数，区分于一次性定时器）
                TimerId rep{-timestamp_ms, ++m_timerSeq};
                auto [it, inserted] = m_timerReps.emplace(
                    rep, TimerRepeatable{
                        timestamp_ms, interval_ms, {timestamp_ms, ++m_timerSeq}, std::move(task)});
                if(!inserted)
                    return TimerId();

                TimerRepeatable* tr = &it->second;
                // 注册当前周期的一次性定时器
                m_timers[tr->timerIdPair] = [this, tr]() { repeatableTimeout(tr); };
                refreshNearestTimer(&tr->timerIdPair);

                TRACE("Repeatable timer registered: repTimerIdPair={%lld, %lld}, interval=%lld ms",
                    rep.first, rep.second, interval_ms);
                return rep;
            }

            TimerId tid{timestamp_ms, ++m_timerSeq};
            m_timers.emplace(tid, std::move(task));
            refreshNearestTimer(&tid);

            TRACE("One-shot timer registered: timerIdPair={%lld, %lld}",
                tid.first, tid.second);
            return tid;
        }

        /**
         * @brief 启动事件循环（阻塞，直到next()被调用）
        */
        void loop()
        {
            TRACE("EventBase loop started: base=%p", m_base);
            // 每次最多等待10秒，避免永久阻塞
            while(!m_exit)
                loopOnce(10000);
            TRACE("EventBase loop exited: base=%p", m_base);

            // 清理资源
            m_timerReps.clear();
            m_timers.clear();
            m_idleConns.clear();

            // 执行最后一次循环，清理剩余连接
            loopOnce(0);
        }

        /**
         * @brief 执行一次事件循环
         * @param waitTime_ms 最大等待时间（毫秒）
        */
        void loopOnce(int waitTime_ms)
        {
            // 等待I/O事件（最多等待m_nextTimeout_ms，避免错过定时器）
            int autualWaitTime_ms = std::min(waitTime_ms, m_nextTimeout_ms);
            m_poller->loopOnce(autualWaitTime_ms);

            // 处理已超时的定时器
            TRACE("Ready to handle timeout timers");
            handleTimeoutTimers();
            TRACE("Timeout timers handled");
        }

        /**
         * @brief 唤醒事件循环（向唤醒管道中写入数据）
        */
        void wakeup()
        {
            char dummy = '\0';
            ssize_t r = ::write(m_wakeupFds[1], &dummy, 1);
            if(r != 1)
                ERROR("write wakeup pipe error: r=%zd, errno=%d, msg=%s", r, errno, strerror(errno));
            TRACE("write wakeup pipe(%d) success", m_wakeupFds[1]);
        }
    };

    EventBase::EventBase(int taskCapacity)
    {
        try
        {
            // 创建事件派发器内部实现
            m_imp = std::make_unique<EventsImp>(this, taskCapacity);
            m_imp->init();
        }
        catch(const std::exception& e)
        {
            FATAL("EventBase create failed: %s", e.what());
            // 重新抛出，让调用者感知错误
            throw;
        }
    }

    EventBase::~EventBase()
    {
        TRACE("EventBase destroying: base=%p", this);
        // unique_ptr析构时会自动调用delete释放m_imp，无需手动处理
    }

    void EventBase::loopOnce(int waitTime_ms)
    {
        if(m_imp)
            m_imp->loopOnce(waitTime_ms);
    }

    void EventBase::loop()
    {
        if(m_imp)
            m_imp->loop();
    }

    bool EventBase::cancel(TimerId timerIdPair)
    {
        return m_imp ? m_imp->cancel(timerIdPair) : false;
    }

    TimerId EventBase::runAt(int64_t timestamp_ms, Task&& task, int64_t interval_ms)
    {
        return m_imp ? m_imp->runAt(timestamp_ms, std::move(task), interval_ms) : TimerId();
    }

    EventBase& EventBase::exit()
    {
        if(m_imp)
        {
            m_imp->m_exit = true;
            m_imp->wakeup();
        }
        return *this;
    }

    bool EventBase::exited()
    {
        return m_imp ? m_imp->m_exit.load() : true;
    }

    void EventBase::wakeup()
    {
        if(m_imp)
            m_imp->wakeup();
    }

    void EventBase::safeCall(Task&& task)
    {
        if(m_imp && task)
        {
            m_imp->m_tasks.push(std::move(task));
            m_imp->wakeup();
        }
    }
    PollerBase* EventBase::getPoller() const
    {
        return m_imp ? m_imp->m_poller : nullptr;
    }

    MultiBase::MultiBase(int sz)
        : m_id(0)
        , m_bases(sz > 0 ? sz : 1)
        , m_threads(m_bases.size() - 1) 
    {
        if(sz <= 0)
            WARN("MultiBase size=%d is invalid, using default size=1", sz);
    }

    MultiBase::~MultiBase()
    {
        // 确保所有子线程安全退出
        exit();
        for(auto& th : m_threads)
        {
            if(th.joinable())
                th.join();
        }
    }

    void MultiBase::loop()
    {
        // 启动子线程
        for(size_t i = 0; i < m_threads.size(); ++i)
        {
            m_threads[i] = std::thread([this, i]()
            {
                m_bases[i].loop();
            });
        }

        // 主线程运行最后一个EventBase
        m_bases.back().loop();

        // 等待所有子线程退出
        for(auto& th : m_threads)
        {
            if(th.joinable())
                th.join();
        }
    }

    MultiBase& MultiBase::exit()
    {
        for(auto& base : m_bases)
            base.exit();
        return *this;
    }

    EventBase* MultiBase::allocBase()
    {
        // 轮询分配EventBase（原子操作保证线程安全）
        int idx = m_id++ % m_bases.size();
        return &m_bases[idx];
    }

    Channel::Channel(EventBase* base, int fd, int events)
        :  m_base(base)
        , m_fd(fd)
        , m_events(static_cast<short>(events))
    {
        if(!base || fd < 0)
            throw std::invalid_argument("Channel create failed: base is null or fd is invalid");

        // 设置fd为非阻塞模式（I/O多路复用需非阻塞fd）
        if(!Net::setNonBlock(m_fd))
        {
            throw std::runtime_error(
                utils::format("Channel set non-block failed: fd=%d, errno=%d, msg=%s",
                m_fd, errno, strerror(errno))
            );
        }
        TRACE("Channel set non-block: fd=%d success", fd);

        // 生成全局唯一ID
        static std::atomic<int64_t>globalId(0);
        m_id = ++globalId;

        // 获取Polller并注册事件
        m_poller = m_base->getPoller();
        m_poller->addChannel(this);

        TRACE("Channel created: id=%lld, fd=%d, events=0x%x",
            m_id, m_fd, m_events);
    }

    Channel::~Channel()
    {
        close();
        TRACE("Channel destroyed: id=%lld, fd=%d", m_id, m_fd); 
    }

    void Channel::close()
    {
        if(m_fd >= 0)
        {
            TRACE("Channel closing: id=%lld, fd=%d", m_id, m_fd);

            // 删除Polller中的事件并关闭fd
            m_poller->removeChannel(this);
            if(::close(m_fd) < 0)
            {
                WARN("Channel close failed: fd=%d, errno=%d, msg=%s",
                    m_fd, errno, strerror(errno));
            }
            TRACE("Channel closed: fd=%d", m_fd);
            m_fd = -1;

            // 处理剩余的读事件（避免数据残留）
            handleRead();
        }
    }

    void Channel::enableRead(bool enable)
    {
        if(enable)
            m_events |= kReadEvent;
        else
            m_events &= ~kReadEvent;
        m_poller->updateChannel(this);
        TRACE("Channel enableRead: id=%lld, fd=%d, events=0x%x",
            m_id, m_fd, m_events);
    }

    void Channel::enableWrite(bool enable)
    {
        if(enable)
            m_events |= kWriteEvent;
        else
            m_events &= ~kWriteEvent;
        m_poller->updateChannel(this);
        TRACE("Channel enableWrite: id=%lld, fd=%d, events=0x%x",
            m_id, m_fd, m_events);
    }

    void Channel::enableReadWrite(bool readEnable, bool writeEnable)
    {
        enableRead(readEnable);
        enableWrite(writeEnable);
    }

    bool Channel::isReadEnabled() const
    {
        return m_events & kReadEvent;
    }

    bool Channel::isWritable() const
    {
        return m_events & kWriteEvent;
    }

    void handleUnregisterIdle(EventBase* base, const IdleId& idleIdPtr)
    {
        if(base && idleIdPtr)
            base->getImp()->unregisterIdle(idleIdPtr);
    }

    void handleUpdateIdle(EventBase* base, const IdleId& idleIdPtr)
    {
        if(base && idleIdPtr)
            base->getImp()->updateIdle(idleIdPtr);
    }

    void TcpConn::addIdleCB(int idle_ms, const TcpCallBack& cb)
    {
        if(m_channel && getBase())
        {
            m_idleIds.push_back(
                getBase()->getImp()->registerIdle(idle_ms, shared_from_this(), cb)
            );
        }   
    }

    void TcpConn::_reconnect()
    {
        auto conn = shared_from_this();
        EventBase* base = getBase();
        if(!base)
        {
            ERROR("TcpConn::_reconnect: base is null");
            return;
        }

        // 添加到重连集合中
        {
            std::lock_guard<std::mutex> lock(base->getImp()->m_reconnectMutex);
            base->getImp()->m_reconnectConns.insert(conn);
        }

        // 计算重连间隔
        int64_t now_ms = utils::timeMilli();
        int64_t interval = m_reconnectInterval_ms - (now_ms - m_connectedTime_ms);
        interval = std::max(interval, 0L);

        INFO("TcpConn reconnect scheduled: interval=%lld ms", interval);

        // 注册重连任务
        base->runAfter(interval, [this, conn, base]() {
            // 从重连任务中移除
            {
                std::lock_guard<std::mutex> lock(base->getImp()->m_reconnectMutex);
                base->getImp()->m_reconnectConns.erase(conn);
            }

            // 执行重连
            _connect(base, m_destHost, static_cast<unsigned short>(m_destPort), m_connectTimeout_ms, m_localIp);
        });

        // 清理当前的Channel
        Channel* ch = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_ChannelMutex);
            ch = m_channel;
            m_channel = nullptr;
        }
        delete ch;
    }
}   // namespace handy