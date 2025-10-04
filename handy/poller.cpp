#include "poller.h"
#include "event_base.h"
#include "logger.h"
#include "current_os.h"
#include <set>

#ifdef OS_LINUX
#include <sys/epoll.h>
#elif defined(OS_MACOSX)
#include <sys/event.h>
#else
#error "platform unsupported"
#endif

namespace handy
{
    #ifdef OS_LINUX
        /**
         * @brief Linux平台基于epoll实现的事件轮询器
         * @details 1. 封装epoll系统调用，实现I/O事件的监控与处理
         *          2. 维护活跃Channel集合，确保事件操作的线程安全
         *          3. 遵循PollerBase接口规范，屏蔽epoll底层细节
        */
        class PollerEpoll : public PollerBase
        {
            public:
                /**
                 * @brief 构造函数，创建epoll实例，初始化活跃事件数组
                 * @throw std::runtime_error 若epoll_create失败则抛出此异常
                */
                PollerEpoll();

                /**
                 * @brief 析构函数，关闭epoll实例，释放所有活跃的Channel资源
                */
                ~PollerEpoll() override;

                /**
                 * @brief 添加Channel到epoll监控
                 * @param ch 待添加的Channel对象指针（非空）
                 * @throw std::invalid_argument 若ch为空则抛出此异常
                 * @throw std::runtime_error 若epoll_ctl(EPOLL_CTL_ADD)失败则抛出此异常
                */
                void addChannel(Channel* ch) override;

                /**
                 * @brief 从epoll监控中移除Channel
                 * @param ch 待移除的Channel对象指针（非空）
                 * @throw std::invalid_argument 若ch为空则抛出此异常
                */
                void removeChannel(Channel* ch) override;

                /**
                 * @brief 更新Channel的epoll监控事件
                 * @param ch 待更新的Channel对象指针（非空）
                 * @throw std::invalid_argument 若ch为空则抛出此异常
                 * @throw std::runtime_error 若epoll_ctl(EPOLL_CTL_MOD)失败则抛出此异常
                */
                void updateChannel(Channel* ch) override;

                /**
                 * @brief 执行一次epoll轮询
                 * @param waitTime_ms 轮询等待时间，单位ms，-1表示无限等待
                 * @return int 本次轮询触发的活跃事件数量
                 * @throw std::runtime_error 若epoll_wait失败则抛出此异常（信号中断除外）0
                */
                int loopOnce(int waitTime_ms) override;


            private:
                int m_epollFd;                           // epoll实例文件描述符
                std::set<Channel*> m_liveChannels;   // 当前活跃Channel集合（需线程安全保护）
                std::mutex m_channelMutex;          // 线程安全保护
                struct epoll_event m_activeEvs[kMaxEvents]; // 活跃事件数组

        };

        PollerEpoll::PollerEpoll()
        {
            // EPOLL_CLOEXEC: 当进程执行exec()系列函数时，该实例会被自动关闭
            m_epollFd = epoll_create1(EPOLL_CLOEXEC);
            if(m_epollFd < 0)
            {
                throw std::runtime_error(
                    utils::format("poller.cpp::PollerEpoll::PollerEpoll()::epoll_create1() failed: errno=%d, msg=%s",
                                    errno, strerror(errno)));
            }
            INFO("PollerEpoll::PollerEpoll(): PollerEpoll[%lld] created, epoll_fd=%d",
                    static_cast<long long>(getId()), m_epollFd);
        }

