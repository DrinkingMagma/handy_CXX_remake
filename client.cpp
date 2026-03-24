#include "handy/handy.h"
#include <iostream>
using namespace handy;

EventBase *g_base = nullptr;
TcpConnPtr g_client = nullptr;

void sigIntHandler(int sig)
{
    (void)sig;
    std::cout << "got SIGINT, process exit" << std::endl;
    if(g_base)
        g_base->exit();
    exit(0);
}

int main(int argc, char *argv[]) { 
    signal(SIGINT, sigIntHandler);

    std::unique_ptr<EventBase> base(new EventBase);
    g_base = base.get();
    g_client = TcpConn::createConnection(g_base, "127.0.0.1", 8080);

    g_client->onState([](const TcpConnPtr& conn){
        // INFO("connection state: %d", conn->getState());
        if (conn->getState() == TcpConn::State::CONNECTED) {
            INFO("connected to server!");

            g_client->send("first hello server");
            INFO("send 'first hello server' to server");
            g_base->runAfter(100, [conn]() {
                conn->getChannel()->enableWrite(true);
            });
            g_base->runAfter(110, [conn]() {
                conn->getChannel()->enableWrite(false);
            });

        } else if (conn->getState() == TcpConn::State::CLOSED) {
            INFO("disconnected from server");
        }
    });

    g_client->onWritable([](const TcpConnPtr& conn){
        conn->send("second hello server");
        INFO("send 'second hello server' to server");
    });

    g_client->onReadable([](const TcpConnPtr& conn){ 
        INFO("receive from server: %s", conn->getInputBuffer().data().c_str());
    });

    g_base->loop();
    g_base = nullptr;

    return 0;
}