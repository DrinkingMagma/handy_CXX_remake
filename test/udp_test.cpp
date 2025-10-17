// udp_test.cpp
#include "udp.h"
#include "event_base.h"
#include "logger.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <thread>
#include <atomic>
#include <unistd.h>
#include <chrono>
#include <memory>
#include <functional>

using namespace handy;

// 测试用全局变量
std::atomic<int> g_udpTestCount(0);
std::atomic<bool> g_serverReady(false);
std::atomic<bool> g_msgReceived(false);
std::string g_testMsg = "Hello UDP Test";
std::string g_testResponse = "Received: Hello UDP Test";

// 测试辅助函数
void initTestLogger() {
    Logger::getInstance().setLogFileName("udp_test.log");
    Logger::getInstance().setLogLevel(Logger::LogLevel::LDEBUG);
    INFO("=== udp_test 测试开始 ===");
}

void destroyTestLogger() {
    INFO("=== udp_test 测试结束 ===");
}

// 测试UdpServer基础功能
void test_udp_server_basic() {
    DEBUG("=== 开始测试 UdpServer 基础功能 ===");
    
    EventBase* base = new EventBase();
    bool testOk = false;

    // 启动服务器
    auto server = UdpServer::startServer(base, "127.0.0.1", 12345);
    if (!server) {
        ERROR("UdpServer 启动失败");
        return;
    }

    // 设置消息回调
    server->onMsg([&](const UdpServer::Ptr& srv, Buffer buf, Ipv4Addr peer) {
        std::string recvMsg(buf.data(), buf.size());
        DEBUG("服务器收到消息: %s 来自 %s", recvMsg.c_str(), peer.toString().c_str());
        
        if (recvMsg == g_testMsg) {
            srv->sendTo(g_testResponse, peer);
            testOk = true;
        }
    });

    // 启动客户端发送消息
    std::thread client([&]() {
        // 等待服务器准备就绪
        while (!g_serverReady) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        EventBase clientBase;
        auto conn = UdpConn::createConnection(&clientBase, "127.0.0.1", 12345);
        if (!conn) {
            ERROR("UdpConn 创建失败");
            return;
        }

        conn->onMsg([&](const UdpConn::Ptr&, Buffer buf) {
            std::string resp(buf.data(), buf.size());
            DEBUG("客户端收到响应: %s", resp.c_str());
            if (resp == g_testResponse) {
                g_msgReceived = true;
                clientBase.exit();
            }
        });

        conn->send(g_testMsg);
        clientBase.loop();
    });

    g_serverReady = true;
    // 运行事件循环一段时间
    base->runAfter(1000, [base]() { base->exit(); });
    base->loop();
    client.join();

    DEBUG("测试1（服务器客户端通信）：%s", (testOk && g_msgReceived) ? "通过" : "失败");
    DEBUG("=== UdpServer 基础功能测试结束 ===\n");
}

// 测试UdpConn功能
void test_udp_conn() {
    DEBUG("=== 开始测试 UdpConn 功能 ===");
    
    EventBase base;
    bool testOk = false;

    // 启动服务器
    auto server = UdpServer::startServer(&base, "127.0.0.1", 12346);
    if (!server) {
        ERROR("UdpServer 启动失败");
        return;
    }

    server->onMsg([&](const UdpServer::Ptr& srv, Buffer buf, Ipv4Addr peer) {
        std::string msg(buf.data(), buf.size());
        DEBUG("服务器收到消息: %s", msg.c_str());
        srv->sendTo(msg, peer); // 回声服务
    });

    // 创建客户端连接
    auto conn = UdpConn::createConnection(&base, "127.0.0.1", 12346);
    if (!conn) {
        ERROR("UdpConn 创建失败");
        return;
    }

    conn->onMsg([&](const UdpConn::Ptr&, Buffer buf) {
        std::string resp(buf.data(), buf.size());
        DEBUG("客户端收到回声: %s", resp.c_str());
        if (resp == g_testMsg) {
            testOk = true;
            base.exit();
        }
    });

    // 发送测试消息
    conn->send(g_testMsg);
    base.runAfter(1000, [&]() { base.exit(); }); // 超时退出
    base.loop();

    DEBUG("测试1（连接回声）：%s", testOk ? "通过" : "失败");
    DEBUG("=== UdpConn 功能测试结束 ===\n");
}

