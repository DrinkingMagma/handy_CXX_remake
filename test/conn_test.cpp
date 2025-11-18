#include "conn.h"
#include "logger.h"
#include "net.h"
#include <thread>
#include <vector>
#include <atomic>
#include <cstring>
#include <chrono>
#include <memory>
#include <cassert>

using namespace std;
using namespace handy;

// 全局计数变量，用于多线程测试
atomic<int> g_connTestCount(0);

/**
 * @brief 初始化Logger（输出到conn_test.log）
 */
void initConnTestLogger() {
    Logger::getInstance().setLogFileName("conn_test.log");
    Logger::getInstance().setLogLevel(Logger::LogLevel::LDEBUG);
    INFO("=== conn_test 测试开始 ===");
}

/**
 * @brief 测试TcpConn基本连接功能
 */
void test_TcpConn_basic() {
    DEBUG("=== 开始TcpConn基本功能测试 ===");
    EventBase base;

    // 启动测试服务器
    TcpServer::Ptr server = TcpServer::startServer(&base, "127.0.0.1", 8888);
    assert(server);
    bool serverConnected = false;
    bool serverReceived = false;
    server->onConnState([&](const TcpConnPtr& conn) {
        if (conn->getState() == TcpConn::State::CONNECTED) {
            serverConnected = true;
            DEBUG("服务器端：连接建立");
        }
    });
    server->onConnRead([&](const TcpConnPtr& conn) {
        Buffer& buf = conn->getInputBuffer();
        if (buf.size() > 0) {
            string data = buf.data();
            if (data == "hello server") {
                serverReceived = true;
                conn->send("hello client");
                DEBUG("服务器端：收到数据: %s", data.c_str());
            }
        }
    });

    // 启动客户端连接
    bool clientConnected = false;
    bool clientReceived = false;
    TcpConnPtr client = TcpConn::createConnection(&base, "127.0.0.1", 8888);
    client->onState([&](const TcpConnPtr& conn) {
        if (conn->getState() == TcpConn::State::CONNECTED) {
            clientConnected = true;
            conn->send("hello server");
            DEBUG("客户端：连接建立，发送数据");
        }
    });
    client->onReadable([&](const TcpConnPtr& conn) {
        Buffer& buf = conn->getInputBuffer();
        if (buf.size() > 0) {
            string data = buf.data();
            if (data == "hello client") {
                clientReceived = true;
                DEBUG("客户端：收到数据: %s", data.c_str());
                base.exit(); // 完成测试，退出事件循环
            }
        }
    });

    // 运行事件循环（最多等待3秒）
    thread t([&]() { base.loop(); });
    this_thread::sleep_for(chrono::seconds(3));
    base.exit();
    t.join();

    // 验证测试结果
    DEBUG("TcpConn连接测试: 服务器连接建立=%s，客户端连接建立=%s",
          serverConnected ? "成功" : "失败", clientConnected ? "成功" : "失败");
    DEBUG("TcpConn数据传输测试: 服务器接收=%s，客户端接收=%s",
          serverReceived ? "成功" : "失败", clientReceived ? "成功" : "失败");
    DEBUG("=== TcpConn基本功能测试结束 ===\n");
}

/**
 * @brief 测试TcpConn的消息编解码功能
 */
void test_TcpConn_codec() {
    DEBUG("=== 开始TcpConn编解码功能测试 ===");
    EventBase base;

    // 启动带编解码器的服务器
    TcpServer::Ptr server = TcpServer::startServer(&base, "127.0.0.1", 8889);
    assert(server);
    server->onConnMsg(make_unique<LineCodec>(), [](const TcpConnPtr& conn, const Slice& msg) {
        string data = msg.toString();
        DEBUG("服务器端：解码收到消息: %s", data.c_str());
        if (data == "codec test") {
            conn->sendMsg("codec response");
        }
    });

    // 启动带编解码器的客户端
    bool clientReceived = false;
    TcpConnPtr client = TcpConn::createConnection(&base, "127.0.0.1", 8889);
    client->onState([&](const TcpConnPtr& conn) {
        if (conn->getState() == TcpConn::State::CONNECTED) {
            conn->onMsg(make_unique<LineCodec>(), [&](const TcpConnPtr& c, const Slice& msg) {
                string data = msg.toString();
                if (data == "codec response") {
                    clientReceived = true;
                    DEBUG("客户端：解码收到消息: %s", data.c_str());
                    base.exit();
                }
            });
            conn->sendMsg("codec test");
            DEBUG("客户端：发送编码消息");
        }
    });

    // 运行事件循环
    thread t([&]() { base.loop(); });
    this_thread::sleep_for(chrono::seconds(3));
    base.exit();
    t.join();

    DEBUG("TcpConn编解码测试: %s", clientReceived ? "成功" : "失败");
    DEBUG("=== TcpConn编解码功能测试结束 ===\n");
}

