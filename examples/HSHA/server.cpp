#include "conn.h"
#include "logger.h"
#include <csignal>
#include <thread>
#include <atomic>

using namespace handy;

static EventBase* g_base = nullptr;
static HSHA::Ptr g_hsha = nullptr;

void sigIntHandler(int sig)
{
    (void)sig;
    INFO("exit HSHA server");
    if (g_hsha) g_hsha->exit();
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
    INFO("Starting HSHA server with %d worker threads", threads);

    g_hsha = HSHA::startServer(base.get(), "127.0.0.1", 12345, threads);
    if(g_hsha == nullptr)
    {
        ERROR("HSHA server start failed");
        return -1;
    }
    INFO("HSHA server start success, listening on 127.0.0.1:12345");

    g_hsha->onMsg(std::make_unique<LineCodec>(), [](const TcpConnPtr& conn, const std::string& msg){
        INFO("Received message from %s: %s", conn->getPeerStr().c_str(), msg.c_str());

        std::string response = "HSHA server processed: " + msg;
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        return response;
    });

    base->loop();

    g_base = nullptr;
    g_hsha = nullptr;
    return 0;
}
