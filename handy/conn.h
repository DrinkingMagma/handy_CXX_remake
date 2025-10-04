/**
 * @file conn.h
 * @brief TCP连接库的主要头文件，包含TcpConn，TcpServer和HSHA类的声明
*/
#pragma once
#include "event_base.h"
#include "codec.h"
#include "non_copy_able.h"
#include "net.h"
#include "thread_pool.h"

namespace handy
{
    // TCP连接的智能指针类型
    using TcpConnPtr = std::shared_ptr<TcpConn>;
    // TCP回调函数类型定义
    using TcpCallBack = std::function<void(const tcpConnPtr&)>;
    // 消息回调函数类型定义
    using MsgCallBack = std::function<void(const tcpConnPtr&, const Slice&)>;
    // 带返回值的消息回调函数类型定义
    using RetMsgCallBack = std::function<std::string(const tcpConnPtr&, const std::string&)>;
    // 空闲回调ID类型定义
    using IdleId = uint64_t;
    // 定时器ID类型定义
    using TimerId = uint64_t;

    /**
     * @class TcpConn
     * @brief TCP连接类，封装TCP连接的创建、读写、状态管理等功能
     * @note 1. 继承自std::enable_shared_from_this<TcpConn>，因此TcpConn对象可被std::shared_ptr管理,以便在成员函数中安全地获取自身的shared_ptr
     * @note 2. 继承自NonCopyAble，因此TcpConn对象不可被拷贝构造和赋值
    */
    class TcpConn : public std::enable_shared_from_this<TcpConn>, private NonCopyAble
    {
        public:
            // TCP连接状态枚举
            enum class State
            {
                INVALID = 1, // 无效状态
                HAND_SHAKING, // 正在握手
                CONNECTED,   // 已连接
                CLOSED,      // 已关闭
                FAILED,      // 连接失败
            };

            /**
             * @brief 构造函数，初始化连接状态未INVALID
            */
            TcpConn();

            /**
             * @brief 析构函数，关闭连接并释放资源
            */
            virtual ~TcpConn();

            /**
             * @brief 创建一个客户端TCP连接
             * @tparam C 连接类型，默认为TcpConn
             * @param base 事件循环对象
             * @param destHost 目标主机名或IP地址
             * @param destPort 目标端口号
             * @param timeout_ms 连接超时时间，默认为0表示不超时
             * @param localIp 本地IP地址，默认为空表示使用自动选择
             * @return TcpConnPtr 创建的连接对象的智能指针
            */
            template <class C = TcpConn>
            static TcpConnPtr createConnection(EventBase* base, const std::string& destHost, unsigned short destPort,
                                                int timeout_ms = 0, const std::string& localIp = "")
            {
                TcpConnPtr conn(std::make_shared<C>());
                conn->connect(base, destHost, destPort, timeout_ms, localIp);
                return conn;
            }

            /**
             * @brief 创建一个服务端TCP连接（从已有的文件描述符）
             * @tparam C 连接类型，默认为TcpConn
             * @param base 事件循环对象
             * @param fd 文件描述符
             * @param local 本地地址
             * @param peer 对端地址
             * @return TcpConnPtr 创建的连接的智能指针
            */
            template <class C = TcpConn>
            static TcpConnPtr createConnection(EventBase* base, int fd, const Ipv4Addr& local, const Ipv4Addr& peer)
            {
                TcpConnPtr conn(std::make_shared<C>());
                conn->attach(base, fd, local, peer);
                return conn;
            }

            /**
             * @brief 判断当前连接是否为客户端连接
             * @return bool true:客户端连接，false:服务端连接
            */
            bool isClient() const { return m_destPort > 0; }

            /**
             * @brief 获取与当前连接关联的上下文
             * @tparam T 上下文类型
             * @return T& 上下文对象引用
            */
            template <typename T>
            T& getContext()
            {
                std::lock_guard<std::mutex> lock(m_ctxMutex);
                return m_ctx.context<T>();
            }

            /**
             * @brief 获取当前连接所属的事件循环
             * @return EventBase* 事件循环对象指针
            */
            EventBase* getBase() const {return m_base; }

            /**
             * @brief 获取当前连接状态
             * @return State 当前连接状态
            */
            State getState() const
            {
                std::lock_guard<std::mutex> lock(m_stateMutex);
                return m_state;
            }

        private:
            EventBase* m_base;                      // 所属的事件循环
            Channel* m_channel;                     // 关联的事件通道
            mutable std::mutex m_ChannelMutex;      // 保护m_channel的互斥锁
            Buffer m_input;                         // 输入缓冲区
            Buffer m_output;                        // 输出缓冲区
            Ipv4Addr m_local;                       // 本地地址
            Ipv4Addr m_peer;                        // 对端地址
            State m_state;                          // 连接状态
            mutable std::mutex m_stateMutex;        // 保护m_state的互斥锁
            TcpCallBack m_readcb;                   // 读回调函数
            TcpCallBack m_writecb;                  // 写回调函数
            TcpCallBack m_statecb;                  // 状态变更回调函数
            mutable std::mutex m_cbsMutex;          // 保护回调函数的互斥锁
            std::list<IdleId> m_idleIds;            // 空闲回调ID列表
            TimerId m_timeoutId;                    // 超时ID
            AutoContext m_ctx;                      // 上下文对象
            AutoContext m_internalCtx;              // 内部上下文对象
            mutable std::mutex m_ctxMutex;          // 保护上下文对象的互斥锁
            std::string m_destHost;                 // 目标主机地址
            std::string m_localIp;                  // 本地IP地址
            int m_destPort;                         // 目标端口
            int m_connectTimeout;                   // 连接超时时间
            int m_reconnectInterval;                // 重连间隔时间
            mutable std::mutex m_intervalMutex;     // 重连间隔的互斥锁
            int64_t m_connectedTime;                // 连接建立时间
            std::unique_ptr<CodecBase> m_codec;     // 编解码器
    };
}   // namespace handy