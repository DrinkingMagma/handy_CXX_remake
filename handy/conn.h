/**
 * @file conn.h
 * @brief TCP连接管理核心头文件，包含TcpConn（连接对象）、TcpServer（TCP服务器）、HSHA（半同步半异步服务器）类声明
 * @details 
 *  该文件提供了TCP网络编程的核心组件定义，实现了连接的创建、读写、状态管理、服务器监听及高并发处理等功能，核心特性如下：
 *  1. TcpConn类：封装单个TCP连接，支持非阻塞I/O、连接状态管理（INVALID/HAND_SHAKING/CONNECTED/CLOSED/FAILED）、
 *     数据发送/接收、编解码集成、超时处理、自动重连、上下文存储等功能，继承自std::enable_shared_from_this确保智能指针安全管理；
 *  2. TcpServer类：实现TCP服务器功能，支持绑定地址、监听端口、接受客户端连接、多事件循环（EventBases）负载均衡，
 *     可配置连接创建回调、状态回调、数据读写回调，支持端口复用；
 *  3. HSHA类：基于TcpServer实现半同步半异步服务器模型，通过线程池（ThreadPool）处理业务逻辑，
 *     实现I/O线程与业务线程分离，提升高并发场景下的性能，支持优雅退出；
 *  4. 类型定义：提供TcpConnPtr（连接智能指针）、TcpCallBack（连接回调）、RetMsgCallBack（带返回值的消息回调）等常用类型别名，简化开发；
 *  5. 线程安全：关键操作（如状态修改、回调注册、通道访问）通过互斥锁保护，支持多线程环境下的安全调用。
 * @note 
 *  1. 依赖组件：需配合event_base.h（事件循环）、codec.h（编解码）、non_copy_able.h（禁止拷贝）、net.h（网络工具）、
 *     thread_pool.h（线程池）等头文件使用，编译时需链接pthread库；
 *  2. 智能指针管理：TcpConn对象必须通过createConnection静态方法创建，确保使用std::shared_ptr管理生命周期，避免内存泄露；
 *  3. 事件驱动：基于EventBase和Channel实现I/O事件的检测与分发，所有socket均设置为非阻塞模式，提升系统吞吐量；
 *  4. 编解码集成：TcpConn和TcpServer支持通过onMsg方法绑定编解码器，自动处理数据的打包与解包，支持自定义编解码逻辑；
 *  5. 上下文存储：TcpConn提供getContext方法，支持存储任意类型的上下文数据，方便业务逻辑共享状态；
 *  6. 重连机制：TcpConn支持通过setReconnectInterval设置重连间隔，连接断开时会自动尝试重连（需开启重连功能）；
 *  7. 空闲回调：支持添加空闲回调函数，当连接在指定时间内无数据读写时触发，适用于心跳检测等场景。
 */

#pragma once
#include "event_base.h"
#include "codec.h"
#include "non_copy_able.h"
#include "net.h"
#include "thread_pool.h"
#include <assert.h>

namespace handy
{
    /// TCP连接的智能指针类型
    using TcpConnPtr = std::shared_ptr<TcpConn>;
    /// TCP回调函数类型定义
    using TcpCallBack = std::function<void(const TcpConnPtr&)>;
    /// 带返回值的消息回调函数类型定义
    using RetMsgCallBack = std::function<std::string(const TcpConnPtr&, const std::string&)>;

    /**
     * @class TcpConn
     * @brief TCP连接类，封装TCP连接的创建、读写、状态管理等功能
     * @note 1. 继承自std::enable_shared_from_this<TcpConn>，因此TcpConn对象可被std::shared_ptr管理,以便在成员函数中安全地获取自身的shared_ptr
     * @note 2. 继承自NonCopyAble，因此TcpConn对象不可被拷贝构造和赋值
    */
    class TcpConn : public std::enable_shared_from_this<TcpConn>, private NonCopyAble
    {
        public:
            /**
             * @enum TcpConn::State
             * @brief TCP 连接状态枚举，描述连接生命周期中的不同状态。
             */
            enum class State
            {
                INVALID = 1, /// 无效状态
                HAND_SHAKING, /// 正在握手
                CONNECTED,   /// 已连接
                CLOSED,      /// 已关闭
                FAILED,      /// 连接失败
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
                conn->_connect(base, destHost, destPort, timeout_ms, localIp);
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

            /**
             * @brief 获取输入缓冲区
             * @return Buffer& 输入缓冲区引用
            */
            Buffer& getInputBuffer() { return m_inputBuffer; }

