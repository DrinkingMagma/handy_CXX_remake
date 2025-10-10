#pragma once
#include "utils.h"
#include "logger.h"
#include "non_copy_able.h"
#include <poll.h>
#include "event_base.h"

namespace handy 
{
    
    static constexpr int kMaxEvents = 2048;        // 单次轮询最大处理事件数（限制单次处理的事件数）(静态常量，避免宏定义）)
    static constexpr int kReadEvent = POLLIN;      // 读事件标识（映射POLLIN）
    static constexpr int kWriteEvent = POLLOUT;    // 写事件标识（映射POLLOUT）

    /**
     * @brief 事件轮询器基类（抽象类）
     * @details 1. 定义I/O事件轮询的统一接口，屏蔽不同操作系统(Linux/macOS)的底层差异
     *          2. 采用私有继承NonCopyAble，禁止拷贝与移动，确保对象唯一性
     *          3. 提供纯虚函数接口，由派生类实现具体的轮询逻辑（如epoll/kqueue）
     * @note 基类仅保证成员变量的原子性，派生类需自行保证事件操作的线程安全
    */
    class PollerBase : private NonCopyAble
    {
        public:
            /**
             * @brief 构造函数
             * @details 初始化最后活跃事件索引，生成唯一的轮询器ID（线程安全）
            */
            PollerBase() : m_lastActive(-1), m_id(globalId++){}

            /**
             * @brief 析构函数（纯虚函数）
            */
            virtual ~PollerBase() = default;

            /**
             * @brief 添加事件通知到轮询器
             * @param ch 待添加的Channel对象指针（非空）
             * @throw std::invalid_argument ch为空指针
             * @throw std::runtime_error 底层系统调用失败
            */
            virtual void addChannel(Channel* ch) = 0;

            /**
             * @brief 从轮询器中移除事件通道
             * @param ch 待移除的Channel对象指针（非空）
             * @throw std::invalid_argument ch为空指针
            */
            virtual void removeChannel(Channel* ch) = 0;

            /**
             * @brief 更新轮询器中的事件通道的监控事件
             * @param ch 待更新的Channel对象指针（非空）
             * @throw std::invalid_argument ch为空指针
             * @throw std::runtime_error 底层系统调用失败
            */
            virtual void updateChannel(Channel* ch) = 0;

            /**
             * @brief 执行一次事件轮询
             * @param waitTime_ms 等待事件的超时时间（毫秒），-1表示无限等待
             * @return int 本次轮询触发的活跃事件数量
             * @throw std::runtime_error 底层系统调用失败（信号中断除外）
            */
            virtual int loopOnce(int waitTime_ms) = 0;

            /**
             * @brief 获取轮询器唯一ID
             * @return int64_t 轮询器唯一ID（非负整数）
            */
            int64_t getId() const noexcept { return m_id; }

        protected:
            static std::atomic<int64_t> globalId;   // 静态原子变量，确保多线程环境下ID唯一递增
            const int64_t m_id;                 // 轮询器唯一标识符（构造时生成）
            int m_lastActive;                   // 最后一次活跃事件的索引（用于遍历）

    };

    /**
     * @brief 轮询器工厂函数
     * @return 根据操作系统类型创建对应的Poller实例（Linux->epoll, macOS->kqueue）
     * @return PollerBase 派生类对象指针（需调用者手动释放，建议使用智能指针）
     * @throw std::runtime_error 若当前操作系统不支持（非Linux/macOS）则抛出异常
    */
    PollerBase* createPoller();
}   // namespace handy