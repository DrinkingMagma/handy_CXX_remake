#include "conn.h"
#include "logger.h"
#include "codec.h"
#include <csignal>
#include <map>
#include <set>

using namespace handy;

static EventBase* g_base = nullptr;
static TcpServerPtr g_server = nullptr;
static std::map<TcpConnPtr, std::string> g_connNames;
static std::set<TcpConnPtr> g_conns;
// 需要延迟关闭的连接集合
static std::set<TcpConnPtr> g_pendingClose;

void sigIntHandler(int sig)
{
    (void)sig;
    INFO("exit chat server");
    if (g_base) g_base->exit();
}

void broadcastMessage(const TcpConnPtr& sender, const std::string& msg)
{
    std::string senderName = "[Unknown]";
    auto it = g_connNames.find(sender);
    if (it != g_connNames.end()) {
        senderName = it->second;
    }

    std::string broadcastMsg = "[" + senderName + "] " + msg;
    INFO("Broadcasting: %s to %d clients", broadcastMsg.c_str(), (int)g_conns.size());

    for (auto& conn : g_conns) {
        if (conn != sender && conn->getState() == TcpConn::State::CONNECTED) {
            conn->send(broadcastMsg + "\n");
        }
    }
}

// 处理延迟关闭的连接
void processPendingCloses()
{
    for (auto it = g_pendingClose.begin(); it != g_pendingClose.end(); ) {
        TcpConnPtr conn = *it;
        if (conn->getState() == TcpConn::State::CONNECTED) {
            conn->closeNow();
        }
        it = g_pendingClose.erase(it);
    }
}

int main(int argc, char *argv[]) 
{
    Logger::getInstance().setLogLevel(Logger::LALL);
    Logger::getInstance().setLogFileName("./logs/chat.log");
    Logger::getInstance().clear();

    signal(SIGINT, sigIntHandler);

    int port = 8080;
    if (argc > 1) {
        port = std::atoi(argv[1]);
    }

    std::unique_ptr<EventBase> base(new EventBase());
    g_base = base.get();

    g_server = TcpServer::startServer(base.get(), "127.0.0.1", port);
    if(g_server == nullptr)
    {
        ERROR("chat server start failed");
        return -1;
    }
    INFO("Chat server started on 127.0.0.1:%d", port);

    g_server->onConnCreate([]() {
        TcpConnPtr conn = std::make_shared<TcpConn>();
        return conn;
    });

    g_server->onConnState([](const TcpConnPtr& conn) {
        if(conn->getState() == TcpConn::State::CONNECTED) {
            std::string name = conn->getPeerStr();
            g_conns.insert(conn);
            g_connNames[conn] = name;
            INFO("Client connected: %s (total: %d)", name.c_str(), (int)g_conns.size());
            
            std::string welcome = "Welcome to chat server! You are [" + name + "]\n";
            conn->send(welcome);
            
            std::string msg = "[" + name + "] joined the chat\n";
            for (auto& c : g_conns) {
                if (c != conn && c->getState() == TcpConn::State::CONNECTED) {
                    c->send(msg);
                }
            }
        }
        else if(conn->getState() == TcpConn::State::CLOSED) {
            auto it = g_connNames.find(conn);
            if (it != g_connNames.end()) {
                std::string name = it->second;
                
                std::string msg = "[" + name + "] left the chat\n";
                for (auto& c : g_conns) {
                    if (c != conn && c->getState() == TcpConn::State::CONNECTED) {
                        c->send(msg);
                    }
                }
                
                INFO("Client disconnected: %s (remaining: %d)", name.c_str(), (int)g_conns.size() - 1);
                g_connNames.erase(it);
            }
            g_conns.erase(conn);
        }
    });

    g_server->onConnMsg(std::make_unique<LineCodec>(), [](const TcpConnPtr& conn, const Slice& msg) {
        std::string msgStr(msg.data(), msg.size());
        INFO("Received from %s: %s", conn->getPeerStr().c_str(), msgStr.c_str());
        
        if (msgStr == "quit") {
            INFO("Client %s requested to quit", conn->getPeerStr().c_str());
            conn->send("Goodbye!\n");
            // 将连接加入待关闭集合，在下次事件循环时关闭
            g_pendingClose.insert(conn);
            return;
        }
        
        broadcastMessage(conn, msgStr);
    });

    // 注册一个定时器，定期处理待关闭的连接
    base->runAfter(10, [](...) {
        processPendingCloses();
    }, 10);

    base->loop();

    g_base = nullptr;
    g_server = nullptr;
    return 0;
}
