/**
 * @file udp.h
 * @brief UDP服务器和客户端连接的实现
*/

#include "udp.h"
#include "fcntl.h"
#include "logger.h"

namespace handy
{
    UdpServer::UdpServer(EventBases* bases) : m_bases(bases)
    {
        FATAL_IF(m_bases == nullptr, "m_bases is nullptr");
        m_base = m_bases->allocBase();
        FATAL_IF(m_base == nullptr, "m_bases allocate base failed");
    }

    UdpServer::~UdpServer()
    {
        if(m_channel)
        {
            // 在事件循环线程中安全删除通道
            m_base->safeCall([this]() { delete m_channel; });
            m_channel = nullptr;
        }
    }

    int UdpServer::bind(const std::string& host, unsigned short port, bool isReusePort)
    {
        m_addr = Ipv4Addr(host, port);

        // 创建UDP套接字
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if(fd < 0)
        {
            ERROR("Create socket failed: errno=%d, msg=%s", errno, strerror(errno));
            return errno;
        }

        // 设置地址复用
        int r = Net::setReuseAddr(fd, true);
        if(r != 1)
        {
            ERROR("Set reuse addr failed: errno=%d, msg=%s", errno, strerror(errno));
            close(fd);
            return errno;
        }

        // 设置端口复用
        if(isReusePort)
        {
            r = Net::setReusePort(fd, true);
            if(r != 1)
            {
                ERROR("Set reuse port failed: errno=%d, msg=%s", errno, strerror(errno));
                close(fd);
                return errno;
            }
        }

        // 设置FD_CLOEXEC标志
        r = utils::addFdFlag(fd, FD_CLOEXEC);
        if(r != 0)
        {
            ERROR("Set FD_CLOEXEC failed: errno=%d, msg=%s", errno, strerror(errno));
            close(fd);
            return errno;
        }

        // 绑定地址
        struct sockaddr_in sockAddr = m_addr.getAddr();
        r = ::bind(fd, reinterpret_cast<struct sockaddr*>(&sockAddr), sizeof(sockAddr));
        if(r != 0)
        {
            ERROR("Bind to %s failed: errno=%d, msg=%s", m_addr.toString(), errno, strerror(errno));
            close(fd);
            return errno;
        }

        // 设置非阻塞模式
        if(Net::setNonBlock(fd) != 1)
        {
            ERROR("Set non-block failed: errno=%d, msg=%s", errno, strerror(errno));
            close(fd);
            return errno;
        }

        INFO("UDP server(fd=%d) bind to %s success", fd, m_addr.toString().c_str());

        // 创建通道并设置读事件回调：读取数据并调用消息处理回调函数
        m_channel = new Channel(m_base, fd, kReadEvent);
        m_channel->onRead([this]() {
            if(!m_channel || m_channel->getFd() < 0)
                return;

            Buffer buf;
            struct sockaddr_in remoteAddr;
            socklen_t remoteAddrLen = sizeof(remoteAddr);
            int fd = m_channel->getFd();

            TRACE("Udp server(fd=%d) recving...", fd);
            while(true)
            {   
                ssize_t rn = recvfrom(fd, buf.makeRoom(kUdpPacketSize), kUdpPacketSize, 0,
                    reinterpret_cast<struct sockaddr*>(&remoteAddr), &remoteAddrLen);
                
                if(rn > 0)
                {
                    buf.addSize(static_cast<size_t>(rn));
                    TRACE("Udp server(fd=%d) recvfrom %ld bytes from %s", fd, rn, Ipv4Addr(remoteAddr).toString().c_str());
                    TRACE("Udp server(fd=%d) recvfrom %s", fd, buf.data().c_str());

                    if(m_serverMsgCallback)
                    {
                        m_serverMsgCallback(shared_from_this(), buf, Ipv4Addr(remoteAddr));
                    }
                }
                // 没有更多数据，退出循环
                else if(rn < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                {
                    break;
                }
                else if(rn < 0 && errno != EINTR)
                {
                    ERROR("UDP server(fd=%d) recvfrom failed: errno=%d, msg=%s", fd, errno, strerror(errno));
                    break;
                }
                else
                    break;
            }
        });

        return 0;
    }

    UdpServer::Ptr UdpServer::startServer(EventBases* bases, const std::string& host,
                                            unsigned short port, bool isReusePort)
    {
        if(bases == nullptr)
        {
            ERROR("EventBases is nullptr");
            return nullptr;
        }

        Ptr server(new UdpServer(bases));
        int r = server->bind(host, port, isReusePort);

        if(r != 0)
        {
            ERROR("UdpServer bind failed: errno=%d, msg=%s", r, strerror(r));
            return nullptr;
        }
        return server;
    }

    void UdpServer::sendTo(const char* buf, size_t len, Ipv4Addr addr)
    {
        if(!m_channel || m_channel->getFd() < 0)
        {
            WARN("UdpServer send %d bytes to %s failed: channel is nullptr or channel fd < 0", len, addr.toString().c_str());
            return;
        }

        int fd= m_channel->getFd();
        struct sockaddr_in sockAddr = addr.getAddr();
        ssize_t wn = sendto(fd, buf, len, 0, reinterpret_cast<struct sockaddr*>(&sockAddr), sizeof(sockAddr));
        if(wn < 0)
        {
            ERROR("UdpServer(fd=%d) send %d bytes to %s failed: errno=%d, msg=%s", fd, len, addr.toString().c_str(), errno, strerror(errno));
            return;
        }

        TRACE("UdpServer(fd=%d) send %d bytes to %s success", fd, wn, addr.toString().c_str());
    }

    void UdpServer::sendTo(Buffer msg, Ipv4Addr addr)
    {
        sendTo(msg.peek(), msg.size(), addr);
        msg.clear();
    }

    void UdpServer::sendTo(const std::string& s, Ipv4Addr addr)
    {
        sendTo(s.c_str(), s.size(), addr);
    }

    void UdpServer::sendTo(const char* s, Ipv4Addr addr)
    {
        sendTo(s, strlen(s), addr);
    }

    UdpConn::~UdpConn()
    {
        close();
    }

    UdpConn::Ptr UdpConn::createConnection(EventBase* base, const std::string& destHost,
                                            unsigned short destPort)
    {
        if(!base)
        {
            ERROR("EventBase is nullptr");
            return nullptr;
        }

        Ipv4Addr destAddr(destHost, destPort);
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if(fd < 0)
        {
            ERROR("Create socket failed: errno=%d, msg=%s", errno, strerror(errno));
            return nullptr;
        }

        // 设置为非阻塞模式
        if(Net::setNonBlock(fd) != 1)
        {
            ERROR("Set non-block failed: errno=%d, msg=%s", errno, strerror(errno));
            ::close(fd);
            return nullptr;
        }

        // 设置FD_CLOEXEC标志
        int r = utils::addFdFlag(fd, FD_CLOEXEC);
        if(r != 0)
        {
            ERROR("Set FD_CLOEXEC failed: errno=%d, msg=%s", errno, strerror(errno));
            ::close(fd);
            return nullptr;
        }

        // 连接到目标地址
        struct sockaddr_in sockAddr = destAddr.getAddr();
        r = ::connect(fd, reinterpret_cast<struct sockaddr*>(&sockAddr), sizeof(sockAddr));
        if(r != 0 && errno != EINPROGRESS)
        {
            ERROR("Connect to %s failed: errno=%d, msg=%s", destAddr.toString().c_str(), errno, strerror(errno));
            ::close(fd);
            return nullptr;
        }

        TRACE("UDP connection(fd=%d) connect to %s success", fd, destAddr.toString().c_str());

        Ptr conn(new UdpConn);
        conn->m_destHost = destHost;
        conn->m_destPort = destPort;
        conn->m_base = base;
        conn->m_peer = destAddr;
        conn->m_channel = new Channel(base, fd, kReadEvent);

        // 设置读事件回调
        conn->m_channel->onRead([conn]() 
        { 
            TRACE("UDP connection(fd=%d) recving...");
            if(!conn->m_channel || conn->m_channel->getFd() < 0)
            {
                WARN("conn closing: conn->m_channel=%p, conn->m_channel->getFd()=%d", conn->m_channel, conn->m_channel->getFd());
                conn->close();
                return;
            }

            TRACE("UDP connection(fd=%d) recving...");

            Buffer input;
            int fd = conn->m_channel->getFd();
            TRACE("UDP connection(fd=%d) recvfrom %s", fd, conn->m_peer.toString().c_str());
            ssize_t rn = ::read(fd, input.makeRoom(kUdpPacketSize), kUdpPacketSize);
            TRACE("UDP connection(fd=%d) recvfrom %ld bytes from %s", fd, rn, conn->m_peer.toString().c_str());
            if(rn < 0)
            {
                ERROR("UDP connection(fd=%d) read failed: errno=%d, msg=%s", 
                        fd, errno, strerror(errno));
                return;
            }

            TRACE("UDP connection(fd=%d) read %ld bytes from %s", fd, rn, conn->m_peer.toString().c_str());
            input.addSize(static_cast<size_t>(rn));

            if(conn->m_udpMsgCallback)
            {
                conn->m_udpMsgCallback(conn, input);
            }
        });

        return conn;
    }

    void UdpConn::close()
    {
        if(!m_channel)
            return;

        // 保存通道指针并在事件循环中安全删除
        Channel* channel = m_channel;
        m_channel = nullptr;
        if(m_base)
        {
            m_base->safeCall([channel]()
            {
                delete channel;
            });
        }
        else
        {
            delete channel;
        }
    }

    void UdpConn::send(const char* buf, size_t len)
    {
        if(!m_channel || m_channel->getFd() < 0)
        {
            WARN("UDP connection send %d bytes to %s failed: channel is nullptr or channel fd < 0", 
                    len, m_peer.toString().c_str());
            return;
        }

        int fd = m_channel->getFd();
        ssize_t wn = ::write(fd, buf, len);
        if(wn < 0)
        {
            ERROR("UDP connection(fd=%d) send %d bytes to %s failed: errno=%d, msg=%s", 
                    fd, len, m_peer.toString().c_str(), errno, strerror(errno));
            return;
        }

        TRACE("UDP connection(fd=%d) send %d bytes to %s success", fd, wn, m_peer.toString().c_str());
    }

    void UdpConn::send(Buffer msg)
    {
        send(msg.peek(), msg.size());
        msg.clear();
    }

    void UdpConn::send(const std::string& s)
    {
        send(s.data(), s.size());
    }

    void UdpConn::send(const char* s)
    {
        send(s, strlen(s));
    }

    HSHAU::HSHAU(int threads) : m_threadPool(threads) {}

    HSHAU::Ptr HSHAU::startServer(EventBase* base, const std::string& host, 
                                    unsigned short port, int threads)
    {
        if(!base)
        {
            ERROR("EventBase is nullptr");
            return nullptr;
        }

        Ptr server(new HSHAU(threads));
        server->m_server = UdpServer::startServer(base, host, port);

        if(!server->m_server)
            return nullptr;

        return server;
    }

    void HSHAU::exit()
    {
        m_threadPool.exit();
        m_threadPool.join();
    }

    void HSHAU::onMsg(const RetMsgCallback& cb)
    {
        if(!m_server)
        {
            ERROR("UdpServer is nullptr");
            return;
        }

        m_server->onMsg([this, cb](const UdpServer::Ptr& server, Buffer buf, Ipv4Addr peerAddr)
        {
            std::string input(buf.data(), buf.size());

            // 提交任务到线程池
            m_threadPool.addTask([=]()
            {
                std::string output = cb(server, input, peerAddr);

                // 在事件循环线程中发送响应
                if(!output.empty() && server->getBase())
                {
                    server->getBase()->safeCall([=]()
                    {
                        server->sendTo(output, peerAddr);
                    });
                }
            });
        });
    }
}   // namespace handy