/**
 * @brief 测试TcpConn的重连功能
 */
void test_TcpConn_reconnect() {
    DEBUG("=== 开始TcpConn重连功能测试 ===");
    EventBase base;
    int connectCount = 0;

    // 延迟启动服务器（测试客户端重连）
    thread serverThread;
    bool serverStarted = false;
    TcpServer::Ptr server;

    // 创建客户端并设置重连
    TcpConnPtr client = TcpConn::createConnection(&base, "127.0.0.1", 8890);
    client->setReconnectInterval(100); // 100ms重连一次
    client->onState([&](const TcpConnPtr& conn) {
        if (conn->getState() == TcpConn::State::CONNECTED) {
            connectCount++;
            DEBUG("客户端：第%d次连接成功", connectCount);
            if (connectCount >= 2) { // 成功重连一次即可
                base.exit();
            }
        }
    });

    // 1秒后启动服务器
    serverThread = thread([&]() {
        this_thread::sleep_for(chrono::seconds(1));
        EventBase serverBase;
        server = TcpServer::startServer(&serverBase, "127.0.0.1", 8890);
        assert(server);
        serverStarted = true;
        server->onConnState([&](const TcpConnPtr& conn) {
            if (conn->getState() == TcpConn::State::CONNECTED) {
                DEBUG("服务器端：客户端连接成功");
            }
        });
        serverBase.loop();
    });

    // 运行客户端事件循环
    thread clientThread([&]() { base.loop(); });
    this_thread::sleep_for(chrono::seconds(3));
    base.exit();
    if (server) {
        server->getBase()->exit();
    }
    clientThread.join();
    serverThread.join();

    DEBUG("TcpConn重连测试: 连接成功次数=%d（预期>=2），%s",
          connectCount, connectCount >= 2 ? "通过" : "失败");
    DEBUG("=== TcpConn重连功能测试结束 ===\n");
}

/**
 * @brief 测试TcpConn的多线程并发读写
 */
void test_TcpConn_threadSafe() {
    DEBUG("=== 开始TcpConn线程安全测试 ===");
    EventBase base;
    const int MSG_COUNT = 100;
    atomic<int> serverRecvCount(0);
    atomic<int> clientRecvCount(0);

    // 启动服务器
    TcpServer::Ptr server = TcpServer::startServer(&base, "127.0.0.1", 8891);
    assert(server);
    server->onConnRead([&](const TcpConnPtr& conn) {
        Buffer& buf = conn->getInputBuffer();
        if (buf.size() > 0) {
            serverRecvCount += buf.size();
            conn->send(buf); // 回显数据
        }
    });

    // 客户端连接
    TcpConnPtr client;
    bool clientReady = false;
    client = TcpConn::createConnection(&base, "127.0.0.1", 8891);
    client->onState([&](const TcpConnPtr& conn) {
        if (conn->getState() == TcpConn::State::CONNECTED) {
            clientReady = true;
            DEBUG("客户端：连接建立，开始多线程测试");
        }
    });
    client->onReadable([&](const TcpConnPtr& conn) {
        Buffer& buf = conn->getInputBuffer();
        if (buf.size() > 0) {
            clientRecvCount += buf.size();
            if (clientRecvCount >= MSG_COUNT * 100) { // 达到预期数据量
                base.exit();
            }
        }
    });

    // 启动多线程发送数据
    thread clientThread([&]() { base.loop(); });
    // 等待客户端连接就绪
    while (!clientReady) {
        this_thread::sleep_for(chrono::milliseconds(10));
    }

    // 启动5个线程并发发送数据
    vector<thread> sendThreads;
    for (int i = 0; i < 5; ++i) {
        sendThreads.emplace_back([&, i]() {
            for (int j = 0; j < MSG_COUNT; ++j) {
                string data = "thread_" + to_string(i) + "_msg_" + to_string(j) + "\n";
                client->send(data);
                g_connTestCount++;
                this_thread::sleep_for(chrono::microseconds(10));
            }
        });
    }

    // 等待所有发送线程完成
    for (auto& t : sendThreads) {
        t.join();
    }

    // 等待接收完成
    clientThread.join();

    DEBUG("TcpConn线程安全测试: 发送次数=%d，服务器接收字节数=%d，客户端接收字节数=%d",
          g_connTestCount.load(), serverRecvCount.load(), clientRecvCount.load());
    DEBUG("=== TcpConn线程安全测试结束 ===\n");
}

