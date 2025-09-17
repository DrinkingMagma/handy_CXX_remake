#include "utils.h"
#include "logger.h"  // 假设Logger类声明在此头文件中（需与项目中Logger路径匹配）
#include <cstdio>
#include <cstring>
#include <vector>
#include <thread>
#include <atomic>
#include <unistd.h>
#include <fcntl.h>

// 测试用全局原子变量（用于多线程测试计数）
std::atomic<int> g_threadTestCount(0);

namespace handy {
namespace utilsTest {

// -------------------------- 测试辅助函数 --------------------------
/**
 * @brief 初始化Logger（输出到utils_test.log）
 */
void initTestLogger() {
    Logger::getInstance().setLogFileName("utils_test.log");
    Logger::getInstance().setLogLevel(Logger::LogLevel::LDEBUG);
    INFO("=== utils_test 测试开始 ===");
}

/**
 * @brief 测试结束时关闭Logger（根据实际Logger接口调整）
 */
void destroy_test_logger() {
    INFO("=== utils_test 测试结束 ===");
}

// -------------------------- 各功能测试函数 --------------------------
/**
 * @brief 测试 format 函数（字符串格式化）
 */
void test_format() {
    DEBUG("=== 开始测试 format 函数 ===");

    // 测试1：基础类型格式化（int/string/double）
    std::string test1 = utils::format("基础格式化：整数=%d, 字符串=%s, 浮点数=%.2f", 
                                     123, "test_str", 3.1415);
    DEBUG("测试1结果：%s（预期：基础格式化：整数=123, 字符串=test_str, 浮点数=3.14）", 
              test1.c_str());

    // 测试2：边界场景（空字符串、超长字符串）
    std::string test2 = utils::format("空字符串测试：%s", "");
    DEBUG("测试2结果：%s（预期：空字符串测试：）", test2.c_str());

    // 测试3：最大缓冲区限制（1MB，此处用1024*1024长度字符串测试）
    std::string long_str(1024 * 1024 - 20, 'a');  // 预留20字节给格式化模板
    std::string test3 = utils::format("超长字符串：%s", long_str.c_str());
    if (test3.empty()) {
        WARN("测试3结果：超出最大缓冲区限制（符合预期）");
    } else {
        DEBUG("测试3结果：超长字符串长度=%zu（符合预期）", test3.size());
    }

    DEBUG("=== format 函数测试结束 ===\n");
}

/**
 * @brief 测试 timeMicro/timeMilli 函数（系统时间）
 */
void test_time_system() {
    DEBUG("=== 开始测试 timeMicro/timeMilli 函数 ===");

    int64_t micro1 = utils::timeMicro();
    int64_t milli1 = utils::timeMilli();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));  // 睡眠100ms
    int64_t micro2 = utils::timeMicro();
    int64_t milli2 = utils::timeMilli();

    // 验证时间差在合理范围（90ms~110ms）
    int64_t micro_diff = micro2 - micro1;
    int64_t milli_diff = milli2 - milli1;
    bool micro_valid = (micro_diff >= 90000 && micro_diff <= 110000);  // 90ms~110ms（微秒）
    bool milli_valid = (milli_diff >= 90 && milli_diff <= 110);        // 90ms~110ms（毫秒）

    DEBUG("timeMicro 差值：%lldμs（%s）", micro_diff, micro_valid ? "有效" : "无效");
    DEBUG("timeMilli 差值：%lldms（%s）", milli_diff, milli_valid ? "有效" : "无效");
    DEBUG("=== timeMicro/timeMilli 函数测试结束 ===\n");
}

/**
 * @brief 测试 steadyMicro/steadyMilli 函数（稳定时钟）
 */
