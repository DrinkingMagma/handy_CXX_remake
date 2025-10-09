#include "conn.h"
#include "logger.h"
#include "utils.h"
#include "thread_pool.h"
#include "poller.h"
#include <fcntl.h>

namespace handy
{
    TcpConn::TcpConn() :
        m_base(nullptr),
        m_channel(nullptr),
        m_state(State::INVALID),
        m_destPort(0),
        m_connectTimeout_ms(0),
        m_reconnectInterval_ms(-1),
        m_connectedTime_ms(0),
        m_local(0),
        m_peer(0) {}

    TcpConn::~TcpConn()
    {
        closeNow();
        TRACE("TcpConn destroyed: %s -> %s", m_local.toString().c_str(), m_peer.toString().c_str());
    }

    void TcpConn::_attach(EventBase* base, int fd, const Ipv4Addr& localIp, const Ipv4Addr& peerIp)
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
            m_channel = new Channel(base, fd, POLLIN | POLLOUT);
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
        _attach(base, fd, Ipv4Addr(local), addr);

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

    void TcpConn::_cleanUp(const TcpConnPtr& conn)
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
            _reconnect;
            return;
        }

        // 注销空闲回调
        for(const auto& idleId : m_idleIds)
        {
            if(m_base)
                handyUnregisterIdle(m_base, idleId);
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
                    handyUpdateIdle(m_base, idleId);

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
                _cleanUp(conn);
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
            _cleanUp(conn);
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
            _cleanUp(conn);
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
}   // namespace handy