            /**
             * @brief 获取输出缓冲区
             * @return Buffer& 输出缓冲区引用
            */
            Buffer& getOutputBuffer() { return m_outputBuffer; }

            /**
             * @brief 获取通道对象
             * @return Channel* 通道对象指针
            */
            Channel* getChannel() const {return m_channel; }

            /**
             * @brief 判断当前连接是否可写
             * @return bool true: 可写, false: 不可写
            */
            bool isWritable() const
            {
                std::lock_guard<std::mutex> lk(m_ChannelMutex);
                return m_channel ? m_channel->isWritable() : false;
            }
            
           /**
            * @brief 获取内部上下文
            * @return AutoContext& 内部上下文
           */
            AutoContext& getInternalContext()
            {
                return m_internalCtx;
            }

            /**
             * @brief 发送输出缓冲区中的数据
            */
            void sendOutputBuffer() { send(m_outputBuffer); }

            /**
             * @brief 发送缓冲区中的数据
             * @param msg 待发送的缓冲区
            */
            void send(Buffer& msg);

            /**
             * @brief 发送指定的数据
             * @param buf 数据缓冲区指针
             * @param len 数据长度
            */
            void send(const char* buf, size_t len);

            /**
             * @brief 发送字符串数据
             * @param s 待发送的字符串
            */
            void send(const std::string& s) { send(s.data(), s.size()); }

            /**
             * @brief 发送以null结尾的字符串
             * @param s 待发送的字符串
             * 
            */
            void send(const char* s) { send(s, strlen(s)); }

            /**
             * @brief 设置数据到达(TCP缓冲区可写)时的回调函数
             * @param cb 回调函数
            */
            void onReadable(const TcpCallBack& cb)
            {
                std::lock_guard<std::mutex> lock(m_callBacksMutex);
                // 断言当前没有已注册的读回调（m_readcb 为空），防止重复注册
                assert(!m_readCB);
                m_readCB = cb;
            }

            /**
             * @brief 设置TCP缓冲区可写时的回调函数
             * @param cb 回调函数
            */
            void onWritable(const TcpCallBack& cb)
            {
                std::lock_guard<std::mutex> lock(m_callBacksMutex);
                if(m_writeCB)
                {
                    WARN("OnWritable callback is being overwritten");
                }
                m_writeCB = cb;
            }

            /**
             * @brief 设置TCP状态改变时的回调函数
             * @param cb 回调函数
            */
            void onState(const TcpCallBack& cb)
            {
                std::lock_guard<std::mutex> lock(m_callBacksMutex);
                if(m_stateCB)
                {
                    WARN("OnState callback is being overwritten");
                }
                m_stateCB = cb;
            }

            /**
             * @brief 添加空闲回调函数
             * @param idle_s 空闲时间（秒）
             * @param cb 回调函数
            */ 
            void addIdleCB(int idle_s, const TcpCallBack& cb);

            /**
             * @brief 设置消息回调函数，与onReadable回调冲突，只能调用一个
             * @param codec 编解码器，所有权将转移给当前对象
             * @param cb 消息回调函数
            */
            void onMsg(std::unique_ptr<CodecBase> codec, const MsgCallBack& cb);

            /**
             * @brief 发送消息（通过编解码器编码后发送）
             * @param msg 要发送的消息
            */
            void sendMsg(const Slice& msg);

            /**
             * @brief 关闭连接（在下一个事件循环中处理）
            */
            void close();

            /**
             * @brief 设置重连时间间隔
             * @param intervalTime_ms 重连时间间隔，单位毫秒（-1：不重连，0：立即重连，>0：间隔时间）
            */
            void setReconnectInterval(int intervalTime_ms)
            {
                std::lock_guard<std::mutex> lock(m_intervalMutex);
                m_reconnectInterval_ms = intervalTime_ms;
            }

            /**
             * @brief 立即关闭连接，清理相关资源
             * @note 慎用，可能导致该连接的引用计数变为0，从而使得连接被析构
            */
            void closeNow();

            /**
             * @brief 清理连接资源
             * @param conn 当前连接的智能指针
            */
            void cleanup(const TcpConnPtr& conn);

            /**
             * @brief 获取远程地址的字符串表示
             * @return std::string 远程地址字符串
            */
            std::string getPeerStr() const { return m_peer.toString(); }

            /**
             * @brief 将连接与已有的文件描述符进行关联
             * @param base 事件循环
             * @param fd 文件描述符
             * @param localIp 本地IP地址
             * @param peerIp 对端IP地址
            */
            void attach(EventBase* base, int fd, const Ipv4Addr& localIp, const Ipv4Addr& peerIp);