// 测试HSHAU半同步半异步服务器
void test_hshau_server() {
    DEBUG("=== 开始测试 HSHAU 服务器 ===");
    
    EventBase base;
    bool testOk = false;

    // 启动HSHAU服务器
    auto hshau = HSHAU::startServer(&base, "127.0.0.1", 12347, 4);
    if (!hshau) {
        ERROR("HSHAU 启动失败");
        return;
    }

    // 设置消息处理回调
    hshau->onMsg([](const UdpServer::Ptr&, const std::string& input, Ipv4Addr) {
        DEBUG("HSHAU处理消息: %s", input.c_str());
        return "HSHAU: " + input;
    });

    // 启动客户端
    std::thread client([&]() {
        EventBase clientBase;
        auto conn = UdpConn::createConnection(&clientBase, "127.0.0.1", 12347);
        if (!conn) {
            ERROR("客户端连接创建失败");
            return;
        }

        conn->onMsg([&](const UdpConn::Ptr&, Buffer buf) {
            std::string resp(buf.data(), buf.size());
            DEBUG("HSHAU客户端收到: %s", resp.c_str());
            if (resp == "HSHAU: " + g_testMsg) {
                testOk = true;
                clientBase.exit();
            }
        });

        conn->send(g_testMsg);
        clientBase.runAfter(1000, [&]() { clientBase.exit(); });
        clientBase.loop();
    });

    base.runAfter(1000, [&]() { base.exit(); });
    base.loop();
    client.join();

    DEBUG("测试1（HSHAU通信）：%s", testOk ? "通过" : "失败");
    DEBUG("=== HSHAU 服务器测试结束 ===\n");
}

// 测试多线程并发访问
void test_udp_thread_safe() {
    DEBUG("=== 开始测试 UDP 线程安全 ===");
    
    const int THREAD_NUM = 5;
    const int MSG_PER_THREAD = 20;
    EventBase* base = new EventBase();
    std::atomic<int> msgCount(0);

    // 启动服务器
    auto server = UdpServer::startServer(base, "127.0.0.1", 12348);
    if (!server) {
        ERROR("UdpServer 启动失败");
        return;
    }

    server->onMsg([&](const UdpServer::Ptr& srv, Buffer buf, Ipv4Addr peer) {
        msgCount++;
        srv->sendTo("ACK", peer);
    });

    // 启动多个客户端线程
    auto clientFunc = [&]() {
        EventBase clientBase;
        auto conn = UdpConn::createConnection(&clientBase, "127.0.0.1", 12348);
        if (!conn) return;

        int ackCount = 0;
        conn->onMsg([&](const UdpConn::Ptr&, Buffer) {
            if (++ackCount >= MSG_PER_THREAD) {
                clientBase.exit();
            }
        });

        for (int i = 0; i < MSG_PER_THREAD; ++i) {
            conn->send(g_testMsg + std::to_string(i));
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }

        clientBase.runAfter(1000, [&]() { clientBase.exit(); });
        clientBase.loop();
        g_udpTestCount += ackCount;
    };

    std::vector<std::thread> clients;
    for (int i = 0; i < THREAD_NUM; ++i) {
        clients.emplace_back(clientFunc);
    }

    // 运行服务器
    base->runAfter(2000, [&]() { base->exit(); });
    base->loop();

    // 等待所有客户端完成
    for (auto& t : clients) {
        t.join();
    }

    int expected = THREAD_NUM * MSG_PER_THREAD;
    bool testOk = (msgCount == expected) && (g_udpTestCount == expected);
    DEBUG("测试1（多线程并发）：总接收=%d, 总确认=%d, 预期=%d, %s",
          (int)msgCount, (int)g_udpTestCount, expected, testOk ? "通过" : "失败");
    DEBUG("=== UDP 线程安全测试结束 ===\n");
}

// 测试端口复用功能
void test_reuse_port() {
    DEBUG("=== 开始测试 端口复用 功能 ===");
    
    EventBase* base = new EventBase();
    bool testOk = false;

    // 第一个服务器绑定端口
    auto server1 = UdpServer::startServer(base, "127.0.0.1", 12349, true);
    if (!server1) {
        ERROR("第一个服务器启动失败");
        return;
    }

    // 第二个服务器尝试复用端口
    auto server2 = UdpServer::startServer(base, "127.0.0.1", 12349, true);
    if (server2) {
        testOk = true;
        DEBUG("第二个服务器成功复用端口");
    } else {
        ERROR("第二个服务器复用端口失败");
    }

    DEBUG("测试1（端口复用）：%s", testOk ? "通过" : "失败");
    DEBUG("=== 端口复用 功能测试结束 ===\n");
}

// 测试入口函数
void run_all_tests() {
    initTestLogger();
    
    test_udp_server_basic();
    test_udp_conn();
    test_hshau_server();
    test_udp_thread_safe();
    test_reuse_port();
    
    destroyTestLogger();
}

int main() {
    run_all_tests();
    return 0;
}