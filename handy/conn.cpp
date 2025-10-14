#include "conn.h"
#include "logger.h"
#include "utils.h"
#include "thread_pool.h"
#include "poller.h"
#include <fcntl.h>

// TCP连接请求的最大等待队列长度
#define MAX_WAIT_QUEUE_LENGTH 20

namespace handy
{
    TcpConn::TcpConn() :
        m_base(nullptr),
        m_channel(nullptr),
        m_state(State::INVALID),
        m_destPort(-1),
        m_connectTimeout_ms(0),
        m_reconnectInterval_ms(-1),
        m_connectedTime_ms(0),
        m_local(0u),
        m_peer(0u) {}

    TcpConn::~TcpConn()
    {
        closeNow();
        TRACE("TcpConn destroyed: %s -> %s", m_local.toString().c_str(), m_peer.toString().c_str());
    }

    void TcpConn::attach(EventBase* base, int fd, const Ipv4Addr& localIp, const Ipv4Addr& peerIp)
    {
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            // 服务器端被动接受的连接或客户端主动发起的连接
            FATAL_IF((m_destPort <= 0 && m_state != State::INVALID) ||
                        (m_destPort >=0 && m_state != State::HAND_SHAKING),
                        "Invalid state for attach. Current state: %d", static_cast<int>(m_state));
        }

