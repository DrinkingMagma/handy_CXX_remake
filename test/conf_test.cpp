#include "conf.h"
#include "logger.h"  // 假设Logger类声明在此头文件中
#include <fstream>
#include <vector>
#include <cassert>
#include <algorithm>
#include <thread>
#include <atomic>

// 测试用全局原子变量（用于多线程测试计数）
std::atomic<int> g_threadTestCount(0);

namespace handy {
namespace confTest {

// -------------------------- 测试辅助函数 --------------------------
/**
 * @brief 初始化Logger（输出到conf_test.log）
 */
void initTestLogger() {
    Logger::getInstance().setLogFileName("conf_test.log");
    Logger::getInstance().setLogLevel(Logger::LogLevel::LDEBUG);
    INFO("=== conf_test 测试开始 ===");
}

/**
 * @brief 创建临时测试INI文件
 * @param content 文件内容
 * @param filename 临时文件名
 * @return 创建成功返回true，否则返回false
 */
bool createTempIniFile(const std::string& content, const std::string& filename) {
    std::ofstream ofs(filename);
    if (!ofs.is_open()) {
        ERROR("创建临时文件失败: %s", filename.c_str());
        return false;
    }
    ofs << content;
    ofs.close();
    return true;
}

/**
 * @brief 删除临时测试文件
 * @param filename 临时文件名
 */
void deleteTempIniFile(const std::string& filename) {
    std::remove(filename.c_str());
}

// -------------------------- 各功能测试函数 --------------------------
/**
 * @brief 测试基本的INI文件解析功能
 */
void test_basic_parsing() {
    DEBUG("=== 开始测试基本INI解析功能 ===");
    
    // 测试INI内容
    const std::string iniContent = R"(
; 这是注释行
# 这也是注释行

[Server]
Port = 8080
EnableSSL = true
Timeout = 30.5

[Database]
Host: localhost
User = root
Password : 123456
)";
    
    const std::string tempFile = "basic_test.ini";
    if (!createTempIniFile(iniContent, tempFile)) {
        DEBUG("=== 基本INI解析功能测试结束（失败） ===\n");
        return;
    }

    Conf conf;
    int ret = conf.parse(tempFile);
    if (ret != 0) {
        ERROR("解析文件失败，错误码: %d", ret);
        deleteTempIniFile(tempFile);
        DEBUG("=== 基本INI解析功能测试结束（失败） ===\n");
        return;
    }

    // 测试获取字符串
    std::string port = conf.get("Server", "Port", "");
    DEBUG("Server.Port测试: %s（预期: 8080，%s）", 
          port.c_str(), port == "8080" ? "通过" : "失败");

    // 测试获取整数
    long timeout = conf.getInteger("Server", "Timeout", -1);
    DEBUG("Server.Timeout整数测试: %ld（预期: 30，%s）", 
          timeout, timeout == 30 ? "通过" : "失败");

    // 测试获取浮点数
    double timeoutReal = conf.getReal("Server", "Timeout", -1);
    DEBUG("Server.Timeout浮点数测试: %.1f（预期: 30.5，%s）", 
          timeoutReal, abs(timeoutReal - 30.5) < 0.001 ? "通过" : "失败");

    // 测试获取布尔值
    bool ssl = conf.getBoolean("Server", "EnableSSL", false);
    DEBUG("Server.EnableSSL测试: %s（预期: true，%s）", 
          ssl ? "true" : "false", ssl ? "通过" : "失败");

    // 测试:分隔符的键值对
    std::string host = conf.get("Database", "Host", "");
    DEBUG("Database.Host测试: %s（预期: localhost，%s）", 
          host.c_str(), host == "localhost" ? "通过" : "失败");

    // 清理临时文件
    deleteTempIniFile(tempFile);
    DEBUG("=== 基本INI解析功能测试结束 ===\n");
}

/**
 * @brief 测试大小写不敏感特性
 */
void test_case_insensitivity() {
    DEBUG("=== 开始测试大小写不敏感特性 ===");
    
    const std::string iniContent = R"(
[TESTSECTION]
TestKey = TestValue
)";
    
    const std::string tempFile = "case_test.ini";
    createTempIniFile(iniContent, tempFile);

    Conf conf;
    conf.parse(tempFile);

    // 测试不同大小写组合
    std::string val1 = conf.get("testsection", "testkey", "");
    std::string val2 = conf.get("TestSection", "TestKey", "");
    std::string val3 = conf.get("TESTSECTION", "TESTKEY", "");

