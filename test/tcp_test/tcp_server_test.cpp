#include "conn.h"
#include "logger.h"
#include <csignal>


using namespace handy;
bool g_exitFlag = false;

void sigIntHandler(int sig)
{
    (void)sig;
    g_exitFlag = true;
    TRACE("exit tcp_server_test.cpp");
}

int main(int argc, char *argv[]) 
{
    signal(SIGINT, sigIntHandler);

    Logger::getInstance().setLogLevel(Logger::LALL);
    Logger::getInstance().setLogFileName("./tcp_server_test_log.log");
    Logger::getInstance().clear();

    EventBase* base = new EventBase();
    TcpServerPtr tcp_server = TcpServer::startServer(base, "127.0.0.1", 12345);
    if(tcp_server == nullptr)
    {
        ERROR("tcp_server start failed");
        return -1;
    }
    TRACE("tcp_server start success");

    tcp_server->onConnState([](TcpConnPtr conn)
    { 
    });

    while(!g_exitFlag)
    {}

    return 0;
}