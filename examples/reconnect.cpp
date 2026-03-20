#include "conn.h"
#include "logger.h"
#include <csignal>
#include <thread>
#include <chrono>

using namespace handy;

static EventBase* g_base = nullptr;
static TcpConnPtr g_clientConn = nullptr;
static TcpServerPtr g_tcpServer = nullptr;
static bool g_running = true;

void setLogs(std::string mode)
{
    Logger::getInstance().setLogLevel(Logger::LINFO);
    if(mode == "server")
        Logger::getInstance().setLogFileName("./logs/reconnect_server.log");
    else
        Logger::getInstance().setLogFileName("./logs/reconnect_client.log");
    Logger::getInstance().clear();
}

void sigIntHandler(int sig)
{
    (void)sig;
    INFO("exit reconnect test");
    g_running = false;
    if (g_base) g_base->exit();
}

// 客户端连接状态回调
void onClientState(const TcpConnPtr& conn)
{
    auto state = conn->getState();
    if (state == TcpConn::State::CONNECTED) {
        INFO("Client connected to server: %s -> %s", 
             conn->getLocalStr().c_str(), conn->getPeerStr().c_str());
        // 发送一条消息
        conn->send("Hello from client!\n");
    }
    else if (state == TcpConn::State::CLOSED) {
        INFO("Client connection closed: %s", conn->getPeerStr().c_str());
    }
    else if (state == TcpConn::State::FAILED) {
        INFO("Client connection failed: %s", conn->getPeerStr().c_str());
    }
}

// 启动客户端连接（带重连功能）
void startClientWithReconnect(EventBase* base, const std::string& host, int port)
{
    g_clientConn = TcpConn::createConnection(base, host, port, 5000);
    
    // 设置重连间隔为3秒
    g_clientConn->setReconnectInterval(3000);
    
    // 设置状态回调
    g_clientConn->onState(onClientState);
    
    // 设置消息回调
    g_clientConn->onReadable([](const TcpConnPtr& conn) {
        Buffer& buf = conn->getInputBuffer();
        std::string msg = buf.data();
        INFO("Client received raw: %s", msg.c_str());
        buf.clear();
    });
    // g_clientConn->onMsg(std::make_unique<LineCodec>(), [](const TcpConnPtr& conn, const Slice& msg){
    //     INFO("Received message from %s: %.*s", conn->getPeerStr().c_str(), (int)msg.size(), msg.data());
    // });
    
    INFO("Client started, connecting to %s:%d with reconnect interval 3000ms", host.c_str(), port);
}

int main(int argc, char *argv[])
{
    signal(SIGINT, sigIntHandler);

    // 解析命令行参数
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <server|client> [host] [port]\n", argv[0]);
        fprintf(stderr, "  Example: %s server\n", argv[0]);
        fprintf(stderr, "  Example: %s client 127.0.0.1 12345\n", argv[0]);
        return -1;
    }

    bool isServer;
    std::string mode = argv[1];
    if (mode == "server") {
        isServer = true;
    } else if (mode == "client") {
        isServer = false;
    } else {
        fprintf(stderr, "Invalid mode: %s. Must be 'server' or 'client'\n", mode.c_str());
        return -1;
    }

    setLogs(mode);

    std::string host = "127.0.0.1";
    int port = 12345;
    if (argc > 2) {
        host = argv[2];
    }
    if (argc > 3) {
        port = std::atoi(argv[3]);
    }

    std::unique_ptr<EventBase> base(new EventBase());
    g_base = base.get();

    if (isServer) {
        // 服务器模式
        g_tcpServer = TcpServer::startServer(base.get(), host, port);
        if (g_tcpServer == nullptr) {
            ERROR("Server start failed on %s:%d", host.c_str(), port);
            return -1;
        }
        INFO("Server started on %s:%d", host.c_str(), port);
        INFO("Usage: Start client in another terminal with: ./reconnect client");

        g_tcpServer->onConnCreate([]() {
            return std::make_shared<TcpConn>();
        });

        g_tcpServer->onConnState([](const TcpConnPtr& conn) {
            if (conn->getState() == TcpConn::State::CONNECTED) {
                INFO("Server: Client connected from %s", conn->getPeerStr().c_str());
                conn->send("Welcome! Server is ready.\n");
            }
            else if (conn->getState() == TcpConn::State::CLOSED) {
                INFO("Server: Client disconnected from %s", conn->getPeerStr().c_str());
            }
        });

        g_tcpServer->onConnRead([](const TcpConnPtr& conn) {
            Buffer& buf = conn->getInputBuffer();
            std::string msg(buf.begin(), buf.size());
            INFO("Server received from %s: %s", conn->getPeerStr().c_str(), msg.c_str());
            buf.clear();
            
            // 回复客户端
            std::string reply = "Server echo: " + msg;
            conn->send(reply);
        });

        // g_tcpServer->onConnMsg(std::make_unique<LineCodec>(), [](const TcpConnPtr& conn, const Slice& msg){
        //     INFO("Server received from %s: %.*s", conn->getPeerStr().c_str(), (int)msg.size(), msg.data());
        //     // 回复客户端
        //     conn->send("Server echo: " + msg.toString() + "\n");
        // });
    } else {
        // 客户端模式（带重连功能）
        startClientWithReconnect(base.get(), host, port);
        
        // 定期发送心跳消息
        base->runAfter(5000, [](...) {
            if (g_clientConn && g_clientConn->getState() == TcpConn::State::CONNECTED) {
                INFO("Sending heartbeat...");
                g_clientConn->send("Heartbeat\n");
            }
        }, 5000);
    }

    base->loop();

    g_base = nullptr;
    g_clientConn = nullptr;
    INFO("Reconnect test exited");
    return 0;
}
