#include "udp.h"
#include "logger.h"
#include <csignal>
#include <unistd.h>

using namespace handy;
static EventBase* g_base = nullptr;

void sigIntHandler(int sig)
{
    (void)sig;
    INFO("exit server.cpp");
    if (g_base) g_base->exit();
}

int main(int argc, char *argv[]) 
{
    Logger::getInstance().setLogLevel(Logger::LALL);
    Logger::getInstance().setLogFileName("./server.log");
    Logger::getInstance().clear();

    signal(SIGINT, sigIntHandler);

    std::unique_ptr<EventBase> base(new EventBase());
    g_base = base.get();
    
    UdpServer::Ptr udpServer = UdpServer::startServer(base.get(), "127.0.0.1", 12345);
    if(udpServer == nullptr)
    {
        ERROR("udp_server start failed");
        return -1;
    }
    INFO("udp_server start success on %s", udpServer->getAddr().toString().c_str());

    udpServer->onMsg([&udpServer](const UdpServer::Ptr& server, Buffer buf, Ipv4Addr addr) {
        INFO("Received %d bytes from %s: %.*s", 
             (int)buf.size(), 
             addr.toString().c_str(),
             (int)buf.size(), 
             buf.begin());
        
        std::string msg(buf.begin(), buf.end());
        server->sendTo(msg + "\n", addr);
    });

    base->loop();

    g_base = nullptr;
    return 0;
}
