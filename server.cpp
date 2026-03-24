#include <handy/handy.h>
#include <iostream>

using namespace handy;

EventBase *g_base = nullptr;
TcpServerPtr g_server = nullptr;

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

    g_server = TcpServer::startServer(g_base, "127.0.0.1", 8080);

    g_server->onConnCreate([](){
        TcpConnPtr conn = std::make_shared<TcpConn>();
        return conn;
    });

    g_server->onConnRead([](const TcpConnPtr& conn){
        std::string msg = conn->getInputBuffer().data();
        conn->getInputBuffer().clear();
        INFO("recv from client: %s", msg.c_str());
        conn->send("Hello client");
        INFO("send 'Hello client' to client");
    });

    g_base->loop();
    g_base = nullptr;

    return 0;
}