#include "event_base.h"
#include "conn.h"
#include "logger.h"
#include "csignal"
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

    INFO("Starting %d connections, each sending %d messages", connCount, msgPerConn);

    std::unique_ptr<EventBase> base(new EventBase());
    g_base = base.get();

    std::vector<TcpConnPtr> clients;
    std::atomic<int> connectedCount{0};
    std::atomic<int> sendCount{0};
    std::atomic<int> recvCount{0};

    for (int i = 0; i < connCount; i++) {
        TcpConnPtr tcpClient = TcpConn::createConnection(base.get(), "127.0.0.1", 12345);
        if(tcpClient == nullptr)
        {
            ERROR("tcp_client %d start failed", i);
            continue;
        }

        tcpClient->onMsg(std::make_unique<LineCodec>(), [&recvCount](const TcpConnPtr& conn, const Slice& msg){
            INFO("Received from %s: %.*s", conn->getPeerStr().c_str(), (int)msg.size(), msg.data());
            recvCount++;
        });

        tcpClient->onState([&connectedCount, basePtr = base.get()](const TcpConnPtr& conn) {
            if (conn->getState() == TcpConn::State::CONNECTED) {
                INFO("Connected to server: %s", conn->getPeerStr().c_str());
                connectedCount++;
                conn->getChannel()->enableWrite(true);
            } else if (conn->getState() == TcpConn::State::CLOSED) {
                INFO("Disconnected from server");
                basePtr->exit();
            }
        });

        clients.push_back(tcpClient);
    }

    base->runAfter(500, [&clients, msgPerConn, &sendCount]() {
        for (auto& conn : clients) {
            if (conn->getState() == TcpConn::State::CONNECTED) {
                for (int i = 0; i < msgPerConn; i++) {
                    std::string msg = "Hello from client " + std::to_string(i) + "\n";
                    conn->send(msg);
                    sendCount++;
                }
            }
        }
    });

    base->loop();

    INFO("Test completed: sent %d messages, received %d responses", sendCount.load(), recvCount.load());
    
    g_base = nullptr;
    return 0;
}
