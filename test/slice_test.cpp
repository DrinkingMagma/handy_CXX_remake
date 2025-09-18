// Slice_test.cpp
#include "slice.h"
#include "logger.h"  // 假设Logger类声明在此头文件中（需与项目中Logger路径匹配）
#include <cstdio>
#include <cstring>
#include <vector>
#include <thread>
#include <atomic>
#include <unistd.h>
#include <fcntl.h>
#include <sstream>

// 测试用全局原子变量（用于多线程测试计数）
std::atomic<int> g_sliceThreadTestCount(0);

namespace handy {
namespace sliceTest {

// -------------------------- 测试辅助函数 --------------------------
/**
 * @brief 初始化Logger（输出到slice_test.log）
 */
void initTestLogger() {
    Logger::getInstance().setLogFileName("slice_test.log");
    Logger::getInstance().setLogLevel(Logger::LogLevel::LDEBUG);
    INFO("=== slice_test 测试开始 ===");
}

/**
 * @brief 测试结束时关闭Logger（根据实际Logger接口调整）
 */
void destroy_test_logger() {
    INFO("=== slice_test 测试结束 ===");
}

// -------------------------- 各功能测试函数 --------------------------
/**
 * @brief 测试 Slice 构造函数
 */
void test_constructors() {
    DEBUG("=== 开始测试 Slice 构造函数 ===");

    // 测试1：默认构造
    Slice s1;
    bool test1_ok = (s1.empty() && s1.size() == 0);
    DEBUG("测试1（默认构造）：%s", test1_ok ? "通过" : "失败");

    // 测试2：从[b, e)区间构造
    const char* str = "hello world";
    Slice s2(str, str + 5);
    bool test2_ok = (s2.toString() == "hello");
    DEBUG("测试2（[b,e)构造）：%s", test2_ok ? "通过" : "失败");

    // 测试3：从指针+长度构造
    Slice s3(str, 5);
    bool test3_ok = (s3.toString() == "hello");
    DEBUG("测试3（指针+长度构造）：%s", test3_ok ? "通过" : "失败");

    // 测试4：从 std::string 构造
    std::string stdStr = "hello world";
    Slice s4(stdStr);
    bool test4_ok = (s4.toString() == "hello world");
    DEBUG("测试4（std::string构造）：%s", test4_ok ? "通过" : "失败");

    // 测试5：从 C 风格字符串构造
    Slice s5("hello world");
    bool test5_ok = (s5.toString() == "hello world");
    DEBUG("测试5（C风格字符串构造）：%s", test5_ok ? "通过" : "失败");

    DEBUG("=== Slice 构造函数测试结束 ===\n");
}

/**
 * @brief 测试 Slice 基础访问接口
 */
void test_basic_access() {
    DEBUG("=== 开始测试 Slice 基础访问接口 ===");

    Slice s("hello world");

    // 测试 data()
    bool test1_ok = (strcmp(s.data(), "hello world") == 0);
    DEBUG("测试1（data()）：%s", test1_ok ? "通过" : "失败");

    // 测试 begin() 和 end()
    bool test2_ok = (strncmp(s.begin(), "hello world", s.size()) == 0);
    DEBUG("测试2（begin()/end()）：%s", test2_ok ? "通过" : "失败");

    // 测试 front() 和 back()
    bool test3_ok = (s.front() == 'h' && s.back() == 'd');
    DEBUG("测试3（front()/back()）：%s", test3_ok ? "通过" : "失败");

    // 测试 size() 和 empty()
    bool test4_ok = (s.size() == 11 && !s.empty());
    DEBUG("测试4（size()/empty()）：%s", test4_ok ? "通过" : "失败");

    DEBUG("=== Slice 基础访问接口测试结束 ===\n");
}

/**
 * @brief 测试 Slice 视图修改接口
 */
void test_view_modification() {
    DEBUG("=== 开始测试 Slice 视图修改接口 ===");

    Slice s("  hello world  ");

    // 测试 resize()
    Slice s1 = s;
    s1.resize(5);
    bool test1_ok = (s1.toString() == "  hel");
    DEBUG("测试1（resize()）：%s", test1_ok ? "通过" : "失败");

    // 测试 clear()
    Slice s2 = s;
    s2.clear();
    bool test2_ok = (s2.empty());
    DEBUG("测试2（clear()）：%s", test2_ok ? "通过" : "失败");

    // 测试 eat()
    Slice s3 = s;
    Slice eaten = s3.eat(2);
    bool test3_ok = (eaten.toString() == "  " && s3.toString() == "hello world  ");
    DEBUG("测试3（eat()）：%s", test3_ok ? "通过" : "失败");

    // 测试 eatWord()
    Slice s4 = s;
    Slice word = s4.eatWord();
    bool test4_ok = (word.toString() == "hello" && s4.toString() == " world  ");
    DEBUG("测试4（eatWord()）：%s", test4_ok ? "通过" : "失败");

    // 测试 eatLine()
    Slice s5("hello\nworld");
    Slice line = s5.eatLine();
    bool test5_ok = (line.toString() == "hello" && s5.toString() == "world");
    DEBUG("测试5（eatLine()）：%s", test5_ok ? "通过" : "失败");

    // 测试 sub()
    Slice s6 = s;
    Slice sub = s6.sub(2, -2);
    bool test6_ok = (sub.toString() == "hello world");
    DEBUG("测试6（sub()）：%s", test6_ok ? "通过" : "失败");

    // 测试 trimSpace()
    Slice s7 = s;
    s7.trimSpace();
    bool test7_ok = (s7.toString() == "hello world");
    DEBUG("测试7（trimSpace()）：%s", test7_ok ? "通过" : "失败");

    DEBUG("=== Slice 视图修改接口测试结束 ===\n");
}

/**
 * @brief 测试 Slice 比较与转换接口
 */
void test_comparison_conversion() {
    DEBUG("=== 开始测试 Slice 比较与转换接口 ===");

    Slice s1("hello");
    Slice s2("world");

    // 测试 operator[]
    bool test1_ok = (s1[0] == 'h' && s1[4] == 'o');
    DEBUG("测试1（operator[]）：%s", test1_ok ? "通过" : "失败");

    // 测试 toString() 和 隐式转换
    std::string stdStr = s1;
    bool test2_ok = (stdStr == "hello");
    DEBUG("测试2（toString()/隐式转换）：%s", test2_ok ? "通过" : "失败");

    // 测试 compare()
    bool test3_ok = (s1.compare(s2) < 0);
    DEBUG("测试3（compare()）：%s", test3_ok ? "通过" : "失败");

    // 测试 startsWith() 和 endsWith()
    Slice s3("hello world");
    bool test4_ok = (s3.startsWith(s1) && s3.endsWith(s2));
    DEBUG("测试4（startsWith()/endsWith()）：%s", test4_ok ? "通过" : "失败");

    // 测试 split()
    Slice s4("a,b,c");
    auto parts = s4.split(',');
    bool test5_ok = (parts.size() == 3 && parts[0].toString() == "a" && parts[1].toString() == "b" && parts[2].toString() == "c");
    DEBUG("测试5（split()）：%s", test5_ok ? "通过" : "失败");

    DEBUG("=== Slice 比较与转换接口测试结束 ===\n");
}

/**
 * @brief 测试 Slice 异常处理
 */
void test_exceptions() {
    DEBUG("=== 开始测试 Slice 异常处理 ===");

    // 测试构造函数异常
    bool test1_ok = false;
    try {
        Slice s(nullptr, 5);
    } catch (const std::invalid_argument&) {
        test1_ok = true;
    }
    DEBUG("测试1（构造函数异常）：%s", test1_ok ? "通过" : "失败");

    // 测试 front()/back() 异常
    Slice emptySlice;
    bool test2_ok = false;
    try {
        emptySlice.front();
    } catch (const std::out_of_range&) {
        test2_ok = true;
    }
    DEBUG("测试2（front()异常）：%s", test2_ok ? "通过" : "失败");

    // 测试 resize() 异常
    bool test3_ok = false;
    try {
        Slice s("hello");
        s.resize(10);
    } catch (const std::out_of_range&) {
        test3_ok = true;
    }
    DEBUG("测试3（resize()异常）：%s", test3_ok ? "通过" : "失败");

    DEBUG("=== Slice 异常处理测试结束 ===\n");
}

/**
 * @brief 多线程测试（验证线程安全：构造/访问）
 */
void test_thread_safe() {
    DEBUG("=== 开始测试 Slice 线程安全 特性 ===");

    const int THREAD_NUM = 5;    // 5个测试线程
    const int LOOP_NUM = 100;    // 每个线程循环100次
    std::vector<std::thread> threads;

    // 线程函数：循环创建和访问Slice（高频操作验证竞争）
    auto thread_func = []() {
        for (int i = 0; i < LOOP_NUM; ++i) {
            Slice s("hello world");
            std::string str = s.toString();
            DEBUG("线程%d-%d：slice=%s", std::this_thread::get_id(), i, str.c_str());
            g_sliceThreadTestCount.fetch_add(1, std::memory_order_relaxed);
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

    // 验证总执行次数（确保无线程遗漏）
    int64_t total = g_sliceThreadTestCount.load();
    int64_t expected = THREAD_NUM * LOOP_NUM;
    bool thread_ok = (total == expected);
    DEBUG("线程安全测试：总执行次数=%lld（预期：%lld，%s）", 
              total, expected, thread_ok ? "通过" : "失败");
    DEBUG("=== Slice 线程安全 特性测试结束 ===\n");
}

// -------------------------- 测试入口函数 --------------------------
void run_all_tests() {
    // 1. 初始化日志
    initTestLogger();

    // 2. 依次执行所有测试
    test_constructors();
    test_basic_access();
    test_view_modification();
    test_comparison_conversion();
    test_exceptions();
    test_thread_safe();

    // 3. 清理日志
    destroy_test_logger();
}

}  // namespace sliceTest
}  // namespace handy

// 主函数：启动测试
int main() {
    handy::sliceTest::run_all_tests();
    return 0;
}