#include "conn.h"
#include "logger.h"
#include "utils.h"
#include "thread_pool.h"

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
        }
    }
}   // namespace handy