        PollerEpoll::~PollerEpoll()
        {
            INFO("PollerEpoll::~PollerEpoll(): PollerEpoll[%lld] destroying, epoll_fd=%d",
                    static_cast<long long>(getId()), m_epollFd);
            
            std::lock_guard<std::mutex> lock(m_channelMutex);
            // 遍历并关闭所有活跃的Channel
            while(!m_liveChannels.empty())
            {
                Channel* ch = *m_liveChannels.begin();
                ch->close(); // 触发Channel自身的资源释放
                m_liveChannels.erase(ch);
            }

            // 关闭epoll实例
            if(::close(m_epollFd) < 0)
            {
                WARN("PollerEpoll::~PollerEpoll(): PollerEpoll[%lld] close epoll_fd=%d failed: errno=%d, msg=%s",
                    static_cast<long long>(getId()), m_epollFd, errno, strerror(errno));
            }

            INFO("PollerEpoll::~PollerEpoll(): PollerEpoll[%lld] destroyed", static_cast<long long>(getId()));
        }

        void PollerEpoll::addChannel(Channel* ch)
        {
            if(ch == nullptr)
                throw std::invalid_argument("poller.cpp::PollerEpoll::addChannel(): ch is null"); 

            // 初始化epoll_event结构体
            struct epoll_event ev;
            memset(&ev, 0, sizeof(ev));
            ev.events = ch->getEvents();
            ev.data.ptr = ch;   // 存储Channel指针，便于事件触发时回调

            TRACE("PollerEpoll::addChannel(): PollerEpoll[%lld] add Channel[%lld], fd=%d, events=0x%x",
                    static_cast<long long>(getId()), static_cast<long long>(ch->getId()), ch->getFd(), ev.events);

            int ret = epoll_ctl(m_epollFd, EPOLL_CTL_ADD, ch->getFd(), &ev);
            if(ret != 0)
            {
                throw std::runtime_error(
                    utils::format("poller.cpp::PollerEpoll::addChannel(): PollerEpoll[%lld] epoll_ctl(EPOLL_CTL_ADD) failed, fd=%d, errno=%d, msg=%s",
                        static_cast<long long>(getId()), ch->getFd(), errno, strerror(errno))
                );
            }

            std::lock_guard<std::mutex> lock(m_channelMutex);
            m_liveChannels.insert(ch);
        }

        void PollerEpoll::removeChannel(Channel* ch)
        {
            if(ch == nullptr)
                throw std::invalid_argument("poller.cpp::PollerEpoll::removeChannel: ch is nullptr");

            TRACE("PollerEpoll::removeChannel(): PollerEpoll[%lld] remove Channel[%lld], fd=%d",
                    static_cast<long long>(getId()), static_cast<long long>(ch->getId()), ch->getFd());

            // ps: epoll 无需显式删除已关闭的 fd（fd 关闭后会自动从 epoll 中移除）
            std::lock_guard<std::mutex> lock(m_channelMutex);
            m_liveChannels.erase(ch);

            // 清理活跃事件数组中残留的Channel指针，避免野指针
            for(int i = m_lastActive; i >= 0; --i)
            {
                if(m_activeEvs[i].data.ptr == ch)
                {
                    m_activeEvs[i].data.ptr = nullptr;
                    break;
                }
            }
        }

        void PollerEpoll::updateChannel(Channel* ch)
        {
            if(ch == nullptr)
                throw std::invalid_argument("poller.cpp::PollerEpoll::updateChannel(): ch is nullptr");
            
            struct epoll_event ev;
            memset(&ev, 0, sizeof(ev));
            ev.events = ch->getEvents();
            ev.data.ptr = ch;

            TRACE("PollerEpoll::updateChannel(): PollerEpoll[%lld] update Channel[%lld], fd=%d, read=%d, write=%d",
                    static_cast<long long>(getId()), 
                    static_cast<long long>(ch->getId()), 
                    ch->getFd(),
                    (ev.events & kReadEvent) ? 1 : 0,
                    (ev.events & kWriteEvent) ? 1 : 0);

            int ret = epoll_ctl(m_epollFd, EPOLL_CTL_MOD, ch->getFd(), &ev);
            if(ret != 0)
            {
                throw std::runtime_error(
                    utils::format("poller.cpp::PollerEpoll::updateChannel(): PollerEpoll[%lld] epoll_ctl(EPOLL_CTL_MOD) failed: fd=%d, errno=%d, mes=%s",
                                    static_cast<long long>(getId()), ch->getFd(), errno, strerror(errno))
                );
            }
        }

