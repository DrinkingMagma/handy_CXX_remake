/**
 * @file poller.h
 * @brief 跨平台I/O事件轮询器抽象基类定义头文件，定义统一的事件轮询接口，适配不同操作系统底层实现
 * @details 
 *  该文件定义了事件轮询器的核心抽象层，主要包含：
 *  1. 核心常量定义：kMaxEvents（单次轮询最大处理事件数）、kReadEvent（读事件标识）、kWriteEvent（写事件标识），为上层提供统一的事件操作接口；
 *  2. PollerBase抽象基类：定义了事件轮询的核心接口，包括Channel的添加（addChannel）、移除（removeChannel）、事件更新（updateChannel）和单次轮询（loopOnce），屏蔽了Linux（epoll）和macOS（kqueue）的底层差异；
 *  3. 工厂函数createPoller()：根据当前操作系统自动创建对应的Poller实例，简化上层使用，无需关注底层实现细节；
 * @note 
 *  1. 抽象基类特性：PollerBase继承自NonCopyAble，禁止拷贝和移动，确保轮询器对象的唯一性；析构函数为纯虚函数，强制派生类实现资源释放逻辑；
 *  2. 线程安全：基类通过std::atomic<int64_t>保证轮询器ID的原子生成，派生类需自行实现事件操作的线程安全（如通过互斥锁保护活跃Channel集合）；
 *  3. 错误处理：所有纯虚接口均明确了异常抛出场景（如空指针、系统调用失败），上层调用需做好异常捕获；
 *  4. 资源管理：createPoller()返回的Poller实例需调用者手动释放（建议使用智能指针管理），避免内存泄漏；
 *  5. 平台限制：目前仅支持Linux和macOS，其他操作系统会通过工厂函数抛出std::runtime_error异常。
 */

#pragma once
#include "utils.h"
#include "logger.h"
#include "non_copy_able.h"
#include <poll.h>
#include "event_base.h"

namespace handy 
{
    
    static constexpr int kMaxEvents = 2048;        /// 单次轮询最大处理事件数（限制单次处理的事件数）(静态常量，避免宏定义）)
    static constexpr int kReadEvent = POLLIN;      /// 读事件标识（映射POLLIN）
    static constexpr int kWriteEvent = POLLOUT;    /// 写事件标识（映射POLLOUT）

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
            PollerBase() : m_id(globalId++), m_lastActive(-1){}

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
            static std::atomic<int64_t> globalId;   /// 静态原子变量，确保多线程环境下ID唯一递增
            const int64_t m_id;                 /// 轮询器唯一标识符（构造时生成）
            int m_lastActive;                   /// 最后一次活跃事件的索引（用于遍历）

    };

    /**
     * @brief 轮询器工厂函数
     * @return 根据操作系统类型创建对应的Poller实例（Linux->epoll, macOS->kqueue）
     * @return PollerBase 派生类对象指针（需调用者手动释放，建议使用智能指针）
     * @throw std::runtime_error 若当前操作系统不支持（非Linux/macOS）则抛出异常
    */
    PollerBase* createPoller();
}   // namespace handy