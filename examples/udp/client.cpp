#include "udp.h"
#include "logger.h"
#include "event_base.h"
#include <csignal>

using namespace handy;

static EventBase* g_base = nullptr;

void sigIntHandler(int sig)
{
    (void)sig;
    INFO("exit client.cpp");
    if (g_base) g_base->exit();
}

int main(int argc, char *argv[]) 
{
    Logger::getInstance().setLogLevel(Logger::LALL);
    Logger::getInstance().setLogFileName("./client.log");
    Logger::getInstance().clear();

    signal(SIGINT, sigIntHandler);
    
    std::unique_ptr<EventBase> base(new EventBase());
    g_base = base.get();
    
    UdpConn::Ptr udpClient = UdpConn::createConnection(base.get(), "127.0.0.1", 12345);
    if(udpClient == nullptr)
    {
        ERROR("udp_client start failed");
        return -1;
    }
    INFO("udp_client start success, connected to %s", udpClient->getPeerStr().c_str());

    udpClient->onMsg([](const UdpConn::Ptr& conn, Buffer msg) {
        INFO("Received from %s: %.*s", 
             conn->getPeerStr().c_str(),
             (int)msg.size(), 
             msg.begin());
    });

    udpClient->send("hello\nworld\n");

    base->loop();

    g_base = nullptr;
    return 0;
}