        int PollerEpoll::loopOnce(int waitTime_ms)
        {
            // 记录轮询开始时间
            const int64_t  startTime_ms = utils::timeMilli();

            // 等待事件
            m_lastActive = epoll_wait(m_epollFd, m_activeEvs, kMaxEvents, waitTime_ms);
            const int64_t usedTime_ms = utils::timeMilli() - startTime_ms;

            TRACE("poller.cpp::PollerEpoll::loopOnce(): PollerEpoll[%lld] epoll_wait, waitTime_ms=%d, return=%d, usedTime_ms=%d, errno=%d",
                    static_cast<long long>(getId()), waitTime_ms, m_lastActive, usedTime_ms, errno);

            // 错误处理：排除信号中断（EINTR是正常情况）
            if(m_lastActive < 0 && errno != EINTR)
            {
                throw std::runtime_error(
                    utils::format("poller.cpp::PollerEpoll::loopOnce(): PollerEpoll[%lld] epoll_wait failed, waitTime_ms=%d, usedTime_ms=%d, errno=%d, error=%s",
                                    static_cast<long long>(getId()), waitTime_ms, usedTime_ms, errno, strerror(errno))
                );
            }

            // 处理活跃事件
            for(int i = m_lastActive - 1; i >= 0; --i)
            {
                struct epoll_event& ev = m_activeEvs[i];
                Channel* ch = static_cast<Channel*>(ev.data.ptr);

                if(ch == nullptr)
                    continue; // 已被移除的Channel，跳过

                // 处理读事件（包含错误事件POLLERR
                if(ev.events & (kReadEvent | POLLERR))
                {
                    TRACE("PollerEpoll::loopOnce(): PollerEpoll[%lld] handle read: Channel[%lld], fd=%d",
                            static_cast<long long>(getId()),
                            static_cast<long long>(ch->getId()),
                            ch->getFd());
                    ch->handleRead();
                }
                else if (ev.events & kWriteEvent)
                {
                    TRACE("PollerEpoll::loopOnce(): PollerEpoll[%lld] handle write: Channel[%lld], fd=%d",
                            static_cast<long long>(getId()),
                            static_cast<long long>(ch->getId()),
                            ch->getFd());
                    ch->handleWrite();
                }
                else
                {
                    FATAL("PollerEpoll::loopOnce(): PollerEpoll[%lld] unexpected event: Channel[%lld], fd=%d, events=0x%x",
                            static_cast<long long>(getId()),
                            static_cast<long long>(ch->getId()),
                            ch->getFd(),
                            ev.events);
                    throw std::runtime_error("poller.cpp::PollerEpoll::loopOnce(): unexpected event type");
                }
            }
            return m_lastActive;
        }

    #elif defined(OS_MACOSX)

        /**
         * @brief macOS平台基于kqueue实现的事件轮询器
         * @details 1. 封装kqueue系统调用，实现I/O事件的监控与处理
         *          2. 维护活跃Channel集合，确保事件操作的线程安全
         *          3. 遵循PollerBase接口规范，屏蔽kqueue底层细节
        */
        class PollerKqueue : public PollerBase
        {
            public:
                /**
                 * @brief 构造函数，创建kqueue实例，初始化活跃事件数组
                 * @throw std::runtime_error 创建kqueue实例失败，则抛出此异常
                */
                PollerKqueue();

                /**
                 * @brief 析构函数，关闭kqueue实例，释放所有创建的Channel资源
                */
                ~PollerKqueue();

                /**
                 * @brief 添加 Channel 到 kqueue 监控
                 * @param ch 待添加的 Channel 对象指针（非空）
                 * @throw std::invalid_argument 若 ch 为空指针
                 * @throw std::runtime_error 若 kevent 调用失败
                 */
                void addChannel(Channel* ch) override;