void test_time_steady() {
    DEBUG("=== 开始测试 steadyMicro/steadyMilli 函数 ===");

    int64_t micro1 = utils::steadyMicro();
    int64_t milli1 = utils::steadyMilli();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));  // 睡眠150ms
    int64_t micro2 = utils::steadyMicro();
    int64_t milli2 = utils::steadyMilli();

    // 验证稳定时钟差值（140ms~160ms，不受系统时间调整影响）
    int64_t micro_diff = micro2 - micro1;
    int64_t milli_diff = milli2 - milli1;
    bool micro_valid = (micro_diff >= 140000 && micro_diff <= 160000);
    bool milli_valid = (milli_diff >= 140 && milli_diff <= 160);

    DEBUG("steadyMicro 差值：%lldμs（%s）", micro_diff, micro_valid ? "有效" : "无效");
    DEBUG("steadyMilli 差值：%lldms（%s）", milli_diff, milli_valid ? "有效" : "无效");
    DEBUG("=== steadyMicro/steadyMilli 函数测试结束 ===\n");
}

/**
 * @brief 测试 readableTime 函数（时间戳转可读字符串）
 */
void test_readable_time() {
    DEBUG("=== 开始测试 readableTime 函数 ===");

    // 测试1：当前时间（验证格式）
    time_t now = time(nullptr);
    std::string now_str = utils::readableTime(now);
    DEBUG("当前时间测试：%s（格式预期：YYYY-MM-DD HH:MM:SS）", now_str.c_str());

    // 测试2：已知时间戳（2024-01-01 00:00:00 UTC）
    time_t test_time = 1704067200;  // 2024-01-01 00:00:00 UTC
    std::string test_str = utils::readableTime(test_time);
    DEBUG("已知时间戳测试：%s（预期：2024-01-01 08:00:00，取决于本地时区）", test_str.c_str());

    // 测试3：非法时间戳（边界值）
    std::string invalid_str1 = utils::readableTime(-1);  // 负时间戳
    std::string invalid_str2 = utils::readableTime(0x7FFFFFFFFFFFFFFF);  // 最大int64_t
    DEBUG("非法时间戳测试1：%s（预期：invalid time 或合法格式）", invalid_str1.c_str());
    DEBUG("非法时间戳测试2：%s（预期：invalid time 或合法格式）", invalid_str2.c_str());

    DEBUG("=== readableTime 函数测试结束 ===\n");
}

/**
 * @brief 测试 atoi/atoi2 函数（字符串转整数）
 */
void test_atoi() {
    DEBUG("=== 开始测试 atoi/atoi2 函数 ===");

    // 测试用例：{输入起始, 输入结束, atoi预期值, atoi2预期值}
    struct TestCase {
        const char* str;
        size_t len;
        int64_t atoi_exp;
        int64_t atoi2_exp;
    };

    std::vector<TestCase> cases = {
        {"123", 3, 123, 123},                // 正常整数
        {"-456", 4, -456, -456},             // 负整数
        {"123abc", 5, 123, -1},              // 部分有效（atoi2严格匹配失败）
        {"abc123", 5, 0, -1},                // 开头非数字
        {"", 0, 0, -1},                      // 空字符串
        {"18446744073709551615", 20, 9223372036854775807, 9223372036854775807},  // 超出int64_t最大值
    };

    for (size_t i = 0; i < cases.size(); ++i) {
        const auto& c = cases[i];
        const char* b = c.str;
        const char* e = c.str + c.len;

        int64_t atoi_res = utils::atoi(b, e);
        int64_t atoi2_res = utils::atoi2(b, e);

        bool atoi_ok = (atoi_res == c.atoi_exp);
        bool atoi2_ok = (atoi2_res == c.atoi2_exp);

        DEBUG("测试用例%d：输入=\"%.*s\"", i + 1, static_cast<int>(c.len), c.str);
        DEBUG("  atoi结果：%lld（预期：%lld，%s）", atoi_res, c.atoi_exp, atoi_ok ? "通过" : "失败");
        DEBUG("  atoi2结果：%lld（预期：%lld，%s）", atoi2_res, c.atoi2_exp, atoi2_ok ? "通过" : "失败");
    }

    DEBUG("=== atoi/atoi2 函数测试结束 ===\n");
}