        private:
            EventBase* m_base;                      /// 所属的事件循环
            Channel* m_channel;                     /// 关联的事件通道
            mutable std::mutex m_ChannelMutex;      /// 保护m_channel的互斥锁
            Buffer m_inputBuffer;                   /// 输入缓冲区
            Buffer m_outputBuffer;                  /// 输出缓冲区
            Ipv4Addr m_local = Ipv4Addr(0);                       /// 本地地址
            Ipv4Addr m_peer = Ipv4Addr(0);                        /// 对端地址
            State m_state;                          /// 连接状态
            mutable std::mutex m_stateMutex;        /// 保护m_state的互斥锁
            TcpCallBack m_readCB;                   /// 读回调函数
            TcpCallBack m_writeCB;                  /// 写回调函数
            TcpCallBack m_stateCB;                  /// 状态变更回调函数
            mutable std::mutex m_callBacksMutex;    /// 保护回调函数的互斥锁
            std::list<IdleId> m_idleIds;            /// 空闲回调ID列表
            TimerId m_timeoutId;                    /// 超时ID
            AutoContext m_ctx;                      /// 上下文对象
            AutoContext m_internalCtx;              /// 内部上下文对象
            mutable std::mutex m_ctxMutex;          /// 保护上下文对象的互斥锁
            std::string m_destHost;                 /// 目标主机地址
            std::string m_localIp;                  /// 本地IP地址
            int m_destPort;                         /// 目标端口
            int m_connectTimeout_ms;                   /// 连接超时时间
            int m_reconnectInterval_ms;                /// 重连间隔时间
            mutable std::mutex m_intervalMutex;     /// 重连间隔的互斥锁
            int64_t m_connectedTime_ms;                /// 连接建立时间
            std::unique_ptr<CodecBase> m_codec;     /// 编解码器

            /**
             * @brief 处理读事件
             * @param conn 当前连接的智能指针
            */
            void _handleRead(const TcpConnPtr& conn);

            /**
             * @brief 处理写事件
             * @param conn 当前连接的智能指针
            */
            void _handleWrite(const TcpConnPtr& conn);

            /**
             * @brief 发送数据的内部实现
             * @param buf 数据缓冲区指针
             * @param len 数据长度
             * @return ssize_t 实际发送的字节数
            */
            ssize_t _send(const char* buf, size_t len);

            /**
             * @brief 主动连接到指定的主机和端口
             * @param base 事件循环
             * @param peerHost 目标主机名或IP地址
             * @param peerPort 目标端口号
             * @param timeout_ms 连接超时时间
             * @param localIp 本地IP地址
            */
            void _connect(EventBase* base, const std::string& peerHost, unsigned short peerPort, 
                            int timeout_ms, const std::string& localIp);

            /**
             * @brief 重连
            */
            void _reconnect();

            /**
             * @brief 读取数据的内部实现
             * @param fd 文件描述符
             * @param buf 缓冲区指针
             * @param len 要读取的字节数
             * @return int 实际读取的字节数，-1表示读取失败
            */
            virtual int _readImp(int fd, void* buf, size_t len) { return ::read(fd, buf, len); }

            /**
             * @brief 写入数据的内部实现
             * @param fd 文件描述符
             * @param buf 缓冲区指针
             * @param len 要写入的字节数
             * @return int 实际写入的字节数，-1表示写入失败
            */
            virtual int _writeImp(int fd, const void* buf, size_t len) { return ::write(fd, buf, len); }

            /**
             * @brief 处理握手过程
             * @param conn 当前连接的智能指针
             * @return int 0：成功，-1：失败
            */
            virtual int _handleHandshake(const TcpConnPtr& conn);
    };

    /**
     * @class TcpServer
     * @brief TCP服务器类，用于创建和管理TCP服务器
    */
    class TcpServer : private NonCopyAble
    {
        public:
            using Ptr = std::shared_ptr<TcpServer>; /// TcpServer智能指针类型定义

            /**
             * @brief 构造函数
             * @param bases 事件循环组
            */
            explicit TcpServer(EventBases* bases);

            /**
             * @brief 析构函数
            */
            ~TcpServer();

            /**
             * @brief 绑定到指定的主机和端口
             * @param host 主机名或IP地址
             * @param port 端口号
             * @param isReusePort 是否启用端口复用
             * @return int 0：成功，<0：失败
            */
            int bind(const std::string& host, unsigned short port, bool isReusePort = false);