                /**
                 * @brief 从 kqueue 中移除 Channel
                 * @param ch 待移除的 Channel 对象指针（非空）
                 * @throw std::invalid_argument 若 ch 为空指针
                 */
                void removeChannel(Channel* ch) override;

                /**
                 * @brief 更新 Channel 的 kqueue 监控事件
                 * @param ch 待更新的 Channel 对象指针（非空）
                 * @throw std::invalid_argument 若 ch 为空指针
                 * @throw std::runtime_error 若 kevent 调用失败
                 */
                void updateChannel(Channel* ch) override;

                /**
                 * @brief 执行一次 kqueue 轮询
                 * @param wait_ms 超时时间（毫秒），-1 表示无限等待
                 * @return 本次轮询触发的活跃事件数量
                 * @throw std::runtime_error 若 kevent 调用失败（信号中断除外）
                 */
                int loopOnce(int wait_ms) override;   

            private:
                int m_kqueueFd;                         // kqueue实例句柄
                std::set<Channel*> m_activeChannels;    // 当前活跃Channel集合（需线程安全保护）
                std::mutex m_channelMutex;              // 线程安全保护
                struct kevent m_activeEvs[kMaxEvents];  // 活跃事件数组
        }

        PollerKqueue::PollerKqueue() {
            // 创建 kqueue 实例
            m_kqueueFd = kqueue();
            if (m_kqueueFd < 0) {
                throw std::runtime_error(
                    utils::format("poller.cpp::PollerKqueue::PollerKqueue(): kqueue failed: errno=%d, msg=%s", 
                                errno, strerror(errno)));
            }
            INFO("PollerKqueue[%lld] created, kqueue_fd=%d", 
                static_cast<long long>(getId()), m_kqueueFd);
        }

        // 实现：PollerKqueue 析构函数
        PollerKqueue::~PollerKqueue() {
            INFO("PollerKqueue[%lld] destroying, kqueue_fd=%d", 
                static_cast<long long>(getId()), m_kqueueFd);

            // 加锁保护：遍历并关闭所有活跃 Channel
            std::lock_guard<std::mutex> lock(channel_mutex_);
            while (!live_channels_.empty()) {
                Channel* ch = *live_channels_.begin();
                ch->close();  // 触发 Channel 自身的资源释放
                live_channels_.erase(ch);
            }

            // 关闭 kqueue 实例
            if (::close(m_kqueueFd) < 0) {
                WARN("PollerKqueue[%lld] close kqueue_fd=%d failed: errno=%d, msg=%s", 
                    static_cast<long long>(getId()), m_kqueueFd, errno, strerror(errno));
            }
            INFO("PollerKqueue[%lld] destroyed", static_cast<long long>(getId()));
        }

        // 实现：添加 Channel 到 kqueue
        void PollerKqueue::addChannel(Channel* ch) {
            // 入参检查：避免空指针
            if (ch == nullptr) {
                throw std::invalid_argument("poller.cpp::PollerKqueue::addChannel(): ch is null");
            }

            // 初始化 kqueue 事件（最多 2 个：读+写）
            struct kevent ev[2];
            int ev_count = 0;
            const struct timespec now = {0, 0};  // 立即执行 kevent 操作

            // 添加读事件（若 Channel 启用读监控）
            if (ch->readEnabled()) {
                EV_SET(&ev[ev_count++], ch->fd(), EVFILT_READ, 
                    EV_ADD | EV_ENABLE, 0, 0, ch);
            }
            // 添加写事件（若 Channel 启用写监控）
            if (ch->writeEnabled()) {
                EV_SET(&ev[ev_count++], ch->fd(), EVFILT_WRITE, 
                    EV_ADD | EV_ENABLE, 0, 0, ch);
            }

            trace("PollerKqueue[%lld] add Channel[%lld], fd=%d, read=%d, write=%d", 
                static_cast<long long>(getId()), 
                static_cast<long long>(ch->id()), 
                ch->fd(), 
                ch->readEnabled() ? 1 : 0, 
                ch->writeEnabled() ? 1 : 0);

            // 调用 kevent 添加事件
            int ret = kevent(m_kqueueFd, ev, ev_count, nullptr, 0, &now);
            if (ret != 0) {
                throw std::runtime_error(
                    utils::format("poller.cpp::PollerKqueue::addChannel(): PollerKqueue[%lld] kevent(ADD) failed: fd=%d, errno=%d, msg=%s", 
                                static_cast<long long>(getId()), ch->fd(), errno, strerror(errno)));
            }

            // 加锁保护：将 Channel 加入活跃集合
            std::lock_guard<std::mutex> lock(channel_mutex_);
            live_channels_.insert(ch);
        }

