#pragma once
#include "logger.h"
#include "net.h"
#include "utils.h"
#include "codec.h"
#include "threads.h"
#include "non_copy_able.h"

namespace handy
{
    class Channel;          // 事件通道抽象类
    class TcpConnection;    // TCP连接抽象类
    class TCPServer;        // TCP服务器抽象类
    class IdleIdImp;        // 空闲资源ID实现类
    struct EventsImp;        // 事件实现结构体
    struct EventBase;        // 事件循环基类结构体
    typedef std::unique_ptr<IdleIdImp> IdleId;      // 空闲资源ID的智能指针类型
    typedef std::pair<int64_t, int64_t> TimerId;    // 定时任务ID类型

    /**
     * @class AutoContext
     * @brief 自动上下文管理器（线程安全）
     * @details 1. 提供模板化的上下文创建、获取、与自动清理能力，支持任意类型的上下文存储
     *          2. 基于原子操作与互斥锁保证线程安全，避免数据竞争与内存泄漏
     *          3. 析构时自动清理上下文资源，也支持显式调用reset()主动释放
     *          4. 采用“双重检查锁定”模式优化性能，减少锁的竞争开销
     * @note 1. 上下文类型需支持默认构造（无参构造函数）
     * @note 2. 禁止拷贝与移动，确保上下文资源唯一管理
     * @note 3. 清理函数在持有互斥锁的情况下执行，避免并发销毁导致的野指针
    */
    class AutoContext : private NonCopyAble
    {
        public:
            /**
             * @brief 默认构造函数
            */
            AutoContext() = default;

            /**
             * @brief 模板方法：获取或创建指定类型的上下文
             * @tparam T 上下文类型（需满足：有默认构造函数、非抽象类）
             * @return T& 上下文对象的引用（确保返回有效对象，不会放回空引用）
             * @throw std::bad_alloc 当内存分配失败时抛出此异常（new操作失败触发）
             * @note 1. 线程安全：支持多线程并发调用，不会导致重复创建或数据竞争
             * @note 2. 类型安全：通过static_cast强制转换，确保返回正确类型的引用
            */
            template<typename T>
            T& context()
            {
                // 编译器检查：确保T有默认构造函数（C++及以上支持）
                static_assert<std::is_default_constructible<T>::value, 
                                "Context type T must have a default constructor">;
                // 获取或创建指定类型的上下文(避免创建抽象类实例)
                static_assert<!std::is_abstract<T>::value, 
                                "Context type T must not be an abstract class">;
                
                // 第一重检查：无锁读取当前上下文指针（内存序：acquire，确保读取到最新值）
                void* currentCtx = m_ctx.load(std::memory_order_acquire);

                if(currentCtx == nullptr)
                {
                    std::lock_guard<std::mutex> lock(m_ctxMutex);

                    // 第二重检查：避免锁竞争期间其他线程已经初始化（内存序：relaxed，无需同步，仅本地检查）
                    currentCtx = m_ctx.load(std::memory_order_relaxed);
                    if(currentCtx == nullptr)
                    {
                        // 1. 分配上下文对象（若内存不足，new会爆出std::bad_alloc异常）
                        T* newCtx = new T();

                        // 2. 分配清理函数：捕获上下文指针，确保销毁时类型正确
                        Task* newDel = new Task([newCtx](){
                            // 销毁上下文对象（类型安全，因为捕获的是T*）
                            delete newCtx;
                        });

                        // 3. 原子存储：先存储清理函数，再存储上下文指针（内存序：release，确保写操作可见）
                        m_ctxDel.store(newDel, std::memory_order_release);
                        m_ctx.store(newCtx, std::memory_order_release);

                        // 更新当前上下文指针
                        currentCtx = newCtx;

                        // 日志：记录上下文初始化
                        DEBUG("AutoContext::context(): Created context of type %s (address %p)",
                                typeid(T).name(), static_cast<void*>(newCtx));
                    }
                }

                // 类型强制转换：确保返回正确类型的引用（因m_ctx存储的是T*，强制转换安全）
                return *static_cast<T*>(currentCtx);
            }
    
            /**
             * @brief 显式重置上下文：释放资源并置空指针
             * @details 1. 加锁：确保清理操作的线程安全，避免并发重置导致的双重释放
             *          2. 原子交换：将m_ctx与m_ctxDel置空，同时获取旧值（内存序：acq_rel，确保同步）
             *          3. 清理逻辑：先执行清理函数销毁上下文对象，再释放清理函数自身的内存
             * @note 1. 幂等性：支持多次调用，第二次及以后调用无操作（因指针已置空）
             *       2. 线程安全：多线程并发调用时，仅第一个调用会执行清理逻辑，其他调用无操作
            */
            void reset()
            {
                std::lock_guard<std::mutex> lock(m_ctxMutex);

                // 原子交换：获取旧的清理函数指针，并将m_ctxDel置空（内存序：acq_rel）
                Task* currentDel = m_ctxDel.exchange(nullptr, std::memory_order_acq_rel);
                // 原子交换：获取旧的上下文指针，并将m_ctx置空（内存序：acq_rel）
                void* currentCtx = m_ctx.exchange(nullptr, std::memory_order_acq_rel);

                if(currentDel != nullptr && currentCtx != nullptr)
                {
                    // 执行清理函数，销毁上下文对象
                    (*currentDel)();
                    // 释放清理函数自身的内存
                    delete currentDel;

                    // 日志：记录上下文重置
                    DEBUG("AutoContext::reset(): Reset context (address %p)", currentCtx);
                }
            }

            /**
             * @brief 检查是否已创建上下文
             * @return bool true: 已创建上下文，false: 未创建
             * @details 无锁读取m_ctx指针，线程安全（内存序：acquire，确保读取到最新值）
             * @note 线程安全：支持多线程并发调用，无锁开销，性能高效
            */
            bool hasContext() const
            {
                return m_ctx.load(std::memory_order_acquire) != nullptr;
            }

            /**
             * @brief 析构函数：自动清理上下文资源
             * @note 1. 符合RAII原则：资源获取即初始化，销毁即释放
             *       2. 安全：即使未显式调用reset()，析构时也会自动清理
            */
            ~AutoContext()
            {
                reset();
            }
        private:
            // 上下文对象指针（原子操作确保多线程可见性）
            std::atomic<void*> m_ctx{nullptr};

            // 互斥锁
            mutable std::mutex m_ctxMutex;

            // 上下文清理函数指针
            // 存储销毁上下文对象的任务，避免析构时类型信息丢失
            std::atomic<Task*> m_ctxDel{nullptr};
    };
}   // namespace handy