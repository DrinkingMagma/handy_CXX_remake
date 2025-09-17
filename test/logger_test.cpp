#include "logger.h"
#include <thread>
#include <vector>
#include <chrono>

int main() {
    // ========== 测试1：日志级别控制 ==========
    handy::Logger& logger = handy::Logger::getInstance();

    logger.setLogLevel(handy::Logger::LTRACE);
    TRACE("This is a TRACE log");
    DEBUG("This is a DEBUG log");
    INFO("This is an INFO log");
    WARN("This is a WARN log");
    ERROR("This is an ERROR log");
    // FATAL("This is a FATAL log"); // 注释：避免终止程序

    logger.setLogLevel(handy::Logger::LINFO);
    TRACE("This TRACE log should NOT print");
    DEBUG("This DEBUG log should NOT print");
    INFO("This INFO log should print");
    WARN("This WARN log should print");


    // ========== 测试2：日志文件输出 ==========
    SET_LOG_FILE("logger_test.log"); // 已修正宏名，对应 setLogFileName
    INFO("Log to file: logger_test.log");
    // EXIT_IF(true, "Exit now");


    // ========== 测试3：日志轮转（时间触发） ==========
    // 需修改logger.h的最小轮转时间
    // logger.setLogRotateInterval(10); // 10秒轮转一次
    // INFO("Log will rotate every 10 seconds");
    // std::this_thread::sleep_for(std::chrono::seconds(11)); // 等待触发轮转
    // INFO("After sleep, check if log rotated by time");


    // ========== 测试4：日志轮转（大小触发） ==========
    logger.setMaxLogFileSize(1); // 1MB限制
    INFO("Log file size limit set to 1MB");
    for (int i = 0; i < 10000; ++i) {
        INFO("Generate large log to trigger size rotation: %d", i);
        if (i % 100 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }


    // ========== 测试5：条件日志宏（注释终止语句） ==========
    // FATAL_IF(true, "FATAL_IF triggered"); // 注释：避免终止
    // CHECK(1 == 2, "1 != 2"); // 注释：避免终止
    EXIT_IF(true, "EXIT_IF triggered"); // 注释：避免终止


    // ========== 测试6：多线程日志（并发安全） ==========
    std::vector<std::thread> threads;
    for (int i = 0; i < 5; ++i) {
        threads.emplace_back([i]() {
            for (int j = 0; j < 10; ++j) {
                INFO("Thread %d, log %d", i, j); // 符合 fmt + 变量格式
            }
        });
    }
    for (auto& t : threads) t.join();


    return 0;
}