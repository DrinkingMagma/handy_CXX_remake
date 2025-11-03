// stat_svr_test.cpp
#include "stat-svr.h"
#include "event_base.h"
#include "http.h"
#include "logger.h"
#include "file.h"
#include <cstdio>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <future>
#include <algorithm>

namespace handy {
namespace statSvrTest {

// 测试用全局变量
std::atomic<int> g_callbackInvokeCount(0);
std::string g_testFileContent = "test file content";
std::string g_testFileName = "stat_svr_test_temp.txt";

// -------------------------- 测试辅助函数 --------------------------
/**
 * @brief 初始化Logger（输出到stat_svr_test.log）
 */
void initTestLogger() {
    Logger::getInstance().setLogFileName("stat_svr_test.log");
    Logger::getInstance().setLogLevel(Logger::LogLevel::LDEBUG);
    INFO("=== stat_svr_test 测试开始 ===");
}

/**
 * @brief 测试结束时关闭Logger
 */
void destroyTestLogger() {
    // 清理测试文件
    if (!g_testFileName.empty()) {
        remove(g_testFileName.c_str());
    }
    INFO("=== stat_svr_test 测试结束 ===");
}

/**
 * @brief 创建测试临时文件
 */
bool createTestFile() {
    FILE* f = fopen(g_testFileName.c_str(), "w");
    if (!f) return false;
    fwrite(g_testFileContent.c_str(), 1, g_testFileContent.size(), f);
    fclose(f);
    return true;
}

/**
 * @brief 模拟HTTP请求并获取响应
 */
std::string simulateRequest(StatServer& server, const std::string& uri, const std::string& query = "") {
    EventBase base;
    server.bind("127.0.0.1", 8080);
    
    // 启动服务器线程
    std::thread serverThread([&base]() {
        base.loop();
    });
    
    // 等待服务器启动
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 客户端发送请求
    std::promise<std::string> respPromise;
    TcpConnPtr client(&base);
    client.onConnect([&](const TcpConnPtr& conn) {
        HttpRequest req;
        req.m_method = "GET";
        req.m_uri = uri;
        if (!query.empty()) {
            req.m_queryUri = uri + "?stat=" + query;
        } else {
            req.m_queryUri = uri;
        }
        
        Buffer buf;
        req.encode(buf);
        conn->send(buf);
    });
    
    client.onMessage([&](const TcpConnPtr& conn, Buffer& buf) {
        HttpResponse resp;
        auto res = resp.tryDecode(Slice(buf.data()));
        if (res == HttpMsg::Result::Complete) {
            respPromise.set_value(resp.getBody().toString());
            conn->close();
            base.exit();
        }
    });
    
    client.connect("127.0.0.1", 8080);
    
    // 等待响应
    std::string response = respPromise.get_future().get();
    serverThread.join();
    return response;
}

// -------------------------- StatServer 测试函数 --------------------------
/**
 * @brief 测试 StatServer 基本构造与绑定功能
 */
void testStatServerBasic() {
    DEBUG("=== 开始测试 StatServer 基本构造与绑定 ===");

    EventBase base;
    StatServer server(&base);
    
    // 测试绑定功能
    int bindResult = server.bind("127.0.0.1", 8080);
    bool test1 = (bindResult == 0);
    DEBUG("测试1（绑定端口）：%s", test1 ? "通过" : "失败");

    // 测试重复绑定同一端口（预期失败）
    StatServer server2(&base);
    int bindResult2 = server2.bind("127.0.0.1", 8080);
    bool test2 = (bindResult2 != 0);
    DEBUG("测试2（重复绑定）：%s", test2 ? "通过" : "失败");

    DEBUG("=== StatServer 基本构造与绑定测试结束 ===\n");
}

/**
 * @brief 测试状态回调注册与处理
 */
void testStatServerStateCallbacks() {
    DEBUG("=== 开始测试 StatServer 状态回调 ===");

    EventBase base;
    StatServer server(&base);
    
    // 重置计数器
    g_callbackInvokeCount = 0;
    
    // 注册状态回调
    server.onState("test_state1", "测试状态1", []() {
        g_callbackInvokeCount++;
        return "state1_value";
    });
    
    server.onState("test_state2", "测试状态2", []() -> int64_t {
        g_callbackInvokeCount++;
        return 12345;
    });
    
    // 测试根路径响应
    std::string rootResp = simulateRequest(server, "/");
    bool test1 = (rootResp.find("test_state1") != std::string::npos &&
                 rootResp.find("test_state2") != std::string::npos &&
                 rootResp.find("测试状态1") != std::string::npos &&
                 rootResp.find("测试状态2") != std::string::npos);
    DEBUG("测试1（根路径状态展示）：%s", test1 ? "通过" : "失败");

    // 测试直接访问状态1
    std::string state1Resp = simulateRequest(server, "/test_state1");
    bool test2 = (state1Resp == "state1_value");
    DEBUG("测试2（状态1直接访问）：%s", test2 ? "通过" : "失败");

    // 测试通过查询参数访问状态2
    std::string state2Resp = simulateRequest(server, "/", "test_state2");
    bool test3 = (state2Resp.find("12345") != std::string::npos);
    DEBUG("测试3（状态2查询访问）：%s", test3 ? "通过" : "失败");

    // 测试回调调用次数
    bool test4 = (g_callbackInvokeCount == 3);  // 根路径1次 + 直接访问1次 + 查询访问1次
    DEBUG("测试4（回调调用次数）：%d/%d, %s", 
          g_callbackInvokeCount.load(), 3, test4 ? "通过" : "失败");

    DEBUG("=== StatServer 状态回调测试结束 ===\n");
}

/**
 * @brief 测试页面回调注册与处理
 */
void testStatServerPageCallbacks() {
    DEBUG("=== 开始测试 StatServer 页面回调 ===");

    EventBase base;
    StatServer server(&base);
    
    // 注册页面回调
    server.onPage("test_page", "测试页面", []() {
        return "<html><body>Test Page</body></html>";
    });
    
    // 创建测试文件
    bool fileCreated = createTestFile();
    if (fileCreated) {
        server.onPageFile("test_page_file", "测试页面文件", g_testFileName);
    }
    
    // 测试根路径页面展示
    std::string rootResp = simulateRequest(server, "/");
    bool test1 = (rootResp.find("test_page") != std::string::npos &&
                 rootResp.find("测试页面") != std::string::npos);
    DEBUG("测试1（根路径页面展示）：%s", test1 ? "通过" : "失败");

    // 测试直接访问页面
    std::string pageResp = simulateRequest(server, "/test_page");
    bool test2 = (pageResp == "<html><body>Test Page</body></html>");
    DEBUG("测试2（页面直接访问）：%s", test2 ? "通过" : "失败");

    // 测试页面文件访问
    bool test3 = false;
    if (fileCreated) {
        std::string fileResp = simulateRequest(server, "/test_page_file");
        test3 = (fileResp == g_testFileContent);
    }
    DEBUG("测试3（页面文件访问）：%s", fileCreated ? (test3 ? "通过" : "失败") : "跳过（文件创建失败）");

    DEBUG("=== StatServer 页面回调测试结束 ===\n");
}

/**
 * @brief 测试命令回调注册与处理
 */
void testStatServerCmdCallbacks() {
    DEBUG("=== 开始测试 StatServer 命令回调 ===");

    EventBase base;
    StatServer server(&base);
    g_callbackInvokeCount = 0;
    
    // 注册命令回调
    server.onCmd("test_cmd1", "测试命令1", []() {
        g_callbackInvokeCount++;
        return "cmd1_executed";
    });
    
    server.onCmd("test_cmd2", "测试命令2", []() -> int64_t {
        g_callbackInvokeCount++;
        return 98765;
    });
    
    // 测试根路径命令展示
    std::string rootResp = simulateRequest(server, "/");
    bool test1 = (rootResp.find("test_cmd1") != std::string::npos &&
                 rootResp.find("test_cmd2") != std::string::npos &&
                 rootResp.find("测试命令1") != std::string::npos &&
                 rootResp.find("测试命令2") != std::string::npos);
    DEBUG("测试1（根路径命令展示）：%s", test1 ? "通过" : "失败");

    // 测试直接访问命令1
    std::string cmd1Resp = simulateRequest(server, "/test_cmd1");
    bool test2 = (cmd1Resp == "cmd1_executed");
    DEBUG("测试2（命令1直接访问）：%s", test2 ? "通过" : "失败");

    // 测试通过查询参数访问命令2
    std::string cmd2Resp = simulateRequest(server, "/", "test_cmd2");
    bool test3 = (cmd2Resp.find("98765") != std::string::npos);
    DEBUG("测试3（命令2查询访问）：%s", test3 ? "通过" : "失败");

    // 测试回调调用次数
    bool test4 = (g_callbackInvokeCount == 3);  // 根路径1次 + 直接访问1次 + 查询访问1次
    DEBUG("测试4（回调调用次数）：%d/%d, %s", 
          g_callbackInvokeCount.load(), 3, test4 ? "通过" : "失败");

    DEBUG("=== StatServer 命令回调测试结束 ===\n");
}

/**
 * @brief 测试404错误处理
 */
void testStatServer404Handling() {
    DEBUG("=== 开始测试 StatServer 404处理 ===");

    EventBase base;
    StatServer server(&base);
    
    // 访问不存在的路径
    std::string resp = simulateRequest(server, "/non_existent_path");
    
    // 检查404响应
    bool test1 = (resp.find("404 Not Found") != std::string::npos);
    DEBUG("测试1（不存在路径的404响应）：%s", test1 ? "通过" : "失败");

    // 通过查询参数访问不存在的资源
    std::string queryResp = simulateRequest(server, "/", "non_existent_key");
    bool test2 = (queryResp.find("404 Not Found") != std::string::npos);
    DEBUG("测试2（不存在查询的404响应）：%s", test2 ? "通过" : "失败");

    DEBUG("=== StatServer 404处理测试结束 ===\n");
}

/**
 * @brief 测试多线程安全
 */
void testStatServerThreadSafe() {
    DEBUG("=== 开始测试 StatServer 线程安全 ===");

    const int THREAD_NUM = 3;
    const int REQ_PER_THREAD = 10;
    EventBase base;
    StatServer server(&base);
    
    // 注册测试回调
    server.onState("thread_safe_state", "线程安全测试状态", []() -> int64_t {
        return g_callbackInvokeCount.fetch_add(1, std::memory_order_relaxed);
    });
    
    // 启动服务器线程
    std::thread serverThread([&base]() {
        base.loop();
    });
    
    // 等待服务器启动
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 多线程发送请求
    std::vector<std::thread> clientThreads;
    for (int i = 0; i < THREAD_NUM; ++i) {
        clientThreads.emplace_back([i]() {
            for (int j = 0; j < REQ_PER_THREAD; ++j) {
                TcpClient client;
                EventBase clientBase;
                std::promise<bool> reqPromise;
                
                client.onConnect([&](const TcpConnPtr& conn) {
                    HttpRequest req;
                    req.m_method = "GET";
                    req.m_queryUri = "/thread_safe_state";
                    Buffer buf;
                    req.encode(buf);
                    conn->send(buf);
                });
                
                client.onMessage([&](const TcpConnPtr& conn, Buffer& buf) {
                    HttpResponse resp;
                    if (resp.tryDecode(Slice(buf.data())) == HttpMsg::Result::Complete) {
                        reqPromise.set_value(true);
                        conn->close();
                        clientBase.exit();
                    }
                });
                
                client.connect("127.0.0.1", 8080);
                clientBase.loop();
                reqPromise.get_future().get();
            }
        });
    }
    
    // 等待所有客户端线程完成
    for (auto& t : clientThreads) {
        t.join();
    }
    
    // 停止服务器
    base.exit();
    serverThread.join();
    
    // 验证总调用次数
    int64_t total = g_callbackInvokeCount.load();
    int64_t expected = THREAD_NUM * REQ_PER_THREAD;
    bool test1 = (total == expected);
    DEBUG("测试1（多线程回调计数）：%lld/%lld, %s", 
          total, expected, test1 ? "通过" : "失败");

    DEBUG("=== StatServer 线程安全测试结束 ===\n");
}

/**
 * @brief 测试内容类型设置
 */
void testStatServerContentType() {
    DEBUG("=== 开始测试 StatServer 内容类型 ===");

    EventBase base;
    StatServer server(&base);
    
    // 注册不同类型的回调
    server.onPage("html_page", "HTML页面", []() {
        return "<html></html>";
    });
    
    // 创建不同类型的测试文件
    bool htmlFileCreated = createTestFile();
    std::string htmlFileName = "test.html";
    if (htmlFileCreated) {
        FILE* f = fopen(htmlFileName.c_str(), "w");
        if (f) {
            fwrite("<html></html>", 1, 13, f);
            fclose(f);
            server.onPageFile("html_file", "HTML文件", htmlFileName);
        } else {
            htmlFileCreated = false;
        }
    }
    
    bool jsFileCreated = false;
    std::string jsFileName = "test.js";
    FILE* f = fopen(jsFileName.c_str(), "w");
    if (f) {
        fwrite("function test() {}", 1, 18, f);
        fclose(f);
        server.onPageFile("js_file", "JS文件", jsFileName);
        jsFileCreated = true;
    }
    
    // 测试根路径的HTML类型
    std::string rootResp = simulateRequest(server, "/");
    bool test1 = (rootResp.find("text/html") != std::string::npos);
    DEBUG("测试1（根路径HTML类型）：%s", test1 ? "通过" : "失败");

    // 测试页面的默认类型
    std::string pageResp = simulateRequest(server, "/html_page");
    bool test2 = (pageResp.find("text/plain") != std::string::npos);
    DEBUG("测试2（页面默认类型）：%s", test2 ? "通过" : "失败");

    // 测试HTML文件类型
    bool test3 = false;
    if (htmlFileCreated) {
        std::string htmlResp = simulateRequest(server, "/html_file");
        test3 = (htmlResp.find("text/html") != std::string::npos);
    }
    DEBUG("测试3（HTML文件类型）：%s", htmlFileCreated ? (test3 ? "通过" : "失败") : "跳过（文件创建失败）");

    // 测试JS文件类型
    bool test4 = false;
    if (jsFileCreated) {
        std::string jsResp = simulateRequest(server, "/js_file");
        test4 = (jsResp.find("application/javascript") != std::string::npos);
    }
    DEBUG("测试4（JS文件类型）：%s", jsFileCreated ? (test4 ? "通过" : "失败") : "跳过（文件创建失败）");

    // 清理测试文件
    remove(htmlFileName.c_str());
    remove(jsFileName.c_str());

    DEBUG("=== StatServer 内容类型测试结束 ===\n");
}

// -------------------------- 测试入口函数 --------------------------
void runAllTests() {
    // 1. 初始化日志
    initTestLogger();

    // 2. 依次执行所有测试
    testStatServerBasic();
    testStatServerStateCallbacks();
    testStatServerPageCallbacks();
    testStatServerCmdCallbacks();
    testStatServer404Handling();
    testStatServerContentType();
    testStatServerThreadSafe();

    // 3. 清理日志
    destroyTestLogger();
}

}  // namespace statSvrTest
}  // namespace handy

// 主函数：启动测试
int main() {
    handy::statSvrTest::runAllTests();
    return 0;
}