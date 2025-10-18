/**
 * @file udp.h
 * @brief UDP客户端和服务端连接的封装实现
*/

#ifndef __HANDY_UDP_H__
#define __HANDY_UDP_H__ 

#include "event_base.h"
#include "non_copy_able.h"

namespace handy
{

    // UDP数据包的最大大小为4096字节（4KB）
    const int kUdpPacketSize = 4096;
    /**
     * @class UdpConn
     * @brief UDP连接类，封装UDP连接的创建、数据发送和接收
    */
    class UdpConn : public std::enable_shared_from_this<UdpConn>, private NonCopyAble
    { 
        public:
            // UdpConn的智能指针类型定义
            using Ptr = std::shared_ptr<UdpConn>;
            /**
             * @brief 消息处理回调函数类型
             * @param conn 当前UdpConn的智能指针
             * @param buf 接收到的数据缓冲区
            */
            using UdpMsgCallBack = std::function<void(const UdpConn::Ptr&, Buffer)>;

            /**
             * @brief 创建新的UDP连接
             * @param base 事件循环对象
             * @param destHost 目标主机地址
             * @param destPort 目标主机端口
             * @return Ptr 新创建的UdpConn智能指针，失败返回nullptr
            */
            static Ptr createConnection(EventBase* base, const std::string& destHost, unsigned short destPort);

            /**
             * @brief 析构函数，关闭连接并释放资源
            */
            virtual ~UdpConn();

            /**
             * @brief 获取关联的事件循环对象
             * @return EventBase* 事件循环对象指针
            */
            EventBase* getBase() const { return m_base; }

            /**
             * @brief 获取通道对象
             * @return Channel* 通道对象指针
            */
            Channel* getChannel() const { return m_channel; }

            /**
             * @brief 发送数据缓冲区中的数据
             * @param msg 待发送的数据缓冲区
            */
            void send(Buffer msg);

            /**
             * @brief 发送指定长度的字节数据
             * @param buf 数据缓冲区指针
             * @param len 数据长度
            */
            void send(const char* buf, size_t len);

            /**
             * @brief 发送字符串数据
             * @param s 待发送的字符串
            */
            void send(const std::string& s);

            /**
             * @brief 发送以null结尾的字符串
             * @param s 待发送的字符串指针
            */
            void send(const char* s);

            /**
             * @brief 设置消息处理回调函数
             * @param cb 回调函数
            */
            void onMsg(const UdpMsgCallBack& cb) { m_udpMsgCallback = cb; }

            /**
             * @brief 关闭当前UDP连接
            */
            void close();

            /**
             * @brief 获取远程地址的字符串表示
             * @return std::string 远程地址字符串
            */
            std::string getPeerStr() const { return m_peer.toString(); }

            /**
             * @brief 获取自动管理的上下文对象
             * @tparam T 上下文数据类型
             * @return T& 上下文数据对象引用
            */
            template <class T>
            T& getContext() { return m_ctx.context<T>(); }
        private:
            EventBase* m_base = nullptr;        // 关联的事件循环对象
            Channel* m_channel = nullptr;       // 通道对象
            Ipv4Addr m_local = Ipv4Addr(0);     // 本地地址
            Ipv4Addr m_peer = Ipv4Addr(0);      // 目标地址
            AutoContext m_ctx;                  // 自动上下文对象
            std::string m_destHost;             // 目标主机地址
            int m_destPort = 0;                 // 目标主机端口
            UdpMsgCallBack m_udpMsgCallback;          // 消息处理回调函数

            /**
             * @brief 构造函数，私有以确保只能通过createConnection方法创建
            */
            UdpConn() = default;

            /**
             * @brief 处理读事件的回调函数
             * @param conn 当前UdpConn的智能指针
            */
            void _handleRead(const UdpConn::Ptr& conn);
    };

    /**
     * @class UdpServer
     * @brief UDP服务器类，封装UDP服务器的创建、绑定和消息处理
    */
    class UdpServer : public std::enable_shared_from_this<UdpServer>, private NonCopyAble
    {
        public:
            // UdpServer的智能指针类型定义
            using Ptr = std::shared_ptr<UdpServer>;

            /**
             * @brief 消息处理回调函数类型
             * @param server 当前UdpConn的智能指针
             * @param buf 接收到的数据缓冲区
             * @param addr 发送方地址
            */
            using ServerMsgCallBack = std::function<void(const UdpServer::Ptr&, Buffer, Ipv4Addr)>;

