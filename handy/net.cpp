#include "net.h"
#include "logger.h"
#include "utils.h"
#include <fcntl.h>
#include <netinet/tcp.h>

namespace handy
{
    bool Net::setNonBlock(int fd, bool value, int *errCode)
    {
        if(fd < 0)
        {
            if(errCode)
                *errCode = EBADF;
            ERROR("Net::setNonBlock: invalid fd = %d", fd);
            return false;
        }

        int flags = fcntl(fd, F_GETFL, 0);
        if(flags < 0)
        {
            int err = errno;
            if(errCode)
                *errCode = err;
            ERROR("Net::setNonBlock: fcntl(%d, F_GETFL) failed, err = %d(%s)", fd, err, strerror(err));
            return false;
        }

        int newFlags = value ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
        if(newFlags == flags)
            return true;

        if(fcntl(fd, F_SETFL, newFlags) < 0)
        {
            int err = errno;
            if(errCode)
                *errCode = err;
            ERROR("Net::setNonBlock: fcntl(%d, F_SETFL, %d) failed, err = %d(%s)", fd, newFlags, err, strerror(err));
        }

        return true;
    }

    bool Net::setReuseAddr(int fd, bool value, int* errCode)
    {
        if(fd < 0)
        {
            if(errCode)
                *errCode = EBADF;
            ERROR("Net::setReuseAddr: invalid fd = %d", fd);
            return false;
        }

        int flag = value ? 1 : 0;
        if(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)) < 0)
        {
            int err = errno;
            if(errCode)
                *errCode = err;
            ERROR("Net::setReuseAddr: setsockopt(%d, SO_REUSEADDR) failed, err = %d(%s)", fd, err, strerror(err));
            return false;
        }

        return true;
    }

    bool Net::setReusePort(int fd, bool value, int* errCode)
    {
        if(fd < 0)
        {
            if(errCode)
                *errCode = EBADF;
            ERROR("Net::setReusePort: invalid fd = %d", fd);
            return false;
        }

        #ifndef SO_REUSEPORT
            if(value)
            {
                if(errCode)
                    *errCode = ENOTSUP;
                ERROR("Net::setReusePort: SO_REUSEPORT not supported on this platform");
                return true;
            }
        #else
            int flag = value ? 1 : 0;
            if(setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &flag, sizeof(flag)) < 0)
            {
                int err = errno;
                if(errCode)
                    *errCode = err;
                ERROR("Net::setReusePort: setsockopt(%d, SO_REUSEPORT) failed, err = %d(%s)", fd, err, strerror(err));
                return false;
            }
            return true;
        #endif
    }

    bool Net::setNoDelay(int fd, bool value, int *errCode)
    {
        if(fd < 0)
        {
            if(errCode)
                *errCode = EBADF;
            ERROR("Net::setNoDelay: invalid fd = %d", fd);
            return false;
        }

        int flag = value ? 1 : 0;
        if(setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) < 0)
        {
            int err = errno;
            if(errCode)
                *errCode = err;
            ERROR("Net::setNoDelay: setsockopt(%d, TCP_NODELAY) failed, err = %d(%s)", fd, err, strerror(err));
            return false;
        }

        return true;
    }

    void Ipv4Addr::initAddr(unsigned short port, uint32_t ipNetOrder)
    {
        memset(&m_addr, 0, sizeof(m_addr));
        m_addr.sin_family = AF_INET;
        m_addr.sin_port = Net::hton(port);
        m_addr.sin_addr.s_addr = ipNetOrder;
    }

    Ipv4Addr::Ipv4Addr(const std::string& host, unsigned short port)
    {
        if(host.empty())
        {
            initAddr(port, INADDR_ANY);
            return;
        }

        struct in_addr ipAddr;
        if(port::stringToAddr(host, &ipAddr))
        {
            initAddr(port, ipAddr.s_addr);
            return;
        }

        if(!port::getHostByName(host, ipAddr))
        {
            initAddr(port, INADDR_NONE);
            ERROR("Ipv4Addr::Ipv4Addr(): resolve host = %s failed", host.c_str());
            return;
        }

        initAddr(port, ipAddr.s_addr);
    }

    Ipv4Addr::Ipv4Addr(unsigned short port)
    {
        initAddr(port, INADDR_ANY);
    }
    
    Ipv4Addr::Ipv4Addr(const struct sockaddr_in& addr)
    {
        if(addr.sin_family != AF_INET)
        {
            initAddr(0, INADDR_NONE);
            ERROR("Ipv4Addr::Ipv4Addr(): invalid address family=%d (expected AF_INET = 2)", addr.sin_family);
            return;
        }
        m_addr = addr;
    }

    std::string Ipv4Addr::toString() const
    {
        if(!isIpValid())
            return "Ipv4Addr::toString(): invalid ip address";

        uint32_t ipHost = Net::ntoh(m_addr.sin_addr.s_addr);
        unsigned short portHost = Net::ntoh(m_addr.sin_port);

        return utils::format("%d.%d.%d.%d:%d", 
                (ipHost >> 24) & 0xff,
                (ipHost >> 16) & 0xff,
                (ipHost >> 8) & 0xff,
                (ipHost >> 0) & 0xff,
                portHost);
    }

    std::string Ipv4Addr::ip() const
    {
        if(!isIpValid()) 
            return "Ipv4Addr::ip(): invalid ip address";

        uint32_t ipHost = Net::ntoh(m_addr.sin_addr.s_addr);

        return utils::format("%d.%d.%d.%d", 
                (ipHost >> 24) & 0xff,
                (ipHost >> 16) & 0xff,
                (ipHost >> 8) & 0xff,
                (ipHost >> 0) & 0xff);
    }

    unsigned short Ipv4Addr::port() const
    {
        return Net::ntoh(m_addr.sin_port);
    }

    uint32_t Ipv4Addr::ipInt() const
    {
        if(!isIpValid()) 
            return 0;

        return Net::ntoh(m_addr.sin_addr.s_addr);
    }

    bool Ipv4Addr::isIpValid() const
    {
        return m_addr.sin_addr.s_addr != INADDR_NONE;
    }

    const struct sockaddr_in& Ipv4Addr::getAddr() const
    {
        return m_addr;
    }

    bool Ipv4Addr::hostToIp(const std::string& host, std::string& outIp)
    {
        Ipv4Addr addr(host, 0);
        if(addr.isIpValid())
        {
            outIp = addr.ip();
            return true;
        }
        outIp.clear();
        return false;
    }

    Buffer::Buffer()
        : m_buf(nullptr), m_b(0), m_e(0), m_cap(0), m_exp(512), m_mutex(new std::mutex()) {}

    Buffer::~Buffer()
    {
        delete[] m_buf;
    }

    Buffer::Buffer(const Buffer& other)
        : m_buf(nullptr), m_b(0), m_e(0), m_cap(0), m_exp(512), m_mutex(new std::mutex())
    {
        std::lock_guard<std::mutex> lock(*other.m_mutex); 
        _copyFrom(other);
    }

    Buffer& Buffer::operator=(const Buffer& other)
    {
        if(this != &other)
        {
            Buffer tmp(other);
            _swap(tmp);
        }
        return *this;
    }

    Buffer::Buffer(Buffer&& other) noexcept
        : m_buf(other.m_buf), m_b(other.m_b), m_e(other.m_e), 
        m_cap(other.m_cap), m_exp(other.m_exp), m_mutex(std::move(other.m_mutex))
    {
        // 将other的成员重置为“空状态”，避免析构时重复释放
        other.m_buf = nullptr;
        other.m_b = 0;
        other.m_e = 0;
        other.m_cap = 0;
        other.m_exp = 512;
        other.m_mutex.reset(new std::mutex());  // 确保other后续使用时锁有效
    }

    Buffer& Buffer::operator=(Buffer&& other) noexcept
    {
        if(this != &other)
        {
            std::lock_guard<std::mutex> lock(*m_mutex);  // 仅对当前对象加锁（保护自身资源）
            // 转移other的资源
            m_buf = other.m_buf;
            m_b = other.m_b;
            m_e = other.m_e;
            m_cap = other.m_cap;
            m_exp = other.m_exp;
            m_mutex = std::move(other.m_mutex);
            
            // 重置other
            other.m_buf = nullptr;
            other.m_b = 0;
            other.m_e = 0;
            other.m_cap = 0;
            other.m_exp = 512;
            other.m_mutex.reset(new std::mutex());
        }
        return *this;
    }

    void Buffer::clear()
    {
        std::lock_guard<std::mutex> lock(*m_mutex);
        delete[] m_buf;
        m_buf = nullptr;
        m_b = 0;
        m_e = 0;
        m_cap = 0;
    }

    size_t Buffer::size() const
    {
        std::lock_guard<std::mutex> lock(*m_mutex);
        return m_e - m_b;
    }

    bool Buffer::empty() const
    {
        std::lock_guard<std::mutex> lock(*m_mutex);
        return m_b == m_e;
    }

    std::string Buffer::data() const
    {
        std::lock_guard<std::mutex> lock(*m_mutex);
        return std::string(m_buf + m_b, m_e - m_b);
    }

    char *Buffer::begin() const
    {
        std::lock_guard<std::mutex> lock(*m_mutex);
        return m_buf + m_b;
    }

    char *Buffer::end() const
    { 
        std::lock_guard<std::mutex> lock(*m_mutex);
        return m_buf + m_e; 
    }

    size_t Buffer::space() const
    {
        std::lock_guard<std::mutex> lock(*m_mutex);
        return m_cap - m_e;
    }

    const char* Buffer::peek() const 
    {
        std::lock_guard<std::mutex> lock(*m_mutex);
        return m_buf + m_b;
    }

    void Buffer::makeRoom()
    {
        std::lock_guard<std::mutex> lock(*m_mutex);
        if(space() < m_exp)
            _expand(0);
    }

    char* Buffer::makeRoom(size_t len)
    {
        std::lock_guard<std::mutex> lock(*m_mutex);
        if(m_e + len > m_cap)
        {
            if(_size() + len < m_exp / 2)
                _moveHead();
            else
                _expand(len);
        }
        return m_buf + m_e;
    }

    void Buffer::addSize(size_t len)
    {
        std::lock_guard<std::mutex> lock(*m_mutex);
        m_e += len;
    }

    Buffer& Buffer::append(const char* p, size_t len)
    {
        if(len == 0 || !p)
            return *this;
        
        std::lock_guard<std::mutex> lock(*m_mutex);
        char* dst = _makeRoom(len);
        memcpy(dst, p, len);
        m_e += len;
        return *this;
    }

    Buffer& Buffer::appendUnSafe(const char* p, size_t len)
    {
        if(len == 0 || !p)
            return *this;
        
        char* dst = _makeRoom(len);
        memcpy(dst, p, len);
        m_e += len;
        return *this;
    }

    Buffer& Buffer::append(const char* p)
    {
        return append(p, strlen(p));
    }

    Buffer& Buffer::append(Slice slice)
    {
        return append(slice.data(), slice.size());
    }

    Buffer& Buffer::append(const std::string& str)
    {
        return append(str.data(), str.size());
    }

    Buffer& Buffer::consume(size_t len)
    {
        if(len == 0)
            return *this;
        
        std::lock_guard<std::mutex> lock(*m_mutex);
        size_t consumeLen = std::min(len, m_e - m_b);
        m_b += consumeLen;

        if(m_b == m_e)
        {
            // 不能使用clear()，会产生死锁
            delete[] m_buf;
            m_buf = nullptr;
            m_b = 0;
            m_e = 0;
            m_cap = 0;
        }
        return *this;
    }

    Buffer& Buffer::absorb(Buffer& other)
    {
        if(this == &other)
            return *this;

        // 按锁地址排序，确保加锁顺序固定
        std::mutex* lock1 = m_mutex.get();
        std::mutex* lock2 = other.m_mutex.get();
        if (lock1 > lock2) std::swap(lock1, lock2);

        std::lock_guard<std::mutex> lockA(*lock1);
        std::lock_guard<std::mutex> lockB(*lock2);

        if(other.m_b == other.m_e)
            return *this;

        if(m_b == m_e)
        {
            std::swap(m_buf, other.m_buf);
            std::swap(m_cap, other.m_cap);
            std::swap(m_b, other.m_b);
            std::swap(m_e, other.m_e);
        }
        else
        {
            appendUnSafe(other.m_buf + other.m_b, other.m_e - other.m_b);
            other.m_b = other.m_e;
            delete[] other.m_buf;
            other.m_buf = nullptr;
            other.m_cap = 0;
        }
        return *this;
    }

    void Buffer::setExpectGrowSize(size_t sz)
    {
        std::lock_guard<std::mutex> lock(*m_mutex);
        if(sz > 0)
            m_exp = sz;
    }

    Buffer::operator Slice() const
    {
        std::lock_guard<std::mutex> lock(*m_mutex);
        return Slice(m_buf + m_b, m_e - m_b);
    }

    char* Buffer::_makeRoom(size_t len)
    {
        if(m_e + len <= m_cap)
            return m_buf + m_e;

        size_t currentSize = m_e - m_b;
        if(currentSize + len < m_cap / 2)
            _moveHead();
        else
            _expand(len);
        return m_buf + m_e;
    }

    void Buffer::_expand(size_t len)
    {
        size_t currentSize = m_e - m_b;
        size_t newCap = std::max(m_exp, std::max(2 * m_cap, currentSize + len));
        char* newBuf = new char[newCap];

        if(currentSize > 0)
            memcpy(newBuf, m_buf + m_b, currentSize);

        delete[] m_buf;
        m_buf = newBuf;
        m_e = currentSize;
        m_b = 0;
        m_cap = newCap;
    }

    void Buffer::_moveHead()
    {
        if(m_b == 0)
            return;
        
        size_t currentSize = m_e - m_b;
        memmove(m_buf, m_buf + m_b, currentSize);
        m_e = currentSize;
        m_b = 0;
    }

    void Buffer::_copyFrom(const Buffer& other)
    {
        m_b = other.m_b;
        m_e = other.m_e;
        m_cap = other.m_cap;
        m_exp = other.m_exp;

        if(other.m_buf && other.m_cap > 0)
        {
            m_buf = new char[m_cap];
            memcpy(m_buf, other.m_buf, m_cap);
        }
        else
            m_buf = nullptr;
    }

    void Buffer::_swap(Buffer& other) noexcept
    {
        std::swap(m_buf, other.m_buf);
        std::swap(m_b, other.m_b);
        std::swap(m_e, other.m_e);
        std::swap(m_cap, other.m_cap);
        std::swap(m_exp, other.m_exp);
        std::swap(m_mutex, other.m_mutex);
    }
} // namespace handy