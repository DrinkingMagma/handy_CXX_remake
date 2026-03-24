#include "conn.h"
#include "logger.h"
#include "event_base.h"
#include <csignal>
#include <atomic>
#include <chrono>
#include <sstream>

using namespace handy;
using namespace std::chrono;

static EventBase* g_base = nullptr;
static std::atomic<uint64_t> g_totalConnections{0};
static std::atomic<uint64_t> g_activeConnections{0};
static std::atomic<uint64_t> g_totalMessages{0};
static std::atomic<uint64_t> g_totalBytes{0};
static std::atomic<bool> g_running{true};

void logStats(const std::string& message) {
    INFO("%s", message.c_str());
}

void printStats() {
    static auto lastTime = steady_clock::now();
    static uint64_t lastConnections = 0;
    static uint64_t lastMessages = 0;
    static uint64_t lastBytes = 0;
    
    auto now = steady_clock::now();
    auto elapsed = duration_cast<seconds>(now - lastTime).count();
    
    if (elapsed >= 5) { // 每5秒输出一次统计
        uint64_t currentConnections = g_totalConnections.load();
        uint64_t currentMessages = g_totalMessages.load();
        uint64_t currentBytes = g_totalBytes.load();
        uint64_t activeConnections = g_activeConnections.load();
        
        uint64_t newConnections = currentConnections - lastConnections;
        uint64_t newMessages = currentMessages - lastMessages;
        uint64_t newBytes = currentBytes - lastBytes;
        
        std::stringstream ss;
        ss << "[统计] 活跃连接: " << activeConnections 
           << ", 总连接: " << currentConnections
           << ", 新增连接/秒: " << (newConnections / elapsed)
           << ", 消息/秒: " << (newMessages / elapsed)
           << ", 字节/秒: " << (newBytes / elapsed);
        
        logStats(ss.str());
        
        lastTime = now;
        lastConnections = currentConnections;
        lastMessages = currentMessages;
        lastBytes = currentBytes;
    }
}

void sigIntHandler(int) {
    g_running = false;
    if (g_base) {
        g_base->exit();
    }
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    // 设置日志：仅输出到文件，运行时清空原有日志
    Logger::getInstance().setLogLevel(Logger::LINFO);
    Logger::getInstance().setLogFileName("logs/server.log");
    Logger::getInstance().clear();
    
    // 注册信号处理
    signal(SIGINT, sigIntHandler);
    signal(SIGTERM, sigIntHandler);
    
    // 创建事件循环
    std::unique_ptr<EventBase> base(new EventBase());
    g_base = base.get();
    
    // 创建TCP服务器
    TcpServerPtr server = TcpServer::startServer(base.get(), "", 9090, true);
    
    if (!server) {
        logStats("服务器启动失败，端口: 9090");
        return 1;
    }
    
    logStats("高并发服务器启动成功，监听端口: 9090");
    
    // 设置连接创建回调
    server->onConnCreate([]() {
        return std::make_shared<TcpConn>();
    });
    
    // 设置连接状态回调
    server->onConnState([](const TcpConnPtr& conn) {
        auto state = conn->getState();
        if (state == TcpConn::State::CONNECTED) {
            g_totalConnections++;
            g_activeConnections++;
            
            if (g_totalConnections % 1000 == 0) {
                std::stringstream ss;
                ss << "新连接建立，总连接数: " << g_totalConnections.load();
                logStats(ss.str());
            }
        } else if (state == TcpConn::State::CLOSED) {
            g_activeConnections--;
        }
    });
    
    // 设置数据读取回调
    server->onConnRead([](const TcpConnPtr& conn) {
        Buffer& input = conn->getInputBuffer();
        g_totalMessages++;
        g_totalBytes += input.size();
        
        // 简单回显
        conn->send(input);
        input.clear();
        
        printStats();
    });
    
    // 主循环
    while (g_running) {
        base->loopOnce(1000);
        printStats();
    }
    
    logStats("服务器正在关闭...");
    
    // 关闭所有连接
    server.reset();
    
    // 输出最终统计
    std::stringstream ss;
    ss << "最终统计 - 总连接: " << g_totalConnections.load()
       << ", 总消息: " << g_totalMessages.load()
       << ", 总字节: " << g_totalBytes.load();
    logStats(ss.str());
    
    return 0;
}
