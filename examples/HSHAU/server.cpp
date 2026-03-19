#include "udp.h"
#include "logger.h"
#include <csignal>
#include <thread>

using namespace handy;

static EventBase* g_base = nullptr;
static HSHAU::Ptr g_hshau = nullptr;

void sigIntHandler(int sig)
{
    (void)sig;
    INFO("exit HSHAU server");
    if (g_hshau) g_hshau->exit();
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

    int threads = 4;
    if (argc > 1) {
        threads = std::atoi(argv[1]);
    }
    INFO("Starting HSHAU server with %d worker threads", threads);

    g_hshau = HSHAU::startServer(base.get(), "127.0.0.1", 12345, threads);
    if(g_hshau == nullptr)
    {
        ERROR("HSHAU server start failed");
        return -1;
    }
    INFO("HSHAU server start success, listening on 127.0.0.1:12345");

    g_hshau->onMsg([](const UdpServer::Ptr& server, const std::string& msg, Ipv4Addr addr){
        INFO("Received message from %s: %s", addr.toString().c_str(), msg.c_str());

        std::string response = "HSHAU server processed: " + msg;
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        return response;
    });

    base->loop();

    g_base = nullptr;
    g_hshau = nullptr;
    return 0;
}