    bool allEqual = (val1 == "TestValue" && val2 == "TestValue" && val3 == "TestValue");
    DEBUG("大小写不敏感测试: %s（%s）", 
          allEqual ? "所有组合匹配" : "存在不匹配", allEqual ? "通过" : "失败");

    deleteTempIniFile(tempFile);
    DEBUG("=== 大小写不敏感特性测试结束 ===\n");
}

/**
 * @brief 测试多行值（续行）功能
 */
void test_multi_line_values() {
    DEBUG("=== 开始测试多行值功能 ===");
    
    const std::string iniContent = R"(
[List]
Items = item1
  item2
  item3
IPs = 192.168.1.1
  192.168.1.2
  192.168.1.3
)";
    
    const std::string tempFile = "multi_line_test.ini";
    createTempIniFile(iniContent, tempFile);

    Conf conf;
    conf.parse(tempFile);

    // 测试多行值获取
    std::list<std::string> items = conf.getStrings("List", "Items");
    std::list<std::string> expectedItems = {"item1", "item2", "item3"};
    
    bool itemsMatch = (items == expectedItems);
    
    // 同时遍历两个列表（需确保长度相同）
    auto expIt = expectedItems.begin();
    for (const auto& item : items) {
        if (expIt == expectedItems.end()) break; // 避免越界
        DEBUG("item=%s, expected=%s", item.c_str(), expIt->c_str());
        ++expIt;
    }

    DEBUG("多行值Items测试: 数量=%zu（预期: 3，%s）", 
          items.size(), itemsMatch ? "通过" : "失败");

    std::list<std::string> ips = conf.getStrings("List", "IPs");
    bool ipsMatch = (ips.size() == 3);
    DEBUG("多行值IPs测试: 数量=%zu（预期: 3，%s）", 
          ips.size(), ipsMatch ? "通过" : "失败");

    deleteTempIniFile(tempFile);
    DEBUG("=== 多行值功能测试结束 ===\n");
}

/**
 * @brief 测试各种数据类型解析
 */
void test_data_types() {
    DEBUG("=== 开始测试数据类型解析 ===");
    
    const std::string iniContent = R"(
[Numbers]
Decimal = 12345
Negative = -678
Hex = 0x1a3f
Float = 3.14159
Scientific = 1.23e-4
BigNumber = 9223372036854775807

[Booleans]
True1 = true
True2 = yes
True3 = on
True4 = 1
False1 = false
False2 = no
False3 = off
False4 = 0
)";
    
    const std::string tempFile = "data_type_test.ini";
    createTempIniFile(iniContent, tempFile);

    Conf conf;
    conf.parse(tempFile);

    // 测试整数解析
    long decimal = conf.getInteger("Numbers", "Decimal", -1);
    DEBUG("十进制整数测试: %ld（预期: 12345，%s）", 
          decimal, decimal == 12345 ? "通过" : "失败");

    long negative = conf.getInteger("Numbers", "Negative", 0);
    DEBUG("负数测试: %ld（预期: -678，%s）", 
          negative, negative == -678 ? "通过" : "失败");

    long hex = conf.getInteger("Numbers", "Hex", 0);
    DEBUG("十六进制测试: %ld（预期: 6719，%s）", 
          hex, hex == 0x1a3f ? "通过" : "失败");

    // 测试浮点数解析
    double floatVal = conf.getReal("Numbers", "Float", -1);
    DEBUG("浮点数测试: %.5f（预期: 3.14159，%s）", 
          floatVal, abs(floatVal - 3.14159) < 0.00001 ? "通过" : "失败");

    double sciVal = conf.getReal("Numbers", "Scientific", -1);
    DEBUG("科学计数法测试: %.6f（预期: 0.000123，%s）", 
          sciVal, abs(sciVal - 0.000123) < 0.000001 ? "通过" : "失败");

    // 测试布尔值解析
    bool true1 = conf.getBoolean("Booleans", "True1", false);
    bool true2 = conf.getBoolean("Booleans", "True2", false);
    bool true3 = conf.getBoolean("Booleans", "True3", false);
    bool true4 = conf.getBoolean("Booleans", "True4", false);
    
    bool truesOk = true1 && true2 && true3 && true4;
    DEBUG("真值测试: %s", truesOk ? "全部通过" : "存在失败");

    bool false1 = conf.getBoolean("Booleans", "False1", true);
    bool false2 = conf.getBoolean("Booleans", "False2", true);
    bool false3 = conf.getBoolean("Booleans", "False3", true);
    bool false4 = conf.getBoolean("Booleans", "False4", true);
    
    bool falsesOk = !false1 && !false2 && !false3 && !false4;
    DEBUG("假值测试: %s", falsesOk ? "全部通过" : "存在失败");

    deleteTempIniFile(tempFile);
    DEBUG("=== 数据类型解析测试结束 ===\n");
}