        // 实现：从 kqueue 移除 Channel
        void PollerKqueue::removeChannel(Channel* ch) {
            // 入参检查：避免空指针
            if (ch == nullptr) {
                throw std::invalid_argument("poller.cpp::PollerKqueue::removeChannel(): ch is null");
            }

            trace("PollerKqueue[%lld] remove Channel[%lld], fd=%d", 
                static_cast<long long>(getId()), 
                static_cast<long long>(ch->id()), 
                ch->fd());

            // 加锁保护：从活跃集合中移除 Channel
            std::lock_guard<std::mutex> lock(channel_mutex_);
            live_channels_.erase(ch);

            // 清理活跃事件数组中残留的 Channel 指针（避免野指针访问）
            for (int i = last_active_; i >= 0; --i) {
                if (active_evs_[i].udata == ch) {
                    active_evs_[i].udata = nullptr;
                    break;
                }
            }

            // 注：kqueue 无需显式删除已关闭的 fd（fd 关闭后会自动从 kqueue 中移除）
        }

        // 实现：更新 Channel 的 kqueue 事件
        void PollerKqueue::updateChannel(Channel* ch) {
            // 入参检查：避免空指针
            if (ch == nullptr) {
                throw std::invalid_argument("poller.cpp::PollerKqueue::updateChannel(): ch is null");
            }

            // 初始化 kqueue 事件（最多 2 个：读+写）
            struct kevent ev[2];
            int ev_count = 0;
            const struct timespec now = {0, 0};  // 立即执行 kevent 操作

            // 更新读事件：启用则添加，禁用则删除
            if (ch->readEnabled()) {
                EV_SET(&ev[ev_count++], ch->fd(), EVFILT_READ, 
                    EV_ADD | EV_ENABLE, 0, 0, ch);
            } else {
                EV_SET(&ev[ev_count++], ch->fd(), EVFILT_READ, 
                    EV_DELETE, 0, 0, ch);
            }
            // 更新写事件：启用则添加，禁用则删除
            if (ch->writeEnabled()) {
                EV_SET(&ev[ev_count++], ch->fd(), EVFILT_WRITE, 
                    EV_ADD | EV_ENABLE, 0, 0, ch);
            } else {
                EV_SET(&ev[ev_count++], ch->fd(), EVFILT_WRITE, 
                    EV_DELETE, 0, 0, ch);
            }

            trace("PollerKqueue[%lld] update Channel[%lld], fd=%d, read=%d, write=%d", 
                static_cast<long long>(getId()), 
                static_cast<long long>(ch->id()), 
                ch->fd(), 
                ch->readEnabled() ? 1 : 0, 
                ch->writeEnabled() ? 1 : 0);

            // 调用 kevent 更新事件
            int ret = kevent(m_kqueueFd, ev, ev_count, nullptr, 0, &now);
            if (ret != 0) {
                throw std::runtime_error(
                    utils::format("poller.cpp::PollerKqueue::updateChannel(): PollerKqueue[%lld] kevent(MOD) failed: fd=%d, errno=%d, msg=%s", 
                                static_cast<long long>(getId()), ch->fd(), errno, strerror(errno)));
            }
        }