/**
 * @brief 测试TcpConn的关闭和清理功能
 */
void test_TcpConn_close() {
    DEBUG("=== 开始TcpConn关闭清理测试 ===");
    EventBase base;
    bool connClosed = false;

    // 启动服务器
    TcpServer::Ptr server = TcpServer::startServer(&base, "127.0.0.1", 8892);
    assert(server);
    server->onConnState([&](const TcpConnPtr& conn) {
        if (conn->getState() == TcpConn::State::CLOSED) {
            DEBUG("服务器端：连接关闭");
            base.exit();
        }
    });

    // 客户端连接并主动关闭
    TcpConnPtr client = TcpConn::createConnection(&base, "127.0.0.1", 8892);
    client->onState([&](const TcpConnPtr& conn) {
        if (conn->getState() == TcpConn::State::CONNECTED) {
            DEBUG("客户端：连接建立，准备关闭");
            conn->close(); // 关闭连接
        } else if (conn->getState() == TcpConn::State::CLOSED) {
            connClosed = true;
            DEBUG("客户端：连接已关闭");
        }
    });

    // 运行事件循环
    thread t([&]() { base.loop(); });
    t.join();

    DEBUG("TcpConn关闭测试: %s", connClosed ? "成功" : "失败");
    DEBUG("=== TcpConn关闭清理测试结束 ===\n");
}

/**
 * @brief 测试TcpConn的空闲回调功能
 */
void test_TcpConn_idle() {
    DEBUG("=== 开始TcpConn空闲回调测试 ===");
    EventBase base;
    bool idleCallbackCalled = false;

    // 启动服务器
    TcpServer::Ptr server = TcpServer::startServer(&base, "127.0.0.1", 8893);
    assert(server);

    // 客户端连接并设置空闲回调
    TcpConnPtr client = TcpConn::createConnection(&base, "127.0.0.1", 8893);
    client->onState([&](const TcpConnPtr& conn) {
        if (conn->getState() == TcpConn::State::CONNECTED) {
            DEBUG("客户端：连接建立，设置空闲回调");
            // 设置1秒空闲回调
            conn->addIdleCB(1000, [&](const TcpConnPtr& c) {
                idleCallbackCalled = true;
                DEBUG("客户端：空闲回调被触发");
                base.exit();
            });
        }
    });

    // 运行事件循环（等待空闲回调）
    thread t([&]() { base.loop(); });
    this_thread::sleep_for(chrono::seconds(2)); // 等待超过空闲时间
    base.exit();
    t.join();

    DEBUG("TcpConn空闲回调测试: %s", idleCallbackCalled ? "成功" : "失败");
    DEBUG("=== TcpConn空闲回调测试结束 ===\n");
}

// 测试入口函数
void run_all_conn_tests() {
    // 初始化日志
    initConnTestLogger();

    // 执行所有测试
    test_TcpConn_basic();
    test_TcpConn_codec();
    test_TcpConn_reconnect();
    test_TcpConn_threadSafe();
    test_TcpConn_close();
    test_TcpConn_idle();

    // 测试总结
    INFO("=== conn_test 所有测试执行完成 ===");
}

// 主函数：启动测试
int main() {
    run_all_conn_tests();
    return 0;
}