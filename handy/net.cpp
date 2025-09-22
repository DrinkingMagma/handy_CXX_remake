#pragma once
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
} // namespace handy