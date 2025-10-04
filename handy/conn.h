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
    };
}   // namespace handy