        m_base = base;

        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            m_state = State::HAND_SHAKING;
        }

        m_local = localIp;
        m_peer = peerIp;
        
        {
            std::lock_guard<std::mutex> lock(m_ChannelMutex);
            delete m_channel;
            m_channel = new Channel(base, fd, kReadEvent | kWriteEvent);
        }

        TRACE("TcpConn attached: %s -> %s, fd: %d",
                m_local.toString().c_str(), m_peer.toString().c_str(), fd);

        TcpConnPtr conn = shared_from_this();
        {
            std::lock_guard<std::mutex> lock(m_ChannelMutex);
            if(m_channel)
            {
                m_channel->onRead([=]{ conn->_handleRead(conn); });
                m_channel->onWrite([=]{ conn->_handleWrite(conn); });
            }
        }
    }

    void TcpConn::_connect(EventBase* base, const std::string& peerHost, unsigned short peerPort, 
                            int timeout_ms, const std::string& localIp)
    {
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            FATAL_IF(m_state != State::INVALID && m_state != State::CLOSED && m_state != State::FAILED,
                        "Invalid state for connect. Current state: %d", static_cast<int>(m_state));
        }

        m_destHost = peerHost;
        m_destPort = peerPort;
        m_connectedTime_ms = utils::timeMilli();
        m_connectTimeout_ms = timeout_ms;
        m_localIp = localIp;

        Ipv4Addr addr(peerHost, peerPort);
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        FATAL_IF(fd < 0, "socket creation failed: errno=%d, msg=%s", errno, strerror(errno));

        Net::setNonBlock(fd);
        int t = utils::addFdFlag(fd, FD_CLOEXEC);
        FATAL_IF(t < 0, "addFdFlag FD_CLOEXEC failed: errno=%d, msg=%s", errno, strerror(errno));

        int r = 0;
        if(!m_localIp.empty())
        {
            Ipv4Addr localAddr(m_localIp, 0);
            r = ::bind(fd, (struct sockaddr*)&localAddr.getAddr(), sizeof(struct sockaddr));
            if(r != 0)
                ERROR("Bind to %s failed: errno=%d, msg=%s", localAddr.toString().c_str(), errno, strerror(errno));
        }

        if(r == 0)
        {
            r = ::connect(fd, (struct sockaddr*)&addr.getAddr(), sizeof(struct sockaddr_in));
            if(r != 0 && errno != EINPROGRESS)
                ERROR("Connect to %s failed: errno=%d, msg=%s", addr.toString().c_str(), errno, strerror(errno));
        }

        sockaddr_in local;
        socklen_t localLen = sizeof(local);
        if(r == 0)
        {
            // 获取socket实际绑定的本地地址（系统分配的地址和端口）
            r = getsockname(fd, (sockaddr*)&local, &localLen);
            if(r < 0)
                ERROR("getsockname failed: errno=%d, msg=%s", errno, strerror(errno));
        }

        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            m_state = State::HAND_SHAKING;
        }
        attach(base, fd, Ipv4Addr(local), addr);

        if(timeout_ms > 0)
        {
            TcpConnPtr conn = shared_from_this();
            m_timeoutId = base->runAfter(timeout_ms, [conn](){
                if(conn->getState() == TcpConn::State::HAND_SHAKING)
                    conn->m_channel->close();
            });
        }
    }

    void TcpConn::close()
    {
        std::lock_guard<std::mutex> lock(m_ChannelMutex);
        if(m_channel)
        {
            TcpConnPtr conn = shared_from_this();
            getBase()->safeCall([conn]()
            {
                std::lock_guard<std::mutex> lock(conn->m_ChannelMutex);
                if(conn->m_channel)
                    conn->m_channel->close();
            });
        }
    }

    void TcpConn::cleanup(const TcpConnPtr& conn)
    {   
        // 处理剩余的输入数据
        {
            std::lock_guard<std::mutex> lock(m_callBacksMutex);
            if(m_readCB && m_inputBuffer.size() > 0)
                m_readCB(conn);
        }

        // 更新状态
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            if(m_state == State::HAND_SHAKING)
                m_state = State::FAILED;
            else
                m_state = State::CLOSED;
        }
        
        TRACE("TcpConn closing: %s -> %s, fd: %d. errno: %d, msg: %s",
                m_local.toString().c_str(), m_peer.toString().c_str(),
                m_channel ? m_channel->getFd() : -1, errno, strerror(errno));

        // 取消超时定时器
        if(m_base)
            m_base->cancel(m_timeoutId);

        // 触发状态回调函数
        {
            std::lock_guard<std::mutex> lock(m_callBacksMutex);
            if(m_stateCB)
                m_stateCB(conn);
        }

        // 处理重连
        int interval_ms;
        {
            std::lock_guard<std::mutex> lock(m_intervalMutex);
            interval_ms = m_reconnectInterval_ms;
        }
        if(interval_ms >= 0 && m_base && !m_base->exited())
        {
            _reconnect();
            return;
        }

        // 注销空闲回调
        for(const auto& idleId : m_idleIds)
        {
            if(m_base)
                handleUnregisterIdle(m_base, idleId);
        }
        m_idleIds.clear();

        // 清理通道
        {
            std::lock_guard<std::mutex> lock(m_callBacksMutex);
            m_readCB == nullptr;
            m_writeCB == nullptr;
            m_stateCB == nullptr;
        }
        Channel* ch = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_ChannelMutex);
            ch = m_channel;
            m_channel = nullptr;
        }
        delete ch;
    }

    void TcpConn::_handleRead(const TcpConnPtr& conn)
    {
        // 处理握手
        if(getState() == State::HAND_SHAKING)
        {
            if(_handleHandshake(conn) != 0)
                return;
        }

        // 处理已连接状态的读事件
        // 每次先扩展输入缓冲区，然后尝试读取数据至输入缓冲区末尾
        while(getState() == State::CONNECTED)
        {
            m_inputBuffer.makeRoom();
            int rd = 0;
            int fd = -1;

            {
                std::lock_guard<std::mutex> lock(m_ChannelMutex);
                if(m_channel)
                    fd = m_channel->getFd();
            }

            if(fd >= 0)
            {
                rd = _readImp(fd, m_inputBuffer.end(), m_inputBuffer.space());
                TRACE("Channel  %lld, fd %d, read %d bytes",
                        (long long)m_channel->getId(), fd, rd);
            }
            // 若被信号中断，继续读取
            else if(rd == -1 && errno == EINTR)
                continue;
            // 若没有数据可读，则结束循环
            else if(rd == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
            {
                for(const auto& idleId : m_idleIds)
                    handleUpdateIdle(m_base, idleId);

                {
                    std::lock_guard<std::mutex> lock(m_callBacksMutex);
                    if(m_readCB && m_inputBuffer.size() > 0)
                        m_readCB(conn);
                }
                break;
            }
            // 若连接关闭或出错
            else if(fd == -1 || rd == 0 || rd == -1)
            {
                cleanup(conn);
                break;
            }
            // rd大于0，即获取到了数据，需修改缓冲区的实际数据长度
            else
            {
                m_inputBuffer.addSize(rd);
            }
        }
    }

    int TcpConn::_handleHandshake(const TcpConnPtr& conn)
    {
        FATAL_IF(getState() != State::HAND_SHAKING, 
                    "handleHandShake called when state is not HandShaking, current state is %d",
                    static_cast<int>(getState()));
        
        int fd = -1;
        {
            std::lock_guard<std::mutex> lock(m_ChannelMutex);
            if(m_channel)
                fd = m_channel->getFd();
        }

        if(fd < 0)
        {
            cleanup(conn);
            return -1;
        }

        // 以非阻塞方式监控fd的可写事件与错误事件
        pollfd pFd;
        pFd.fd = fd;
        pFd.events = POLLOUT | POLLERR;
        int r = poll(&pFd, 1, 0);

        // 若检测到可写事件
        if(r == 1 && pFd.revents == POLLOUT)
        {
            {
                std::lock_guard<std::mutex> lock(m_ChannelMutex);
                if(m_channel)
                    m_channel->enableReadWrite(true, false);
            }

            {
                std::lock_guard<std::mutex> lock(m_stateMutex);
                m_state = State::CONNECTED;
            }

            m_connectedTime_ms = utils::timeMilli();
            TRACE("TcpConn connected: %s -> %s, fd: %d",
                    m_local.toString().c_str(), m_peer.toString().c_str(), fd);

            {
                std::lock_guard<std::mutex> lock(m_stateMutex);
                if(m_stateCB)
                    m_stateCB(conn);
            }
        }
        else
        {
            TRACE("Poll on fd %d returned %d, revents: %d", fd, r, pFd.revents);
            cleanup(conn);
            return -1;
        }

        return 0;
    }

    void TcpConn::_handleWrite(const TcpConnPtr& conn)
    {
        State currentState = getState();

        if(currentState == State::HAND_SHAKING)
            _handleHandshake(conn);
        else if(currentState == State::CONNECTED)
        {
            ssize_t sended = _send(m_outputBuffer.begin(), m_outputBuffer.size());
            m_outputBuffer.consume(sended);

            {
                std::lock_guard<std::mutex> lock(m_callBacksMutex);
                if(m_outputBuffer.empty() && m_writeCB)
                    m_writeCB(conn);
            }

            bool isWritable = false;
            {
                std::lock_guard<std::mutex> lock(m_ChannelMutex);
                if(m_channel)
                    isWritable = m_channel->isWritable();
            }

            if(m_outputBuffer.empty() && isWritable)
            {
                // 写回调可能已经写入新的数据，因此需要检查是否仍然为空
                if(m_outputBuffer.empty())
                {
                    std::lock_guard<std::mutex> lock(m_ChannelMutex);
                    if(m_channel)
                        m_channel->enableWrite(false);
                }
            }
        }
        else
        {
            ERROR("Unexpected call to _handleWrite in state: %d", static_cast<int>(currentState));
        }
    }

    ssize_t TcpConn::_send(const char* buf, size_t len)
    {
        if(len == 0)
            return 0;

        size_t sended = 0;
        int fd = -1;

        {
            std::lock_guard<std::mutex> lock(m_ChannelMutex);
            if(m_channel)
                fd = m_channel->getFd();
        }

        if(fd < 0)
            return 0;

        while(len > sended)
        {
            ssize_t curWrited = _writeImp(fd, buf + sended, len - sended);
            TRACE("Channel %lld, fd %d, wrote %ld bytes",
                    (long long)m_channel->getId(), fd, curWrited);

            if(curWrited > 0)
            {
                sended += curWrited;
                continue;
            }
            // 被信号中断，继续写
            else if(curWrited == -1 && errno == EINTR)
            {
                continue;
            }
            // 暂时无法写入，启用写事件监听
            else if(curWrited == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
            {
                {
                    std::lock_guard<std::mutex> lock(m_ChannelMutex);
                    if(m_channel && !m_channel->isWritable())
                        m_channel->enableWrite(true);
                }
                break;
            }
            // 写入错误
            else
            {
                ERROR("Write error on channel %lld, fd %d, curWrited %d, errno=%d, msg=%s",
                        (long long)m_channel->getId(), fd, (int)curWrited, errno, strerror(errno));
                break;
            }
        }

        return sended;
    }

    void TcpConn::send(Buffer& buf)
    {
        if(buf.empty())
            return;

        bool isChannelValid = false;
        {
            std::lock_guard<std::mutex> lock(m_ChannelMutex);
            isChannelValid = (m_channel != nullptr);
        }

        if(!isChannelValid)
        {
            WARN("Sending data to a closed connection: %s -> %s, %lu bytes lost",
                    m_local.toString().c_str(), m_peer.toString().c_str(), buf.size());
            return;
        }

        bool isWritable = false;
        {
            std::lock_guard<std::mutex> lock(m_ChannelMutex);
            if(m_channel)
                isWritable = m_channel->isWritable();
        }

        if(isWritable)
        {
            // 若通道启用写事件，则将数据追加到输出缓冲区中
            m_outputBuffer.absorb(buf);
        }
        // 尝试直接发送数据
        else
        {
            ssize_t sended = _send(buf.begin(), buf.size());
            buf.consume(sended);

            // 若还有未发送的数据，则追加到输出缓冲区中，并启用写事件监听
            if(buf.size() > 0)
            {
                m_outputBuffer.absorb(buf);

                {
                    std::lock_guard<std::mutex> lock(m_ChannelMutex);
                    if(m_channel && !m_channel->isWritable())
                        m_channel->enableWrite(true);
                }
            }
        }
    }

    void TcpConn::send(const char* buf, size_t len)
    {
        if(len == 0)
            return;
        
        bool isChannelValid = false;
        {
            std::lock_guard<std::mutex> lock(m_ChannelMutex);
            isChannelValid = (m_channel != nullptr);
        }

        if(!isChannelValid)
        {
            WARN("Sending data to a closed connection: %s -> %s, %lu bytes lost",
                    m_local.toString().c_str(), m_peer.toString().c_str(), len);
            return;
        }

        // 若输出缓冲区为空，尝试直接发送
        if(m_outputBuffer.empty())
        {
            ssize_t sended = _send(buf, len);
            buf += sended;
            len -= sended;
        }

        // 将剩余数据加入输出缓冲区
        if(len > 0)
        {
            m_outputBuffer.append(buf, len);
        }
    }

    void TcpConn::onMsg(std::unique_ptr<CodecBase> codec, const MsgCallBack& cb)
    {
        std::lock_guard<std::mutex> lock(m_callBacksMutex);
        FATAL_IF(m_readCB, "onMsg and onReadable are mutually exclusive");

        m_codec = std::move(codec);
        m_readCB = [cb](const TcpConnPtr& conn)
        {
            int r = 1;
            while(r > 0)
            {
                Slice msg;
                r = conn->m_codec->tryDecode(conn->getInputBuffer(), msg);
                // 若解码错误，关闭连接
                if(r < 0)
                {
                    conn->m_channel->close();
                    break;
                }
                else if(r > 0)
                {
                    TRACE("Decoded a message. Original length: %d, message length: %ld",
                            r, msg.size());
                    cb(conn, msg);
                    conn->getInputBuffer().consume(r);
                }
            }
        };
    }

    void TcpConn::sendMsg(const Slice& msg)
    {
        if(m_codec)
        {
            m_codec->encode(msg, getOutputBuffer());
            sendOutputBuffer();
        }
        else
            ERROR("sendMsg called without codec");
    }

    void TcpConn::closeNow()
    {
        Channel* ch = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_ChannelMutex);
            ch = m_channel;
            m_channel = nullptr;
        }
        if(ch)
        {
            ch->close();
            delete ch;
        }
    }

    TcpServer::TcpServer(EventBases* bases) :
        m_bases(bases),
        m_listenChannel(nullptr),
        m_addr(0u),
        m_createCB([](){ return TcpConnPtr(new TcpConn); })
        {
            m_base = bases->allocBase();
            FATAL_IF(!m_base, "Failed to allocate event base");
        }

    TcpServer::~TcpServer()
    {
        std::lock_guard<std::mutex> lock(m_ChannelMutex);
        delete m_listenChannel;
    }

    int TcpServer::bind(const std::string& host, unsigned short port, bool isReusePort)
    {
        m_addr = Ipv4Addr(host, port);

        int fd = socket(AF_INET, SOCK_STREAM, 0);
        FATAL_IF(fd < 0, "socket creation failed: errno=%d, msg=%s", errno, strerror(errno));

        // 设置地址重用
        int r = Net::setReuseAddr(fd, true);
        FATAL_IF(!r, "Failed to set SO_REUSEADDR: errno=%d, msg=%s", r, strerror(r));

        // 设置端口重用
        if(isReusePort)
        {
            r = Net::setReusePort(fd, true);
            FATAL_IF(!r, "Failed to set SO_REUSEPORT: errno=%d, msg=%s", r, strerror(r));
        }

        // 设置FD_CLOEXEC标识
        r = utils::addFdFlag(fd, FD_CLOEXEC);
        FATAL_IF(r, "Failed to set FD_CLOEXEC: errno=%d, msg=%s", errno, strerror(errno));

        // 绑定地址
        r = ::bind(fd, (struct sockaddr*)&m_addr.getAddr(), sizeof(struct sockaddr));
        if(r != 0)
        {
            close(fd);
            ERROR("Bind to addr(%s) failed: errno=%d, msg=%s", m_addr.toString().c_str(), errno, strerror(errno));
            return errno;
        }

        // 开始监听
        r = listen(fd, MAX_WAIT_QUEUE_LENGTH);
        FATAL_IF(r, "Listen failed: errno=%d, msg=%s", errno, strerror(errno));

        INFO("Listening on fd %d at %s", fd, m_addr.toString().c_str());

        // 创建监听通道
        {
            std::lock_guard<std::mutex> lock(m_ChannelMutex);
            m_listenChannel = new Channel(m_base, fd, kReadEvent);
            m_listenChannel->onRead([this]() {
                this->_handleAccept();
            });
        }

        return 0;
    }

    TcpServer::Ptr TcpServer::startServer(EventBases* bases, const std::string& host,
                                    unsigned short port, bool isReusePort)
    {
        Ptr p(new TcpServer(bases));
        int r = p->bind(host, port, isReusePort);
        return (r == 0) ? p : nullptr;
    }

    void TcpServer::_handleAccept()
    {
        int listenFd = -1;
        {
            std::lock_guard<std::mutex> lock(m_ChannelMutex);
            if(m_listenChannel)
            {
                listenFd = m_listenChannel->getFd();
            }
        }

        if(listenFd < 0)
            return;

        struct sockaddr_in remoteAddr;
        socklen_t remoteSize = sizeof(remoteAddr);
        int curFd;

        // 处理所有待接受的连接
        while((curFd = accept(listenFd, (struct sockaddr*)&remoteAddr, &remoteSize)) >= 0)
        {
            // 获取对端地址
            sockaddr_in local, peer;
            socklen_t addrSize = sizeof(peer);
            int r = getpeername(curFd, (struct sockaddr*)&peer, &addrSize);
            if(r < 0)
            {
                close(curFd);
                ERROR("getpeername failed: errno=%d, msg=%s", errno, strerror(errno));
                continue;
            }

            // 获取本地地址
            r = getsockname(curFd, (struct sockaddr*)&local, &addrSize);
            if(r < 0)
            {
                close(curFd);
                ERROR("getsockname failed: errno=%d, msg=%s", errno, strerror(errno));
                continue;
            }

            // 设置FD_CLOEXEC标志
            r = utils::addFdFlag(curFd, FD_CLOEXEC);
            if(r != 0)
            {
                close(curFd);
                ERROR("Failed to set FD_CLOEXEC flag: errno=%d, msg=%s", errno, strerror(errno));
                continue;
            }

            // 从事件循环组中分配一个事件循环
            EventBase* base = m_bases->allocBase();
            FATAL_IF(!base, "Failed to allocate EventBase");

            // 创建连接并初始化
            auto addConn = [=]()
            {
                TcpConnPtr conn;
                {
                    std::lock_guard<std::mutex> lock(m_callBacksMutex);
                    conn = m_createCB();
                }

                if(conn)
                {
                    conn->attach(base, curFd, Ipv4Addr(local), Ipv4Addr(peer));

                    {
                        std::lock_guard<std::mutex> lock(m_callBacksMutex);
                        if(m_stateCB)
                            conn->onState(m_stateCB);
                        if(m_readCB)
                            conn->onReadable(m_readCB);
                        if(m_msgCB && m_codec)
                        {
                            handy::CodecBase* clonedRaw = m_codec->clone();
                            conn->onMsg(std::unique_ptr<handy::CodecBase>(clonedRaw), m_msgCB);
                        }
                    }
                }
                else
                {
                    close(curFd);
                    ERROR("Failed to accept new connection");
                }
            };

            // 在适当的事件循环中执行连接初始化
            if(base == m_base)
                addConn();
            else
                base->safeCall(std::move(addConn));
        }

        if(errno != EAGAIN && errno != EINTR)
            WARN("appcet failed: errno=%d, msg=%s", errno, strerror(errno));
    }

    HSHA::HSHA(int threads) : m_threadPool(threads) {}

    HSHA::Ptr HSHA::startServer(EventBase* base, const std::string& host, 
                                    unsigned short port, int threads)
    {
        Ptr p = Ptr(new HSHA(threads));
        p->m_server = TcpServer::startServer(base, host, port);
        return p->m_server ? p : NULL;
    }

    void HSHA::onMsg(std::unique_ptr<CodecBase> codec, const RetMsgCallBack &cb)
    {
        if(!m_server)
            return;

        m_server->onConnMsg(std::move(codec), [this, cb](const TcpConnPtr& conn, const Slice& msg)
        {
            std::string input = msg.toString();
            m_threadPool.addTask([=]()
            {
                std::string output = cb(conn, input);
                if(output.size() > 0)
                {
                    m_server->getBase()->safeCall([=]()
                    {
                        // 检查连接是否有效
                        if(conn->getState() == TcpConn::State::CONNECTED)
                            conn->sendMsg(output);
                    });
                }
            });
        });
    }
}   // namespace handy