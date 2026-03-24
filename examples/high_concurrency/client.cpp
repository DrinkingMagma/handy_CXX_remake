#include "conn.h"
#include "logger.h"
#include "event_base.h"
#include <csignal>
#include <atomic>
#include <chrono>
#include <sstream>
#include <vector>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <future>

using namespace handy;
using namespace std::chrono;

static std::atomic<uint64_t> g_totalConnections{0};
static std::atomic<uint64_t> g_activeConnections{0};
static std::atomic<uint64_t> g_totalMessages{0};
static std::atomic<uint64_t> g_totalBytes{0};
static std::atomic<uint64_t> g_failedConnections{0};
static std::atomic<bool> g_running{true};

// 用于信号通知
static std::mutex g_mutex;
static std::condition_variable g_cv;
static bool g_shouldExit = false;

// 配置参数
static int g_connectionsPerThread = 250;  // 每个线程的连接数
static int g_numThreads = 4;              // 线程数
static int g_messageSize = 128;           // 消息大小
static int g_messagesPerSecond = 10;      // 每条连接每秒发送消息数

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
    
    if (elapsed >= 5) {
        uint64_t currentConnections = g_totalConnections.load();
        uint64_t currentMessages = g_totalMessages.load();
        uint64_t currentBytes = g_totalBytes.load();
        uint64_t activeConnections = g_activeConnections.load();
        uint64_t failedConnections = g_failedConnections.load();
        
        uint64_t newConnections = currentConnections - lastConnections;
        uint64_t newMessages = currentMessages - lastMessages;
        uint64_t newBytes = currentBytes - lastBytes;
        
        std::stringstream ss;
        ss << "[统计] 活跃连接: " << activeConnections
           << ", 总连接: " << currentConnections
           << ", 失败连接: " << failedConnections
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

void clientThread(int threadId) {
    EventBase base;
    std::vector<TcpConnPtr> connections;
    connections.reserve(g_connectionsPerThread);
    
    // 创建连接
    for (int i = 0; i < g_connectionsPerThread && g_running; i++) {
        TcpConnPtr conn = TcpConn::createConnection<TcpConn>(&base, "127.0.0.1", 9090);
        
        conn->onState([threadId, i](const TcpConnPtr& conn) {
            auto state = conn->getState();
            if (state == TcpConn::State::CONNECTED) {
                g_totalConnections++;
                g_activeConnections++;
                
                if (g_totalConnections % 100 == 0) {
                    std::stringstream ss;
                    ss << "线程 " << threadId << " 建立连接 " << i 
                       << ", 总连接: " << g_totalConnections.load();
                    logStats(ss.str());
                }
            } else if (state == TcpConn::State::CLOSED) {
                g_activeConnections--;
            } else if (state == TcpConn::State::FAILED) {
                g_failedConnections++;
                if (g_failedConnections % 10 == 0) {
                    std::stringstream ss;
                    ss << "连接失败数: " << g_failedConnections.load();
                    logStats(ss.str());
                }
            }
        });
        
        conn->onReadable([](const TcpConnPtr& conn) {
            Buffer& input = conn->getInputBuffer();
            g_totalBytes += input.size();
            input.clear();
        });
        
        connections.push_back(conn);
        
        // 每10个连接处理一次事件，避免一次性创建过多连接
        if (i % 10 == 0) {
            base.loopOnce(1);
        }
        
        // 检查是否需要退出
        if (!g_running) {
            break;
        }
        
        // 每50个连接增加延迟，避免连接风暴
        if (i % 50 == 0 && i > 0) {
            for (int j = 0; j < 5 && g_running; j++) {
                std::this_thread::sleep_for(milliseconds(10));
            }
        }
    }
    
    logStats("所有连接已建立，开始发送消息");
    
    // 准备消息
    std::string message(g_messageSize, 'X');
    auto lastSendTime = steady_clock::now();
    int messageCount = 0;
    
    // 主循环：发送消息
    while (g_running) {
        base.loopOnce(1);
        
        auto now = steady_clock::now();
        auto elapsed = duration_cast<milliseconds>(now - lastSendTime).count();
        
        // 控制发送速率
        if (elapsed >= 1000 / g_messagesPerSecond) {
            for (auto& conn : connections) {
                if (!g_running) break;
                if (conn->getState() == TcpConn::State::CONNECTED) {
                    conn->send(message);
                    g_totalMessages++;
                    g_totalBytes += message.size();
                    messageCount++;
                }
            }
            lastSendTime = now;
        }
        
        printStats();
    }
    
    // 关闭所有连接
    for (auto& conn : connections) {
        if (conn->getState() == TcpConn::State::CONNECTED) {
            conn->close();
        }
    }
    
    // 处理关闭事件，最多等待500ms
    for (int i = 0; i < 50; i++) {
        base.loopOnce(10);
    }
    
    std::stringstream ss;
    ss << "线程 " << threadId << " 退出，发送消息数: " << messageCount;
    logStats(ss.str());
}

void sigIntHandler(int) {
    g_running = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_shouldExit = true;
    }
    g_cv.notify_all();
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    // 设置日志：仅输出到文件，运行时清空原有日志
    Logger::getInstance().setLogLevel(Logger::LINFO);
    Logger::getInstance().setLogFileName("logs/client.log");
    Logger::getInstance().clear();
    
    // 注册信号处理
    signal(SIGINT, sigIntHandler);
    signal(SIGTERM, sigIntHandler);
    
    logStats("高并发客户端启动");
    std::stringstream ss;
    ss << "配置: " << g_numThreads << " 线程, " 
       << g_connectionsPerThread << " 连接/线程, "
       << "总计 " << (g_numThreads * g_connectionsPerThread) << " 连接";
    logStats(ss.str());
    
    // 创建客户端线程
    std::vector<std::thread> threads;
    for (int i = 0; i < g_numThreads; i++) {
        threads.emplace_back(clientThread, i);
    }
    
    // 等待信号或所有线程完成
    {
        std::unique_lock<std::mutex> lock(g_mutex);
        g_cv.wait(lock, [] { return g_shouldExit; });
    }
    
    logStats("收到退出信号，正在关闭...");
    
    // 通知所有线程退出
    g_running = false;
    
    // 等待所有线程，最多等待5秒
    for (auto& t : threads) {
        if (t.joinable()) {
            // 使用异步等待实现超时
            auto future = std::async(std::launch::async, [&t]() {
                t.join();
            });
            if (future.wait_for(seconds(5)) == std::future_status::timeout) {
                logStats("警告：线程等待超时，强制退出");
                // 分离线程，避免崩溃
                t.detach();
            }
        }
    }
    
    // 输出最终统计
    std::stringstream finalSS;
    finalSS << "最终统计 - 总连接: " << g_totalConnections.load()
            << ", 失败连接: " << g_failedConnections.load()
            << ", 总消息: " << g_totalMessages.load()
            << ", 总字节: " << g_totalBytes.load();
    logStats(finalSS.str());
    
    return 0;
}