            /**
             * @brief 构造哈桑农户
             * @param m_bases 事件循环对象集合智能指针
            */
            explicit UdpServer(EventBases* m_bases);

            /**
             * @brief 析构函数，释放资源
            */
            ~UdpServer();

            /**
             * @brief 将服务器绑定到指定地址和端口
             * @param host 绑定的主机地址
             * @param port 绑定的端口号
             * @param isReusePort 是否复用端口
             * @return int 0: 成功; -1: 失败
            */
            int bind(const std::string& host, unsigned short port, bool isReusePort = false);

            /**
             * @brief 启动并创建一个新的UDP服务器
             * @param bases 事件循环对象集合智能指针
             * @param host 绑定的主机地址
             * @param port 绑定的端口号
             * @param isReusePort 是否复用端口
             * @return Ptr 新创建的UdpServer的智能指针，失败返回nullptr
            */
            static Ptr startServer(EventBases* bases, const std::string& host, unsigned short port, bool isReusePort = false);

            /**
             * @brief 获取服务器绑定的地址
             * @return Ipv4Addr 服务器绑定的地址
            */
            Ipv4Addr getAddr() const { return m_addr; }

            /**
             * @brief 获取关联的事件循环对象
             * @return EventBase* 事件循环对象指针
            */
            EventBase* getBase() const { return m_base; }

            /**
             * @brief 发送数据缓冲区到指定地址
             * @param msg 待发送的数据缓冲区
             * @param addr 目标地址
            */
            void sendTo(Buffer msg, Ipv4Addr addr);

            /**
             * @brief 发送指定长度的字节数据到指定地址
             * @param buf 数据缓冲区指针
             * @param len 数据长度
             * @param addr 目标地址
            */
            void sendTo(const char* buf, size_t len, Ipv4Addr addr);

            /**
             * @brief 发送字符串到指定地址
             * @param s 待发送的字符串
             * @param addr 目标地址
            */
            void sendTo(const std::string& s, Ipv4Addr addr);

            /**
             * @brief 发送以null结尾的字符串到指定地址
             * @param s 字符串指针
             * @param addr 目标地址
            */
            void sendTo(const char* s, Ipv4Addr addr);

            /**
             * @brief 设置消息处理回调函数
             * @param cb 回调函数
            */
            void onMsg(const ServerMsgCallBack& cb) { m_serverMsgCallback = cb; }
        
        private:
            EventBase* m_base = nullptr;        // 关联的事件循环对象
            EventBases* m_bases = nullptr;      // 事件循环对象集合
            Ipv4Addr m_addr = Ipv4Addr(0);                    // 服务器绑定的地址
            Channel* m_channel = nullptr;       // 通道对象
            ServerMsgCallBack m_serverMsgCallback;          // 消息处理回调函数
    };

    /**
     * @class HSHAU
     * @brief 半同步半异步UDP服务器，使用线程池处理消息
    */
    class HSHAU : private NonCopyAble
    {
        public:
            // HSHAU的智能指针类型定义
            using Ptr = std::shared_ptr<HSHAU>;

            /**
             * @brief 消息处理回调函数类型
             * @param server 当前UdpConn的智能指针
             * @param input 输入字符串
             * @param addr 发送方地址
             * @return std::string 处理后的输出字符串
            */
            using RetMsgCallback = std::function<std::string(const UdpServer::Ptr&, const std::string&, Ipv4Addr)>;

            /**
             * @brief 启动半同步半异步UDP服务器
             * @param base 事件循环对象指针
             * @param host 绑定的主机地址
             * @param port 绑定的端口号
             * @param threads 线程池大小
             * @return Ptr 新创建的HSHAU的智能指针，失败返回nullptr
            */
            static Ptr startServer(EventBase* base, const std::string& host,
                                     unsigned short port, int threads);

            /**
             * @brief 构造函数
             * @param threads 线程池大小
            */
            explicit HSHAU(int threads);

            /**
             * @brief 析构函数，停止线程池
            */
            ~HSHAU() { exit(); }

            /**
             * @brief 停止服务器并释放资源
            */
            void exit();

            /**
             * @brief 设置消息处理回调函数
             * @param cb 回调函数
            */
            void onMsg(const RetMsgCallback& cb);

        private:
            UdpServer::Ptr m_server;    // UDP服务器对象
            ThreadPool m_threadPool;    // 线程池对象
    };
} // namespace handy


#endif  // __HANDY_UDP_H__