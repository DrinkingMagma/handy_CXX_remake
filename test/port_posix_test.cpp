#include "port_posix.h"
#include "logger.h"  // 假设Logger类声明在此头文件中
#include <vector>
#include <thread>
#include <atomic>
#include <cassert>
#include <arpa/inet.h>
#include <unistd.h>

// 测试用全局原子变量（用于多线程测试计数）
std::atomic<int> g_threadSafeCount(0);

namespace handy {
namespace portTest {

// -------------------------- 测试辅助函数 --------------------------
/**
 * @brief 初始化Logger（输出到port_posix_test.log）
 */
void initTestLogger() {
    Logger::getInstance().setLogFileName("port_posix_test.log");
    Logger::getInstance().setLogLevel(Logger::LogLevel::LDEBUG);
    INFO("=== port_posix_test 测试开始 ===");
}

// -------------------------- 字节序转换测试 --------------------------
/**
 * @brief 测试16位整数字节序转换（有符号/无符号）
 */
void test_htobe_betoh_16bit() {
    DEBUG("=== 开始测试16位整数字节序转换 ===");

    // 测试无符号16位：主机序→网络序→主机序，验证一致性
    const uint16_t host_uint16 = 0x1234;
    uint16_t net_uint16 = port::htobe(host_uint16);
    uint16_t restore_uint16 = port::betoh(net_uint16);
    
    bool uint16_ok = (restore_uint16 == host_uint16);
    DEBUG("无符号16位测试: 原始=0x%x → 网络序=0x%x → 恢复=0x%x（%s）",
          host_uint16, net_uint16, restore_uint16, uint16_ok ? "通过" : "失败");

    // 测试有符号16位：验证符号位保留
    const int16_t host_int16 = -0x1234;  // 二进制含符号位
    int16_t net_int16 = port::htobe(host_int16);
    int16_t restore_int16 = port::betoh(net_int16);
    
    bool int16_ok = (restore_int16 == host_int16);
    DEBUG("有符号16位测试: 原始=%d → 网络序=%d → 恢复=%d（%s）",
          host_int16, net_int16, restore_int16, int16_ok ? "通过" : "失败");

    DEBUG("=== 16位整数字节序转换测试结束 ===\n");
}

/**
 * @brief 测试32位整数字节序转换（有符号/无符号）
 */
void test_htobe_betoh_32bit() {
    DEBUG("=== 开始测试32位整数字节序转换 ===");

    // 测试无符号32位：典型值验证（如IP地址段）
    const uint32_t host_uint32 = 0x12345678;
    uint32_t net_uint32 = port::htobe(host_uint32);
    uint32_t restore_uint32 = port::betoh(net_uint32);
    
    bool uint32_ok = (restore_uint32 == host_uint32);
    DEBUG("无符号32位测试: 原始=0x%x → 网络序=0x%x → 恢复=0x%x（%s）",
          host_uint32, net_uint32, restore_uint32, uint32_ok ? "通过" : "失败");

    // 测试有符号32位：大负数验证
    const int32_t host_int32 = -0x12345678;
    int32_t net_int32 = port::htobe(host_int32);
    int32_t restore_int32 = port::betoh(net_int32);
    
    bool int32_ok = (restore_int32 == host_int32);
    DEBUG("有符号32位测试: 原始=%d → 网络序=%d → 恢复=%d（%s）",
          host_int32, net_int32, restore_int32, int32_ok ? "通过" : "失败");

    DEBUG("=== 32位整数字节序转换测试结束 ===\n");
}

/**
 * @brief 测试64位整数字节序转换（有符号/无符号）
 */
void test_htobe_betoh_64bit() {
    DEBUG("=== 开始测试64位整数字节序转换 ===");

    // 测试无符号64位：大数值验证（如时间戳）
    const uint64_t host_uint64 = 0x123456789abcdef0ULL;
    uint64_t net_uint64 = port::htobe(host_uint64);
    uint64_t restore_uint64 = port::betoh(net_uint64);
    
    bool uint64_ok = (restore_uint64 == host_uint64);
    DEBUG("无符号64位测试: 原始=0x%lx → 网络序=0x%lx → 恢复=0x%lx（%s）",
          host_uint64, net_uint64, restore_uint64, uint64_ok ? "通过" : "失败");

    // 测试有符号64位：超大负数验证
    const int64_t host_int64 = -0x123456789abcdef0LL;
    int64_t net_int64 = port::htobe(host_int64);
    int64_t restore_int64 = port::betoh(net_int64);
    
    bool int64_ok = (restore_int64 == host_int64);
    DEBUG("有符号64位测试: 原始=%lld → 网络序=%lld → 恢复=%lld（%s）",
          host_int64, net_int64, restore_int64, int64_ok ? "通过" : "失败");

    DEBUG("=== 64位整数字节序转换测试结束 ===\n");
}

// -------------------------- 主机名解析测试 --------------------------
/**
 * @brief 测试主机名解析（域名+IP字符串）
 */
void test_getHostByName() {
    DEBUG("=== 开始测试主机名解析功能 ===");

    // 测试1：直接解析IP字符串（IPv4）
    struct in_addr ip_result;
    bool ip_ok = port::getHostByName("192.168.1.1", ip_result);
    std::string ip_str = port::addrToString(&ip_result);
    DEBUG("IP字符串解析测试: 输入=192.168.1.1 → 输出=%s（%s）",
          ip_str.c_str(), ip_ok ? "通过" : "失败");

    // 测试2：解析公共域名（如百度）
    struct in_addr domain_result;
    bool domain_ok = port::getHostByName("www.baidu.com", domain_result);
    std::string domain_ip = port::addrToString(&domain_result);
    DEBUG("域名解析测试: 输入=www.baidu.com → 输出=%s（%s）",
          domain_ip.c_str(), domain_ok ? "通过" : "失败");

    // 测试3：解析无效主机名（边界场景）
    struct in_addr invalid_result;
    bool invalid_ok = !port::getHostByName("invalid.example.invalid", invalid_result);
    DEBUG("无效主机名解析测试: 输入=invalid.example.invalid → 预期失败（%s）",
          invalid_ok ? "通过" : "失败");

    DEBUG("=== 主机名解析功能测试结束 ===\n");
}

/**
 * @brief 测试主机名解析多线程安全性
 */
void test_getHostByName_threadSafe() {
    DEBUG("=== 开始测试主机名解析线程安全 ===");

    const int THREAD_NUM = 5;    // 5个并发线程
    const int LOOP_NUM = 200;    // 每个线程解析200次
    const std::vector<std::string> test_hosts = {
        "www.baidu.com", "www.google.com", "127.0.0.1", "8.8.8.8", "www.github.com"
    };

    // 线程函数：并发解析不同主机名
    auto thread_func = [&]() {
        for (int i = 0; i < LOOP_NUM; ++i) {
            struct in_addr result;
            const std::string& host = test_hosts[i % test_hosts.size()];
            port::getHostByName(host, result);  // 重复调用验证线程安全
            g_threadSafeCount.fetch_add(1, std::memory_order_relaxed);
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

    // 验证执行次数（无崩溃即线程安全，计数仅辅助）
    int64_t total = g_threadSafeCount.load();
    int64_t expected = THREAD_NUM * LOOP_NUM;
    bool thread_ok = (total == expected);
    DEBUG("线程安全测试: 总解析次数=%lld（预期: %lld），无崩溃即线程安全（%s）",
          total, expected, thread_ok ? "计数一致" : "计数异常");

    DEBUG("=== 主机名解析线程安全测试结束 ===\n");
}

// -------------------------- 线程ID获取测试 --------------------------
/**
 * @brief 测试当前线程ID获取（单线程+多线程）
 */
void test_getCurrentThreadId() {
    DEBUG("=== 开始测试线程ID获取功能 ===");

    // 测试1：主线程ID获取
    uint64_t main_tid = port::getCurrentThreadId();
    DEBUG("主线程ID测试: 主线程ID=0x%lx（非0即有效，%s）",
          main_tid, main_tid != 0 ? "通过" : "失败");

    // 测试2：多线程ID唯一性验证
    const int THREAD_NUM = 3;
    std::vector<uint64_t> thread_ids(THREAD_NUM, 0);
    std::vector<std::thread> threads;

    for (int i = 0; i < THREAD_NUM; ++i) {
        threads.emplace_back([&, i]() {
            thread_ids[i] = port::getCurrentThreadId();
            sleep(1);  // 确保线程ID已写入
        });
    }

    // 等待线程完成
    for (auto& t : threads) {
        t.join();
    }

    // 验证线程ID非0且唯一
    bool thread_id_unique = true;
    for (int i = 0; i < THREAD_NUM; ++i) {
        if (thread_ids[i] == 0) {
            thread_id_unique = false;
            break;
        }
        for (int j = i + 1; j < THREAD_NUM; ++j) {
            if (thread_ids[i] == thread_ids[j]) {
                thread_id_unique = false;
                break;
            }
        }
    }

    // 打印所有线程ID
    std::string id_str;
    for (auto tid : thread_ids) {
        id_str += std::to_string(tid) + ", ";
    }
    if (!id_str.empty()) id_str.pop_back(), id_str.pop_back();  // 移除末尾逗号和空格

    DEBUG("多线程ID测试: 子线程ID列表=[%s]（%s）",
          id_str.c_str(), thread_id_unique ? "ID唯一且有效" : "存在重复或无效ID");

    DEBUG("=== 线程ID获取功能测试结束 ===\n");
}

// -------------------------- IP地址转换测试 --------------------------
/**
 * @brief 测试IPv4地址与字符串互转
 */
void test_addr_string_convert() {
    DEBUG("=== 开始测试IP地址与字符串互转 ===");

    // 测试1：in_addr → 字符串（正常场景）
    struct in_addr addr1;
    addr1.s_addr = htonl(0xc0a80101);  // 192.168.1.1
    std::string str1 = port::addrToString(&addr1);
    bool addr_to_str_ok = (str1 == "192.168.1.1");
    DEBUG("in_addr→字符串测试: 输入=0xc0a80101 → 输出=%s（%s）",
          str1.c_str(), addr_to_str_ok ? "通过" : "失败");

    // 测试2：字符串 → in_addr（正常场景）
    struct in_addr addr2;
    bool str_to_addr_ok1 = port::stringToAddr("8.8.8.8", &addr2);
    std::string str2 = port::addrToString(&addr2);
    DEBUG("字符串→in_addr测试1: 输入=8.8.8.8 → 输出=%s（%s）",
          str2.c_str(), (str_to_addr_ok1 && str2 == "8.8.8.8") ? "通过" : "失败");

    // 测试3：无效IP字符串转换（边界场景）
    struct in_addr addr3;
    bool str_to_addr_ok2 = !port::stringToAddr("256.256.256.256", &addr3);  // 无效IP
    DEBUG("无效IP转换测试: 输入=256.256.256.256 → 预期失败（%s）",
          str_to_addr_ok2 ? "通过" : "失败");

    // 测试4：空指针输入（异常场景）
    std::string str4 = port::addrToString(nullptr);
    bool null_ptr_ok = (str4.empty());
    DEBUG("空指针处理测试: 输入=nullptr → 输出=%s（%s）",
          str4.c_str(), null_ptr_ok ? "通过" : "失败");

    DEBUG("=== IP地址与字符串互转测试结束 ===\n");
}

// -------------------------- 测试入口函数 --------------------------
void run_all_tests() {
    // 1. 初始化日志
    initTestLogger();

    // 2. 依次执行所有测试
    test_htobe_betoh_16bit();
    test_htobe_betoh_32bit();
    test_htobe_betoh_64bit();
    test_getHostByName();
    test_getHostByName_threadSafe();
    test_getCurrentThreadId();
    test_addr_string_convert();

    // 3. 测试总结
    INFO("=== port_posix_test 所有测试执行完成 ===");
}

}  // namespace portTest
}  // namespace handy

// 主函数：启动测试
int main() {
    handy::portTest::run_all_tests();
    return 0;
}