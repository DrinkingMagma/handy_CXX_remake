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
                int m_fd;                           // epoll实例文件描述符
                std::set<Channel*> m_liveChannels;   // 当前活跃Channel集合（需线程安全保护）
                std::mutex m_channelMutex;          // 线程安全保护
                struct epoll_event m_activeEvs[kMaxEvents]; // 活跃事件数组

        };

        PollerEpoll::PollerEpoll()
        {
            // EPOLL_CLOEXEC: 当进程执行exec()系列函数时，该实例会被自动关闭
            m_fd = epoll_create1(EPOLL_CLOEXEC);
            if(m_fd < 0)
            {
                throw std::runtime_error(
                    utils::format("poller.cpp::PollerEpoll::PollerEpoll()::epoll_create1() failed: errno=%d, msg=%s",
                                    errno, strerror(errno)));
            }
            INFO("PollerEpoll::PollerEpoll(): PollerEpoll[%lld] created, epoll_fd=%d",
                    static_cast<long long>(getId()), m_fd);
        }

        PollerEpoll::~PollerEpoll()
        {
            INFO("PollerEpoll::~PollerEpoll(): PollerEpoll[%lld] destroying, epoll_fd=%d",
                    static_cast<long long>(getId()), m_fd);
            
            std::lock_guard<std::mutex> lock(m_channelMutex);
            // 遍历并关闭所有活跃的Channel
            while(!m_liveChannels.empty())
            {
                Channel* ch = *m_liveChannels.begin();
                ch->close(); // 触发Channel自身的资源释放
                m_liveChannels.erase(ch);
            }

            // 关闭epoll实例
            if(::close(m_fd) < 0)
            {
                WARN("PollerEpoll::~PollerEpoll(): PollerEpoll[%lld] close epoll_fd=%d failed: errno=%d, msg=%s",
                    static_cast<long long>(getId()), m_fd, errno, strerror(errno));
            }

            INFO("PollerEpoll::~PollerEpoll(): PollerEpoll[%lld] destroyed", static_cast<long long>(getId()));
        }

        void PollerEpoll::addChannel(Channel* ch)
        {
            if(ch == nullptr)
                throw std::invalid_argument("PollerEpoll::addChannel(): ch is null"); 

            // 初始化epoll_event结构体
            struct epoll_event ev;
            memset(&ev, 0, sizeof(ev));
            ev.events = ch->getEvents();
            ev.data.ptr = ch;   // 存储Channel指针，便于事件触发时回调

            TRACE("PollerEpoll::addChannel(): PollerEpoll[%lld] add Channel[%lld], fd=%d, events=0x%x",
                    static_cast<long long>(getId()), static_cast<long long>(ch->getId()), ch->getFd(), ev.events);

            int ret = epoll_ctl(m_fd, EPOLL_CTL_ADD, ch->getFd(), &ev);
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
                throw std::invalid_argument("PollerEpoll::removeChannel: ch is nullptr");

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

    #elif defined(OS_MACOSX)

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