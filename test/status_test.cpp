// Status_test.cpp
#include "status.h"  // 包含Status类的头文件
#include "logger.h"  // 假设Logger类声明在此头文件中
#include <cstdio>
#include <cstring>
#include <vector>
#include <thread>
#include <atomic>
#include <unistd.h>
#include <fcntl.h>
#include <sstream>
#include <stdexcept>

// 测试用全局原子变量（用于多线程测试计数）
std::atomic<int> g_statusThreadTestCount(0);

namespace handy {
namespace statusTest {

// -------------------------- 测试辅助函数 --------------------------
/**
 * @brief 初始化Logger（输出到status_test.log）
 */
void initTestLogger() {
    Logger::getInstance().setLogFileName("status_test.log");
    Logger::getInstance().setLogLevel(Logger::LogLevel::LDEBUG);
    INFO("=== status_test 测试开始 ===");
}

/**
 * @brief 测试结束时关闭Logger
 */
void destroyTestLogger() {
    INFO("=== status_test 测试结束 ===");
}

// -------------------------- 各功能测试函数 --------------------------
/**
 * @brief 测试Status构造函数和基础属性
 */
void testConstructors() {
    DEBUG("=== 开始测试Status构造函数 ===");

    // 测试1：默认构造（成功状态）
    Status s1;
    bool test1Ok = (s1.ok() && s1.code() == 0 && strcmp(s1.msg(), "") == 0);
    DEBUG("测试1（默认构造）：%s", test1Ok ? "通过" : "失败");

    // 测试2：带错误码和消息的构造
    Status s2(1, "test error");
    bool test2Ok = (s2.code() == 1 && strcmp(s2.msg(), "test error") == 0 && !s2.ok());
    DEBUG("测试2（错误码+消息构造）：%s", test2Ok ? "通过" : "失败");

    // 测试3：std::string消息构造
    Status s3(2, std::string("string error"));
    bool test3Ok = (s3.code() == 2 && strcmp(s3.msg(), "string error") == 0);
    DEBUG("测试3（std::string消息构造）：%s", test3Ok ? "通过" : "失败");

    // 测试4：空消息处理
    Status s4(3, nullptr);
    bool test4Ok = (s4.code() == 3 && strcmp(s4.msg(), "") == 0);
    DEBUG("测试4（空消息处理）：%s", test4Ok ? "通过" : "失败");

    DEBUG("=== Status构造函数测试结束 ===\n");
}

/**
 * @brief 测试Status拷贝和移动语义
 */
void testCopyAndMove() {
    DEBUG("=== 开始测试拷贝和移动语义 ===");

    // 测试1：拷贝构造
    Status s1(10, "copy test");
    Status s2(s1);
    bool test1Ok = (s1.code() == s2.code() && strcmp(s1.msg(), s2.msg()) == 0);
    DEBUG("测试1（拷贝构造）：%s", test1Ok ? "通过" : "失败");

    // 测试2：拷贝赋值
    Status s3;
    s3 = s1;
    bool test2Ok = (s3.code() == 10 && strcmp(s3.msg(), "copy test") == 0);
    DEBUG("测试2（拷贝赋值）：%s", test2Ok ? "通过" : "失败");

    // 测试3：移动构造
    Status s4(20, "move test");
    Status s5(std::move(s4));
    bool test3Ok = (s5.code() == 20 && strcmp(s5.msg(), "move test") == 0 && s4.ok());  // 移动后原对象应处于有效状态
    DEBUG("测试3（移动构造）：%s", test3Ok ? "通过" : "失败");

    // 测试4：移动赋值
    Status s6;
    s6 = std::move(s5);
    bool test4Ok = (s6.code() == 20 && strcmp(s6.msg(), "move test") == 0 && s5.ok());
    DEBUG("测试4（移动赋值）：%s", test4Ok ? "通过" : "失败");

    DEBUG("=== 拷贝和移动语义测试结束 ===\n");
}

/**
 * @brief 测试Status静态创建函数
 */
void testStaticCreators() {
    DEBUG("=== 开始测试静态创建函数 ===");

    // 测试1：fromSystem()（基于当前errno）
    errno = EIO;  // 设置已知错误码
    Status s1 = Status::fromSystem();
    bool test1Ok = (s1.code() == EIO && strstr(s1.msg(), "Input/output error") != nullptr);
    DEBUG("测试1（fromSystem()）：%s", test1Ok ? "通过" : "失败");

    // 测试2：fromSystem(int err)（指定错误码）
    Status s2 = Status::fromSystem(ENOENT);
    bool test2Ok = (s2.code() == ENOENT && strstr(s2.msg(), "No such file") != nullptr);
    DEBUG("测试2（fromSystem(int)）：%s", test2Ok ? "通过" : "失败");

    // 测试3：fromFormat()（格式化消息）
    Status s3 = Status::fromFormat(3, "format %d %s", 123, "test");
    bool test3Ok = (s3.code() == 3 && strcmp(s3.msg(), "format 123 test") == 0);
    DEBUG("测试3（fromFormat()）：%s", test3Ok ? "通过" : "失败");

    // 测试4：ioError()（I/O错误封装）
    errno = EACCES;
    Status s4 = Status::ioError("read", "test.txt");
    bool test4Ok = (s4.code() == EACCES && strstr(s4.msg(), "I/O error: read test.txt:") != nullptr);
    DEBUG("测试4（ioError()）：%s", test4Ok ? "通过" : "失败");

    // 测试5：fromFormat异常情况（无效格式化字符串）
    Status s5 = Status::fromFormat(4, nullptr);
    bool test5Ok = (s5.code() == 4 && strcmp(s5.msg(), "") == 0);
    DEBUG("测试5（fromFormat空字符串）：%s", test5Ok ? "通过" : "失败");

    DEBUG("=== 静态创建函数测试结束 ===\n");
}

/**
 * @brief 测试Status工具函数（toString等）
 */
void testUtils() {
    DEBUG("=== 开始测试工具函数 ===");

    // 测试1：toString()
    Status s1(5, "utils test");
    std::string str1 = s1.toString();
    bool test1Ok = (str1 == "error code: 5, error msg: utils test");
    DEBUG("测试1（toString()）：%s", test1Ok ? "通过" : "失败");

    // 测试2：成功状态的toString()
    Status s2;
    std::string str2 = s2.toString();
    bool test2Ok = (str2 == "error code: 0, error msg: ");
    DEBUG("测试2（成功状态toString()）：%s", test2Ok ? "通过" : "失败");

    DEBUG("=== 工具函数测试结束 ===\n");
}

/**
 * @brief 测试内存分配失败处理
 */
void testMemoryFailure() {
    DEBUG("=== 开始测试内存分配失败处理 ===");

    // 模拟内存分配失败（通过极大尺寸触发）
    // 注意：此测试可能影响系统稳定性，仅在受控环境下运行
    bool test1Ok = false;
    try {
        // 分配接近最大可能的内存（实际环境中可能返回分配失败状态）
        Status s = Status::fromFormat(999, "%s", std::string(1024 * 1024 * 1024, 'x').c_str());
        test1Ok = true;  // 若未崩溃则通过（分配成功或已处理失败）
    } catch (...) {
        test1Ok = false;  // 捕获到异常则失败
    }
    DEBUG("测试1（大内存分配容错）：%s", test1Ok ? "通过" : "失败");

    DEBUG("=== 内存分配失败处理测试结束 ===\n");
}

/**
 * @brief 多线程测试（验证线程安全）
 */
void testThreadSafe() {
    DEBUG("=== 开始测试线程安全特性 ===");

    const int THREAD_NUM = 10;   // 10个测试线程
    const int LOOP_NUM = 1000;   // 每个线程循环1000次
    std::vector<std::thread> threads;

    // 线程函数：并发创建和访问Status对象
    auto threadFunc = []() {
        for (int i = 0; i < LOOP_NUM; ++i) {
            // 测试系统错误转换
            Status s1 = Status::fromSystem(i % 10);  // 循环使用不同错误码
            
            // 测试格式化创建
            Status s2 = Status::fromFormat(i, "thread %d, loop %d", 
                                          std::this_thread::get_id(), i);
            
            // 验证基本属性（避免优化掉代码）
            if (s1.code() < 0 || s2.msg() == nullptr) {
                throw std::runtime_error("线程安全测试失败");
            }
            
            g_statusThreadTestCount.fetch_add(1, std::memory_order_relaxed);
        }
    };

    // 创建线程
    for (int i = 0; i < THREAD_NUM; ++i) {
        threads.emplace_back(threadFunc);
    }

    // 等待所有线程结束
    for (auto& t : threads) {
        t.join();
    }

    // 验证总执行次数
    int64_t total = g_statusThreadTestCount.load();
    int64_t expected = THREAD_NUM * LOOP_NUM;
    bool threadOk = (total == expected);
    DEBUG("线程安全测试：总执行次数=%lld（预期：%lld，%s）", 
              total, expected, threadOk ? "通过" : "失败");

    DEBUG("=== 线程安全特性测试结束 ===\n");
}

// -------------------------- 测试入口函数 --------------------------
void runAllTests() {
    // 1. 初始化日志
    initTestLogger();

    // 2. 依次执行所有测试
    testConstructors();
    testCopyAndMove();
    testStaticCreators();
    testUtils();
    testMemoryFailure();
    testThreadSafe();

    // 3. 清理日志
    destroyTestLogger();
}

}  // namespace statusTest
}  // namespace handy

// 主函数：启动测试
int main() {
    handy::statusTest::runAllTests();
    return 0;
}