            /**
             * @brief 启动一个TCP服务器
             * @param bases 事件循环组
             * @param host 主机名或IP地址
             * @param port 端口号
             * @param isReusePort 是否启用端口复用
             * @return Ptr 服务器的智能指针，nullptr表示启动失败
            */
            static Ptr startServer(EventBases* bases, const std::string& host, 
                                    unsigned short port, bool isReusePort = false);

            /**
             * @brief 获取服务器绑定的地址
             * @return Ipv4Addr 服务器地址
            */
            Ipv4Addr getAddr() const { return m_addr; }

            /**
             * @brief 获取服务器使用的事件循环
             * @return EventBase* 事件循环对象指针
            */
            EventBase* getBase() const { return m_base; }

            /**
             * @brief 设置连接创建时的回调函数
             * @param cb 回调函数，返回新创建的连接
            */
            void onConnCreate(const std::function<TcpConnPtr()>& cb)
            {
                std::lock_guard<std::mutex> lock(m_callBacksMutex);
                m_createCB = cb;
            }

            /**
             * @brief 设置连接状态改变时的回调函数
             * @param cb 回调函数
            */
            void onConnState(const TcpCallBack& cb)
            {
                std::lock_guard<std::mutex> lock(m_callBacksMutex);
                m_stateCB = cb;
            }

            /**
             * @brief 设置连接可读时的回调函数
             * @param cb 回调函数
             * @note 与onConnMsg冲突，只能调用一个
            */
            void onConnRead(const TcpCallBack& cb)
            {
                std::lock_guard<std::mutex> lock(m_callBacksMutex);
                m_readCB = cb;
            }

            /**
             * @brief 设置消息处理的回调函数
             * @param codec 消息编解码器，所有权转移给TcpServer
             * @param cb 回调函数
             * @note 与onConnRead冲突，只能调用一个
            */
            void onConnMsg(std::unique_ptr<CodecBase> codec, const MsgCallBack& cb)
            {
                std::lock_guard<std::mutex> lock(m_callBacksMutex);
                m_codec = std::move(codec);
                m_msgCB = cb;
                assert(!m_readCB);
            }
        private:
            EventBase* m_base;                      /// 事件循环对象
            EventBases* m_bases;                    /// 事件循环对象组
            Ipv4Addr m_addr = Ipv4Addr(0);                        /// 绑定的服务器地址
            Channel* m_listenChannel;               /// 监听通道
            mutable std::mutex m_ChannelMutex;      /// 监听通道的互斥锁
            TcpCallBack m_stateCB;                  /// 连接状态回调函数
            TcpCallBack m_readCB;                   /// 读事件回调函数
            MsgCallBack m_msgCB;                    /// 消息回调函数
            std::function<TcpConnPtr()> m_createCB; /// 连接创建回调函数
            std::unique_ptr<CodecBase> m_codec;     /// 编解码器
            mutable std::mutex m_callBacksMutex;    /// 回调函数的互斥锁

            /**
             * @brief 处理接受连接事件
            */
            void _handleAccept();
    };

    /**
     * @class HSHA
     * @brief 半同步半异步(HSHA)服务器类
    */
    class HSHA : private NonCopyAble
    {
        public:
            using Ptr = std::shared_ptr<HSHA>; /// HSHA智能指针类型定义
            /**
             * @brief 启动一个HSHA服务器
             * @param base 事件循环
             * @param host 主机名或IP地址
             * @param port 端口号
             * @param threads 工作线程数量
             * @return Ptr 服务器的智能指针，nullptr表示启动失败
            */
            static Ptr startServer(EventBase* base, const std::string& host,
                                     unsigned short port, int threads);

            /**
             * @brief 构造函数
             * @param threads 工作线程数量
            */
            explicit HSHA(int threads);

            /**
             * @brief 析构函数
            */
            ~HSHA() = default;

            /**
             * @brief 退出服务器
            */
            void exit() 
            {
                m_threadPool.exit();
                m_threadPool.join();
            }

            /**
             * @brief 设置消息处理的回调函数
             * @param codec 消息编解码器，所有权转移给HSHA
             * @param cb 消息处理函数
            */
            void onMsg(std::unique_ptr<CodecBase> codec, const RetMsgCallBack& cb);

        private:
        
            TcpServer::Ptr m_server;          /// TCP服务器对象
            ThreadPool m_threadPool;          /// 线程池对象
    };
}   // namespace handy