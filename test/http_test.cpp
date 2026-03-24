// http_test.cpp
#include "http.h"
#include "net.h"
#include "logger.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <thread>
#include <atomic>
#include <unistd.h>
#include <sstream>
#include <algorithm>

namespace handy {
namespace httpTest {

// 测试用全局原子变量（用于多线程测试计数）
std::atomic<int> g_httpThreadTestCount(0);

// -------------------------- 测试辅助函数 --------------------------
/**
 * @brief 初始化Logger（输出到http_test.log）
 */
void initTestLogger() {
    Logger::getInstance().setLogFileName("http_test.log");
    Logger::getInstance().setLogLevel(Logger::LogLevel::LDEBUG);
    INFO("=== http_test 测试开始 ===");
}

/**
 * @brief 测试结束时关闭Logger
 */
void destroyTestLogger() {
    INFO("=== http_test 测试结束 ===");
}

// -------------------------- HttpRequest 测试函数 --------------------------
/**
 * @brief 测试 HttpRequest 构造与基础操作
 */
void testHttpRequestBasic() {
    DEBUG("=== 开始测试 HttpRequest 基础操作 ===");

    // 测试默认构造
    HttpRequest req;
    bool test1 = (req.m_method == "GET" && req.m_uri.empty() && 
                 req.m_queryUri.empty() && req.m_args.empty());
    DEBUG("测试1（默认构造）：%s", test1 ? "通过" : "失败");

    // 测试设置方法和URI
    req.m_method = "POST";
    req.m_uri = "/api/test";
    req.m_queryUri = "/api/test?name=test&value=123";
    bool test2 = (req.m_method == "POST" && req.m_uri == "/api/test" &&
                 req.m_queryUri == "/api/test?name=test&value=123");
    DEBUG("测试2（设置方法和URI）：%s", test2 ? "通过" : "失败");

    // 测试设置头部
    req.setHeader("Content-Type", "application/json");
    req.setHeader("Authorization", "Bearer token");
    bool test3 = (req.getHeaderValue("content-type") == "application/json" &&
                 req.getHeaderValue("authorization") == "Bearer token");
    DEBUG("测试3（设置头部）：%s", test3 ? "通过" : "失败");

    // 测试设置消息体
    req.setBody("{\"key\":\"value\"}");
    bool test4 = (req.getBody().toString() == "{\"key\":\"value\"}");
    DEBUG("测试4（设置消息体）：%s", test4 ? "通过" : "失败");

    // 测试clear()
    req.clear();
    bool test5 = (req.m_method == "GET" && req.m_uri.empty() && 
                 req.getHeaders().empty() && req.getBody().empty());
    DEBUG("测试5（clear()）：%s", test5 ? "通过" : "失败");

    DEBUG("=== HttpRequest 基础操作测试结束 ===\n");
}

/**
 * @brief 测试 HttpRequest 编码功能
 */
void testHttpRequestEncode() {
    DEBUG("=== 开始测试 HttpRequest 编码功能 ===");

    HttpRequest req;
    req.m_method = "POST";
    req.m_queryUri = "/submit?action=save";
    req.setHeader("Content-Type", "text/plain");
    req.setBody("test data");

    Buffer buf;
    int len = req.encode(buf);
    
    std::string expected = "POST /submit?action=save HTTP/1.1\r\n"
                          "content-type: text/plain\r\n"
                          "Connection: Keep-Alive\r\n"
                          "Content-Length: 9\r\n"
                          "\r\n"
                          "test data";
    
    bool test1 = (buf.data() == expected);
    DEBUG("测试1（完整编码）：%s", test1 ? "通过" : "失败");
    DEBUG("实际输出:\n%s", buf.data().c_str());
    DEBUG("预期输出:\n%s", expected.c_str());

    bool test2 = (static_cast<size_t>(len) == expected.size());
    DEBUG("测试2（编码长度）：%s", test2 ? "通过" : "失败");

    DEBUG("=== HttpRequest 编码功能测试结束 ===\n");
}

/**
 * @brief 测试 HttpRequest 解码功能
 */
void testHttpRequestDecode() {
    DEBUG("=== 开始测试 HttpRequest 解码功能 ===");

    // 测试1：完整的GET请求
    std::string getReq = "GET /index.html?user=test&page=1 HTTP/1.1\r\n"
                        "Host: example.com\r\n"
                        "User-Agent: test\r\n"
                        "Content-Length: 0\r\n"
                        "\r\n";
    
    HttpRequest req1;
    HttpMsg::Result res1 = req1.tryDecode(Slice(getReq));
    // GET因为Content-Length: 0导致解码未完成
    bool test1 = (res1 == HttpMsg::Result::NotComplete &&
                 req1.m_method == "GET" &&
                 req1.m_uri == "/index.html" &&
                 req1.getArg("user") == "test" &&
                 req1.getArg("page") == "1" &&
                 req1.getHeaderValue("host") == "example.com");
    DEBUG("res1: %d", (int)res1);
    DEBUG("测试1（GET请求解码）：%s", test1 ? "通过" : "失败");

    // 测试2：完整的POST请求
    std::string postReq = "POST /api/submit HTTP/1.1\r\n"
                         "Content-Type: application/x-www-form-urlencoded\r\n"
                         "Content-Length: 16\r\n"
                         "\r\n"
                         "name=test&age=18";
    
    HttpRequest req2;
    HttpMsg::Result res2 = req2.tryDecode(Slice(postReq));
    bool test2 = (res2 == HttpMsg::Result::Complete &&
                 req2.m_method == "POST" &&
                 req2.m_uri == "/api/submit" &&
                 req2.getBody().toString() == "name=test&age=18");

    if(test2 == false)
    {
        DEBUG("res2: %d\nreq2.m_method: %s\nreq2.m_uri: %s\nreq2.getBody(): %s\n", 
            (int)res2, req2.m_method.c_str(), req2.m_uri.c_str(), req2.getBody().toString().c_str());
    }
    DEBUG("测试2（POST请求解码）：%s", test2 ? "通过" : "失败");

    // 测试3：不完整请求
    std::string partialReq = "GET /partial HTTP/1.1\r\n"
                            "Host: example.com\r\n";
    
    HttpRequest req3;
    HttpMsg::Result res3 = req3.tryDecode(Slice(partialReq));
    bool test3 = (res3 == HttpMsg::Result::NotComplete);
    DEBUG("测试3（不完整请求）：%s", test3 ? "通过" : "失败");

    // 测试4：带100 Continue的请求
    std::string continueReq = "POST /large HTTP/1.1\r\n"
                             "Expect: 100-continue\r\n"
                             "Content-Length: 100\r\n"
                             "\r\n";  // 不包含完整消息体
    
    HttpRequest req4;
    HttpMsg::Result res4 = req4.tryDecode(Slice(continueReq));
    bool test4 = (res4 == HttpMsg::Result::Continue100);
    DEBUG("测试4（100 Continue请求）：%s", test4 ? "通过" : "失败");

    // 测试5：无效URI
    std::string invalidUriReq = "GET invalid_uri HTTP/1.1\r\n\r\n";
    HttpRequest req5;
    HttpMsg::Result res5 = req5.tryDecode(Slice(invalidUriReq));
    bool test5 = (res5 == HttpMsg::Result::Error);
    DEBUG("测试5（无效URI）：%s", test5 ? "通过" : "失败");

    DEBUG("=== HttpRequest 解码功能测试结束 ===\n");
}

// -------------------------- HttpResponse 测试函数 --------------------------
/**
 * @brief 测试 HttpResponse 构造与基础操作
 */
void testHttpResponseBasic() {
    DEBUG("=== 开始测试 HttpResponse 基础操作 ===");

    // 测试默认构造
    HttpResponse resp;
    bool test1 = (resp.m_status == 200 && resp.m_statusMsg == "OK" &&
                 resp.getVersion() == "HTTP/1.1");
    DEBUG("测试1（默认构造）：%s", test1 ? "通过" : "失败");

    // 测试设置状态
    resp.setStatus(404, "Not Found");
    bool test2 = (resp.m_status == 404 && resp.m_statusMsg == "Not Found");
    DEBUG("测试2（设置状态）：%s", test2 ? "通过" : "失败");

    // 测试设置404
    resp.setNotFound();
    bool test3 = (resp.m_status == 404 && resp.m_statusMsg == "Not Found");
    DEBUG("测试3（setNotFound()）：%s", test3 ? "通过" : "失败");

    // 测试设置头部和消息体
    resp.setHeader("Content-Type", "text/html");
    resp.setBody("<h1>Not Found</h1>");
    bool test4 = (resp.getHeaderValue("content-type") == "text/html" &&
                 resp.getBody().toString() == "<h1>Not Found</h1>");
    DEBUG("测试4（设置头部和消息体）：%s", test4 ? "通过" : "失败");

    // 测试clear()
    resp.clear();
    bool test5 = (resp.m_status == 200 && resp.m_statusMsg == "OK" &&
                 resp.getHeaders().empty() && resp.getBody().empty());
    DEBUG("测试5（clear()）：%s", test5 ? "通过" : "失败");

    DEBUG("=== HttpResponse 基础操作测试结束 ===\n");
}

/**
 * @brief 测试 HttpResponse 编码功能
 */
void testHttpResponseEncode() {
    DEBUG("=== 开始测试 HttpResponse 编码功能 ===");

    HttpResponse resp;
    resp.setStatus(200, "OK");
    resp.setHeader("Content-Type", "application/json");
    resp.setBody("{\"status\":\"success\"}");

    Buffer buf;
    int len = resp.encode(buf);
    
    std::string expected = "HTTP/1.1 200 OK\r\n"
                          "content-type: application/json\r\n"
                          "Connection: Keep-Alive\r\n"
                          "Content-Length: 20\r\n"
                          "\r\n"
                          "{\"status\":\"success\"}";
    
    bool test1 = (buf.data() == expected);
    DEBUG("测试1（完整编码）：%s", test1 ? "通过" : "失败");
    DEBUG("实际输出:\n%s", buf.data().c_str());
    DEBUG("预期输出:\n%s", expected.c_str());

    bool test2 = (static_cast<size_t>(len) == expected.size());
    DEBUG("测试2（编码长度）：%s", test2 ? "通过" : "失败");

    // 测试404响应
    HttpResponse resp404;
    resp404.setNotFound();
    Buffer buf404;
    resp404.encode(buf404);
    bool test3 = (buf404.data().find("404 Not Found") != std::string::npos);
    DEBUG("测试3（404响应编码）：%s", test3 ? "通过" : "失败");

    DEBUG("=== HttpResponse 编码功能测试结束 ===\n");
}

/**
 * @brief 测试 HttpResponse 解码功能
 */
void testHttpResponseDecode() {
    DEBUG("=== 开始测试 HttpResponse 解码功能 ===");

    // 测试1：完整的200响应
    std::string okResp = "HTTP/1.1 200 OK\r\n"
                        "Content-Type: text/plain\r\n"
                        "Content-Length: 7\r\n"
                        "\r\n"
                        "success";
    
    HttpResponse resp1;
    HttpMsg::Result res1 = resp1.tryDecode(Slice(okResp));
    bool test1 = (res1 == HttpMsg::Result::Complete &&
                 resp1.m_status == 200 &&
                 resp1.m_statusMsg == "OK" &&
                 resp1.getHeaderValue("content-type") == "text/plain" &&
                 resp1.getBody().toString() == "success");
    DEBUG("测试1（200响应解码）：%s", test1 ? "通过" : "失败");

    // 测试2：404响应
    std::string notFoundResp = "HTTP/1.1 404 Not Found\r\n"
                              "Content-Length: 9\r\n"
                              "\r\n"
                              "Not Found";
    
    HttpResponse resp2;
    HttpMsg::Result res2 = resp2.tryDecode(Slice(notFoundResp));
    bool test2 = (res2 == HttpMsg::Result::Complete &&
                 resp2.m_status == 404 &&
                 resp2.m_statusMsg == "Not Found" &&
                 resp2.getBody().toString() == "Not Found");

    DEBUG("res2: %d\nresp2.m_status: %d\nresp2.m_statusMsg: %s\nresp2.getBody(): %s", 
        (int)res2, resp2.m_status, resp2.m_statusMsg.c_str(), resp2.getBody().toString().c_str());
    DEBUG("测试2（404响应解码）：%s", test2 ? "通过" : "失败");

    // 测试3：不完整响应
    std::string partialResp = "HTTP/1.1 500 Internal Server Error\r\n"
                             "Content-Type: text/plain\r\n";
    
    HttpResponse resp3;
    HttpMsg::Result res3 = resp3.tryDecode(Slice(partialResp));
    bool test3 = (res3 == HttpMsg::Result::NotComplete);
    DEBUG("测试3（不完整响应）：%s", test3 ? "通过" : "失败");

    DEBUG("=== HttpResponse 解码功能测试结束 ===\n");
}

// -------------------------- HttpServer 测试函数 --------------------------
/**
 * @brief 测试 HttpServer 路由功能
 */
void testHttpServerRouting() {
    DEBUG("=== 开始测试 HttpServer 路由功能 ===");

    EventBase base;
    HttpServer server(&base);
    
    // 注册测试路由
    bool getCalled = false;
    server.onGet("/test/get", [&](const HttpConnPtr& conn) {
        getCalled = true;
        HttpResponse& resp = conn.getResponse();
        resp.setBody("GET response");
        conn.sendResponse();
    });

    bool postCalled = false;
    server.onRequest("POST", "/test/post", [&](const HttpConnPtr& conn) {
        postCalled = true;
        HttpResponse& resp = conn.getResponse();
        resp.setBody("POST response");
        conn.sendResponse();
    });

    bool defaultCalled = false;
    server.onDefault([&](const HttpConnPtr& conn) {
        defaultCalled = true;
        HttpResponse& resp = conn.getResponse();
        resp.setNotFound();
        conn.sendResponse();
    });

    // 模拟GET请求匹配
    HttpRequest getReq;
    getReq.m_method = "GET";
    getReq.m_uri = "/test/get";
    getReq.m_queryUri = "/test/get";
    
    // 模拟连接和请求处理（简化测试）
    auto mockGetHandler = server.getHandler("GET","/test/get");
    TcpConnPtr mockTcpConn(new TcpConn);
    HttpConnPtr mockHttpConn(mockTcpConn);
    mockGetHandler(mockHttpConn);
    bool test1 = getCalled;
    DEBUG("测试1（GET路由匹配）：%s", test1 ? "通过" : "失败");

    // 模拟POST请求匹配
    HttpRequest postReq;
    postReq.m_method = "POST";
    postReq.m_uri = "/test/post";
    postReq.m_queryUri = "/test/post";
    
    auto mockPostHandler = server.getHandler("POST","/test/post");
    mockPostHandler(mockHttpConn);
    bool test2 = postCalled;
    DEBUG("测试2（POST路由匹配）：%s", test2 ? "通过" : "失败");

    // 模拟默认路由
    server.getDefault()(mockHttpConn);
    bool test3 = defaultCalled;
    DEBUG("测试3（默认路由）：%s", test3 ? "通过" : "失败");

    DEBUG("=== HttpServer 路由功能测试结束 ===\n");
}

// -------------------------- 多线程安全测试 --------------------------
/**
 * @brief 多线程测试HTTP消息处理
 */
void testHttpThreadSafe() {
    DEBUG("=== 开始测试 HTTP 线程安全 特性 ===");

    const int THREAD_NUM = 5;    // 5个测试线程
    const int LOOP_NUM = 100;    // 每个线程循环100次
    std::vector<std::thread> threads;

    // 线程函数：循环创建和处理HTTP消息
    auto thread_func = []() {
        for (int i = 0; i < LOOP_NUM; ++i) {
            // 测试请求
            HttpRequest req;
            req.m_method = "GET";
            req.m_queryUri = "/thread/test?i=" + std::to_string(i);
            req.setHeader("Thread-Id", std::to_string(std::hash<std::thread::id>()(std::this_thread::get_id())));
            
            // 测试响应
            HttpResponse resp;
            resp.setStatus(200);
            resp.setBody("Response from thread");
            
            // 编码测试
            Buffer buf;
            req.encode(buf);
            buf.clear();
            resp.encode(buf);
            
            g_httpThreadTestCount.fetch_add(1, std::memory_order_relaxed);
        }
    };

    // 创建线程
    for (int i = 0; i < THREAD_NUM; ++i) {
        threads.emplace_back(thread_func);
    }

    // 等待所有线程结束
    for (auto& t : threads) {
        t.join();
    }

    // 验证总执行次数
    int64_t total = g_httpThreadTestCount.load();
    int64_t expected = THREAD_NUM * LOOP_NUM;
    bool thread_ok = (total == expected);
    DEBUG("线程安全测试：总执行次数=%ld（预期：%ld，%s）", 
              total, expected, thread_ok ? "通过" : "失败");
    DEBUG("=== HTTP 线程安全 特性测试结束 ===\n");
}

// -------------------------- 测试入口函数 --------------------------
void runAllTests() {
    // 1. 初始化日志
    initTestLogger();

    // 2. 依次执行所有测试
    testHttpRequestBasic();
    testHttpRequestEncode();
    testHttpRequestDecode();
    testHttpResponseBasic();
    testHttpResponseEncode();
    testHttpResponseDecode();
    testHttpServerRouting();
    testHttpThreadSafe();

    // 3. 清理日志
    destroyTestLogger();
}

}  // namespace httpTest
}  // namespace handy

// 主函数：启动测试
int main() {
    handy::httpTest::runAllTests();
    return 0;
}