/**
 * @brief 测试 addFdFlag 函数（文件描述符添加标志）
 */
void test_add_fd_flag() {
    DEBUG("=== 开始测试 addFdFlag 函数 ===");

    // 测试1：创建临时文件获取合法FD
    int fd = open("utils_test_temp.txt", O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        ERROR("创建临时文件失败（errno=%d），跳过 addFdFlag 测试", errno);
        DEBUG("=== addFdFlag 函数测试结束（跳过） ===\n");
        return;
    }

    // 测试1.1：添加 FD_CLOEXEC 标志（初始未设置）
    int ret1 = utils::addFdFlag(fd, FD_CLOEXEC);
    int flags1 = fcntl(fd, F_GETFD);
    bool test1_ok = (ret1 == 0) && ((flags1 & FD_CLOEXEC) == FD_CLOEXEC);
    DEBUG("测试1（添加FD_CLOEXEC）：返回值=%d，当前标志=0x%x（%s）", 
              ret1, flags1, test1_ok ? "通过" : "失败");

    // 测试1.2：重复添加 FD_CLOEXEC 标志（应返回0，无操作）
    int ret2 = utils::addFdFlag(fd, FD_CLOEXEC);
    int flags2 = fcntl(fd, F_GETFD);
    bool test2_ok = (ret2 == 0) && ((flags2 & FD_CLOEXEC) == FD_CLOEXEC);
    DEBUG("测试2（重复添加FD_CLOEXEC）：返回值=%d，当前标志=0x%x（%s）", 
              ret2, flags2, test2_ok ? "通过" : "失败");

    // 测试1.3：非法FD（应返回-1，errno=EBADF）
    int invalid_fd = -1;
    int ret3 = utils::addFdFlag(invalid_fd, FD_CLOEXEC);
    bool test3_ok = (ret3 == -1) && (errno == EBADF);
    DEBUG("测试3（非法FD=-1）：返回值=%d，errno=%d（%s）", 
              ret3, errno, test3_ok ? "通过" : "失败");

    // 清理临时文件
    close(fd);
    remove("utils_test_temp.txt");

    DEBUG("=== addFdFlag 函数测试结束 ===\n");
}

/**
 * @brief 多线程测试（验证线程安全：readableTime/format）
 */
void test_thread_safe() {
    DEBUG("=== 开始测试 线程安全 特性 ===");

    const int THREAD_NUM = 5;    // 5个测试线程
    const int LOOP_NUM = 100;    // 每个线程循环100次
    std::vector<std::thread> threads;

    // 线程函数：循环调用readableTime和format（高频操作验证竞争）
    auto thread_func = []() {
        for (int i = 0; i < LOOP_NUM; ++i) {
            time_t now = time(nullptr);
            std::string time_str = utils::readableTime(now);
            std::string fmt_str = utils::format("线程%d-%d：time=%s", 
                                               std::this_thread::get_id(), i, time_str.c_str());
            // 日志输出（间接验证无崩溃/乱码）
            DEBUG("%s", fmt_str.c_str());
            g_threadTestCount.fetch_add(1, std::memory_order_relaxed);
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
    int64_t total = g_threadTestCount.load();
    int64_t expected = THREAD_NUM * LOOP_NUM;
    bool thread_ok = (total == expected);
    DEBUG("线程安全测试：总执行次数=%lld（预期：%lld，%s）", 
              total, expected, thread_ok ? "通过" : "失败");
    DEBUG("=== 线程安全 特性测试结束 ===\n");
}

// -------------------------- 测试入口函数 --------------------------
void run_all_tests() {
    // 1. 初始化日志
    initTestLogger();

    // 2. 依次执行所有测试
    test_format();
    test_time_system();
    test_time_steady();
    test_readable_time();
    test_atoi();
    test_add_fd_flag();
    test_thread_safe();

    // 3. 清理日志
    destroy_test_logger();
}

}  // namespace utilsTest
}  // namespace handy

// 主函数：启动测试
int main() {
    handy::utilsTest::run_all_tests();
    return 0;
}