/**
 * @brief 测试错误处理和边界情况
 */
void test_error_handling() {
    DEBUG("=== 开始测试错误处理和边界情况 ===");
    
    // 测试1: 不存在的文件
    Conf conf1;
    int ret1 = conf1.parse("nonexistent_file.ini");
    DEBUG("不存在的文件测试: 返回值=%d（预期: -1，%s）", 
          ret1, ret1 == -1 ? "通过" : "失败");

    // 测试2: 格式错误的INI文件（未闭合的节）
    const std::string badContent = R"(
[BadSection
Key = Value
)";
    const std::string badFile = "bad_format_test.ini";
    createTempIniFile(badContent, badFile);
    
    Conf conf2;
    int ret2 = conf2.parse(badFile);
    DEBUG("格式错误文件测试: 返回值=%d（预期: 正数行号，%s）", 
          ret2, ret2 > 0 ? "通过" : "失败");

    // 测试3: 获取不存在的键
    const std::string goodFile = "good_test.ini";
    createTempIniFile("[Section]\nKey=Value", goodFile);
    
    Conf conf3;
    conf3.parse(goodFile);
    
    std::string defStr = conf3.get("Section", "Nonexistent", "default");
    long defInt = conf3.getInteger("Section", "Nonexistent", 123);
    bool defBool = conf3.getBoolean("Section", "Nonexistent", true);
    
    bool defaultsOk = (defStr == "default" && defInt == 123 && defBool == true);
    DEBUG("默认值测试: %s", defaultsOk ? "通过" : "失败");

    deleteTempIniFile(badFile);
    deleteTempIniFile(goodFile);
    DEBUG("=== 错误处理和边界情况测试结束 ===\n");
}

/**
 * @brief 测试多线程安全性
 */
void test_thread_safety() {
    DEBUG("=== 开始测试线程安全特性 ===");
    
    const std::string iniContent = R"(
[ThreadTest]
Value1 = 100
Value2 = 3.14
Value3 = true
Value4 = test_string
)";
    const std::string tempFile = "thread_test.ini";
    createTempIniFile(iniContent, tempFile);

    // 加载配置
    Conf conf;
    conf.parse(tempFile);

    const int THREAD_NUM = 4;    // 4个测试线程
    const int LOOP_NUM = 1000;   // 每个线程循环1000次
    
    // 线程函数: 并发读取配置
    auto thread_func = [&conf]() {
        for (int i = 0; i < LOOP_NUM; ++i) {
            // 混合调用各种获取方法
            conf.get("ThreadTest", "Value4", "");
            conf.getInteger("ThreadTest", "Value1", 0);
            conf.getReal("ThreadTest", "Value2", 0.0);
            conf.getBoolean("ThreadTest", "Value3", false);
            g_threadTestCount.fetch_add(1, std::memory_order_relaxed);
        }
    };

    // 创建并启动线程
    std::vector<std::thread> threads;
    for (int i = 0; i < THREAD_NUM; ++i) {
        threads.emplace_back(thread_func);
    }

    // 等待所有线程完成
    for (auto& t : threads) {
        t.join();
    }

    // 验证执行次数
    int64_t total = g_threadTestCount.load();
    int64_t expected = THREAD_NUM * LOOP_NUM;
    bool threadOk = (total == expected);
    DEBUG("线程安全测试: 总执行次数=%lld（预期: %lld，%s）", 
          total, expected, threadOk ? "通过" : "失败");

    deleteTempIniFile(tempFile);
    DEBUG("=== 线程安全特性测试结束 ===\n");
}

// -------------------------- 测试入口函数 --------------------------
void run_all_tests() {
    // 1. 初始化日志
    initTestLogger();

    // 2. 依次执行所有测试
    test_basic_parsing();
    test_case_insensitivity();
    test_multi_line_values();
    test_data_types();
    test_error_handling();
    test_thread_safety();

    // 3. 测试总结
    INFO("=== conf_test 所有测试执行完成 ===");
}

}  // namespace confTest
}  // namespace handy

// 主函数：启动测试
int main() {
    handy::confTest::run_all_tests();
    return 0;
}
    