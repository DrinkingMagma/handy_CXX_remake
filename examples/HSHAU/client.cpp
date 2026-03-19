#include "udp.h"
#include "logger.h"
#include "event_base.h"
#include <csignal>
#include <atomic>
#include <vector>

using namespace handy;

static EventBase* g_base = nullptr;

void sigIntHandler(int sig)
{
    (void)sig;
    INFO("exit client");
    if (g_base) g_base->exit();
}

int main(int argc, char *argv[]) 
{
    Logger::getInstance().setLogLevel(Logger::LINFO);
    Logger::getInstance().setLogFileName("./client.log");
    Logger::getInstance().clear();

    signal(SIGINT, sigIntHandler);

    int connCount = 2;
    int msgPerConn = 5;
    
    if (argc > 1) {
        connCount = std::atoi(argv[1]);
    }
    if (argc > 2) {
        msgPerConn = std::atoi(argv[2]);
    }

    INFO("Starting %d UDP clients, each sending %d messages", connCount, msgPerConn);

    std::unique_ptr<EventBase> base(new EventBase());
    g_base = base.get();

    std::vector<UdpConn::Ptr> clients;
    std::atomic<int> sendCount{0};
    std::atomic<int> recvCount{0};

    for (int i = 0; i < connCount; i++) {
        UdpConn::Ptr udpClient = UdpConn::createConnection(base.get(), "127.0.0.1", 12345);
        if(udpClient == nullptr)
        {
            ERROR("udp_client %d start failed", i);
            continue;
        }
        INFO("UDP client %d created, connected to %s", i, udpClient->getPeerStr().c_str());

        udpClient->onMsg([&recvCount](const UdpConn::Ptr& conn, Buffer msg){
            INFO("Received from %s: %.*s", 
                 conn->getPeerStr().c_str(),
                 (int)msg.size(), 
                 msg.begin());
            recvCount++;
        });

        clients.push_back(udpClient);
    }

    base->runAfter(500, [&clients, msgPerConn, &sendCount, &recvCount, basePtr = base.get()]() {
        for (auto& conn : clients) {
            for (int i = 0; i < msgPerConn; i++) {
                std::string msg = "Hello from client " + std::to_string(i);
                conn->send(msg + "\n");
                sendCount++;
            }
        }
        
        basePtr->runAfter(2000, [&sendCount, &recvCount, basePtr]() {
            INFO("Summary: sent %d messages, received %d responses", sendCount.load(), recvCount.load());
            basePtr->exit();
        });
    });

    base->loop();

    g_base = nullptr;
    return 0;
}