        // 实现：执行一次 kqueue 轮询
        int PollerKqueue::loopOnce(int wait_ms) {
            // 初始化超时时间（kqueue 需 timespec 类型）
            struct timespec timeout;
            if (wait_ms < 0) {
                // 无限等待：timespec 设为 NULL（通过特殊值标识）
                timeout.tv_sec = 0;
                timeout.tv_nsec = -1;
            } else {
                timeout.tv_sec = wait_ms / 1000;
                timeout.tv_nsec = (wait_ms % 1000) * 1000 * 1000;  // 转换为纳秒
            }

            // 记录轮询开始时间（用于统计耗时）
            const int64_t start_ms = utils::timeMilli();

            // 调用 kevent 等待事件（根据 timeout 决定是否阻塞）
            last_active_ = kevent(
                m_kqueueFd, 
                nullptr, 0,                // 无事件修改，仅等待
                active_evs_, kMaxEvents,   // 活跃事件存储
                (wait_ms < 0) ? nullptr : &timeout  // 超时参数
            );
            const int64_t used_ms = utils::timeMilli() - start_ms;

            // 日志：输出轮询结果
            trace("PollerKqueue[%lld] kevent: wait_ms=%d, return=%d, used_ms=%lld, errno=%d", 
                static_cast<long long>(getId()), 
                wait_ms, last_active_, used_ms, errno);

            // 错误处理：排除信号中断（EINTR 是正常情况，无需抛出异常）
            if (last_active_ < 0 && errno != EINTR) {
                throw std::runtime_error(
                    utils::format("poller.cpp::PollerKqueue::loopOnce(): PollerKqueue[%lld] kevent failed: errno=%d, msg=%s", 
                                static_cast<long long>(getId()), errno, strerror(errno)));
            }

            // 处理活跃事件（从后往前遍历，避免删除元素影响索引）
            for (int i = last_active_ - 1; i >= 0; --i) {
                struct kevent& ev = active_evs_[i];
                Channel* ch = static_cast<Channel*>(ev.udata);

                // 若 Channel 已被移除（udata 为空），跳过处理
                if (ch == nullptr) {
                    continue;
                }

                // 处理逻辑：优先处理写事件，其次处理读事件（含 EOF）
                if (!(ev.flags & EV_EOF) && ch->writeEnabled()) {
                    trace("PollerKqueue[%lld] handle write: Channel[%lld], fd=%d", 
                        static_cast<long long>(getId()), 
                        static_cast<long long>(ch->id()), 
                        ch->fd());
                    ch->handleWrite();
                } else if ((ev.flags & EV_EOF) || ch->readEnabled()) {
                    trace("PollerKqueue[%lld] handle read: Channel[%lld], fd=%d", 
                        static_cast<long long>(getId()), 
                        static_cast<long long>(ch->id()), 
                        ch->fd());
                    ch->handleRead();
                }
                // 异常事件：无预期的事件类型
                else {
                    fatal("PollerKqueue[%lld] unexpected event: Channel[%lld], fd=%d, flags=0x%x", 
                        static_cast<long long>(getId()), 
                        static_cast<long long>(ch->id()), 
                        ch->fd(), ev.flags);
                    throw std::runtime_error("poller.cpp::PollerKqueue::loopOnce(): unexpected event type");
                }
            }

            return last_active_;  // 返回本次处理的活跃事件数量
        }
    #else
        #error "Poller: unsupported opertating system(current only support linux and macOS)"

    #endif

    // 轮询器工厂函数
    PollerBase* createPoller()
    {
        #ifdef OS_LINUX
            return new PollerEpoll();
        #elif defined(OS_MACOSX)
            return new PollerKqueue();
        #else
            throw std::runtime_error("Poller: unsupported opertating system(current only support linux and macOS)");
        #endif
    }
}