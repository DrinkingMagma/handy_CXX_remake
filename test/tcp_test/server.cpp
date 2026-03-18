#include "conn.h"
#include "logger.h"
#include <csignal>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>

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
    TcpServerPtr tcpServer = TcpServer::startServer(base.get(), "127.0.0.1", 12345);
    if(tcpServer == nullptr)
    {
        ERROR("tcp_server start failed");
        return -1;
    }
    INFO("tcp_server start success");

    tcpServer->onConnCreate([]() {
        TcpConnPtr conn = std::make_shared<TcpConn>();

        conn->addIdleCB(10 * 1000, [](const TcpConnPtr& conn){
            INFO("Connection idle timeout, closing...");
            conn->close();
        });
        return conn;
    });

    tcpServer->onConnState([](const TcpConnPtr& conn)
    {
        if(conn->getState() == TcpConn::State::CONNECTED)
        {
            INFO("New connection from %s created", conn->getPeerStr().c_str());
        }
        else if(conn->getState() == TcpConn::State::CLOSED)
        {
            INFO("Old connection from %s closed", conn->getPeerStr().c_str());
        }
    });

    tcpServer->onConnMsg(std::make_unique<LineCodec>(), [](const TcpConnPtr& conn, const Slice& msg){
        INFO("Received message from %s: %.*s", conn->getPeerStr().c_str(), (int)msg.size(), msg.data());

        conn->send(msg.toString() + "\n");
    });


    base->loop();

    g_base = nullptr;
    return 0;
}