#include "daemon.h"
#include "utils.h"
#include "logger.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <thread>
#include <atomic>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

// 测试用PID文件路径
const char* TEST_PID_FILE = "/tmp/daemon_test.pid";
// 测试用信号标记
std::atomic<bool> g_signalReceived(false);

namespace handy {
namespace daemonTest {

// -------------------------- 测试辅助函数 --------------------------
/**
 * @brief 初始化Logger（输出到daemon_test.log）
 */
void initTestLogger() {
    Logger::getInstance().setLogFileName("daemon_test.log");
    Logger::getInstance().setLogLevel(Logger::LogLevel::LDEBUG);
    INFO("=== daemon_test 测试开始 ===");
}

/**
 * @brief 清理测试环境
 */
void cleanTestEnv() {
    // 移除测试PID文件
    if (access(TEST_PID_FILE, F_OK) == 0) {
        unlink(TEST_PID_FILE);
    }
    g_signalReceived = false;
}

/**
 * @brief 测试结束时关闭Logger
 */
void destroyTestLogger() {
    INFO("=== daemon_test 测试结束 ===");
}

// -------------------------- 各功能测试函数 --------------------------
/**
 * @brief 测试守护进程启动功能
 */
void test_start() {
    DEBUG("=== 开始测试 start 功能 ===");
    cleanTestEnv();

    // Daemon::process("start", TEST_PID_FILE);
    // 创建子进程来执行启动操作，避免守护进程影响主测试进程
    pid_t pid = fork();
    if (pid < 0) {
        ERROR("fork failed: %s", strerror(errno));
        return;
    } else if (pid == 0) {
        // 子进程：执行start命令
        Daemon::process("start", TEST_PID_FILE);
    }

    // 父进程等待子进程完成启动
    int status;
    waitpid(pid, &status, 0);
    
    // 等待一小段时间确保守护进程启动完成
    sleep(1);

    // 检查PID文件是否创建
    int daemonPid = Daemon::getPidFromFile(TEST_PID_FILE);
    bool pidFileCreated = (daemonPid > 0);
    // 检查进程是否存活
    bool processAlive = (pidFileCreated && kill(daemonPid, 0) == 0);

    DEBUG("启动后PID文件检查：%s（PID=%d）", 
          pidFileCreated ? "存在" : "不存在", daemonPid);
    DEBUG("守护进程存活检查：%s", processAlive ? "存活" : "未存活");

    // 清理：停止守护进程
    if (processAlive) {
        Daemon::process("stop", TEST_PID_FILE);
    }

    bool testOk = pidFileCreated && processAlive;
    DEBUG("start功能测试：%s", testOk ? "通过" : "失败");
    DEBUG("=== start 功能测试结束 ===\n");
}

/**
 * @brief 测试守护进程停止功能
 */
void test_stop() {
    DEBUG("=== 开始测试 stop 功能 ===");
    cleanTestEnv();

    // 先启动守护进程
    pid_t startPid = fork();
    if (startPid == 0) {
        Daemon::process("start", TEST_PID_FILE);
    }
    waitpid(startPid, nullptr, 0);
    int daemonPid = Daemon::getPidFromFile(TEST_PID_FILE);
    if (daemonPid <= 0) {
        ERROR("启动守护进程失败，无法测试stop功能");
        return;
    }
    DEBUG("启动的守护进程PID：%d", daemonPid);

    // 执行停止操作
    pid_t stopPid = fork();
    if (stopPid < 0) {
        ERROR("fork failed: %s", strerror(errno));
        return;
    } else if (stopPid == 0) {
        Daemon::process("stop", TEST_PID_FILE);
    }
    waitpid(stopPid, nullptr, 0);

    // 检查结果
    bool pidFileRemoved = (access(TEST_PID_FILE, F_OK) != 0);
    bool processStopped = (kill(daemonPid, 0) != 0 && errno == ESRCH);

    DEBUG("停止后PID文件检查：%s", pidFileRemoved ? "已删除" : "未删除");
    DEBUG("守护进程停止检查：%s", processStopped ? "已停止" : "未停止");

    bool testOk = pidFileRemoved && processStopped;
    DEBUG("stop功能测试：%s", testOk ? "通过" : "失败");
    DEBUG("=== stop 功能测试结束 ===\n");
}

/**
 * @brief 测试守护进程重启功能
 */
void test_restart() {
    DEBUG("=== 开始测试 restart 功能 ===");
    cleanTestEnv();

    // 先启动初始守护进程
    pid_t startPid = fork();
    if (startPid == 0) {
        Daemon::process("start", TEST_PID_FILE);
    }
    waitpid(startPid, nullptr, 0);
    int oldPid = Daemon::getPidFromFile(TEST_PID_FILE);
    if (oldPid <= 0) {
        ERROR("启动初始守护进程失败，无法测试restart功能");
        return;
    }
    DEBUG("初始守护进程PID：%d", oldPid);

    // 执行重启操作
    pid_t restartPid = fork();
    if (restartPid < 0) {
        ERROR("fork failed: %s", strerror(errno));
        return;
    } else if (restartPid == 0) {
        Daemon::process("restart", TEST_PID_FILE);
    }
    waitpid(restartPid, nullptr, 0);

    // 检查结果
    int newPid = Daemon::getPidFromFile(TEST_PID_FILE);
    bool pidChanged = (newPid > 0 && newPid != oldPid);
    bool oldProcessStopped = (kill(oldPid, 0) != 0 && errno == ESRCH);
    bool newProcessAlive = (pidChanged && kill(newPid, 0) == 0);

    DEBUG("重启后新PID：%d（旧PID：%d）", newPid, oldPid);
    DEBUG("旧进程停止检查：%s", oldProcessStopped ? "已停止" : "未停止");
    DEBUG("新进程存活检查：%s", newProcessAlive ? "存活" : "未存活");

    // 清理
    if (newProcessAlive) {
        Daemon::stop(TEST_PID_FILE);
    }

    bool testOk = pidChanged && oldProcessStopped && newProcessAlive;
    DEBUG("restart功能测试：%s", testOk ? "通过" : "失败");
    DEBUG("=== restart 功能测试结束 ===\n");
}

/**
 * @brief 测试getPidFromFile函数
 */
void test_get_pid_from_file() {
    DEBUG("=== 开始测试 getPidFromFile 函数 ===");
    cleanTestEnv();

    // 测试1：文件不存在
    int pid1 = Daemon::getPidFromFile(TEST_PID_FILE);
    bool test1Ok = (pid1 == -1);
    DEBUG("测试1（文件不存在）：返回值=%d（预期-1，%s）", pid1, test1Ok ? "通过" : "失败");

    // 测试2：创建合法PID文件
    int fd = open(TEST_PID_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd >= 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d\n", getpid());
        write(fd, buf, strlen(buf));
        close(fd);

        int pid2 = Daemon::getPidFromFile(TEST_PID_FILE);
        bool test2Ok = (pid2 == getpid());
        DEBUG("测试2（合法PID文件）：返回值=%d（预期%d，%s）", 
              pid2, getpid(), test2Ok ? "通过" : "失败");
    } else {
        ERROR("创建测试PID文件失败，跳过测试2");
    }

    // 测试3：非法内容的PID文件
    fd = open(TEST_PID_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd >= 0) {
        write(fd, "invalid_pid", 11);
        close(fd);

        int pid3 = Daemon::getPidFromFile(TEST_PID_FILE);
        bool test3Ok = (pid3 == -1);
        DEBUG("测试3（非法内容）：返回值=%d（预期-1，%s）", pid3, test3Ok ? "通过" : "失败");
    } else {
        ERROR("创建测试PID文件失败，跳过测试3");
    }

    cleanTestEnv();
    DEBUG("=== getPidFromFile 函数测试结束 ===\n");
}

/**
 * @brief 测试信号处理功能
 */
void test_signal_handler() {
    DEBUG("=== 开始测试 signal 处理功能 ===");
    cleanTestEnv();

    // 启动带信号处理的守护进程
    pid_t pid = fork();
    if (pid < 0) {
        ERROR("fork failed: %s", strerror(errno));
        return;
    } else if (pid == 0) {
        // 注册信号处理函数
        Signal::signal(SIGUSR1, []() {
            g_signalReceived = true;
            // 接收到信号后退出
            Daemon::stop(TEST_PID_FILE);
        });
        // 启动守护进程
        Daemon::process("start", TEST_PID_FILE);
        
        // 守护进程循环等待信号
        while (!g_signalReceived) {
            usleep(100000); // 100ms
        }
        _exit(0);
    }

    // 等待守护进程启动
    sleep(1);
    int daemonPid = Daemon::getPidFromFile(TEST_PID_FILE);
    if (daemonPid <= 0) {
        ERROR("启动守护进程失败，无法测试信号处理");
        waitpid(pid, nullptr, 0);
        return;
    }
    DEBUG("向守护进程（PID=%d）发送SIGUSR1信号", daemonPid);

    // 发送测试信号
    kill(daemonPid, SIGUSR1);
    
    // 等待进程退出
    sleep(1);
    bool processStopped = (kill(daemonPid, 0) != 0 && errno == ESRCH);
    bool pidFileRemoved = (access(TEST_PID_FILE, F_OK) != 0);

    DEBUG("信号处理后进程状态：%s", processStopped ? "已退出" : "仍运行");
    DEBUG("信号处理后PID文件：%s", pidFileRemoved ? "已删除" : "未删除");

    bool testOk = processStopped && pidFileRemoved;
    DEBUG("signal处理功能测试：%s", testOk ? "通过" : "失败");
    DEBUG("=== signal 处理功能测试结束 ===\n");
}

/**
 * @brief 测试重复启动防护功能
 */
void test_start_protection() {
    DEBUG("=== 开始测试 重复启动防护 功能 ===");
    cleanTestEnv();

    // 第一次启动
    pid_t pid1 = fork();
    if (pid1 == 0) {
        Daemon::process("start", TEST_PID_FILE);
        _exit(0);
    }
    waitpid(pid1, nullptr, 0);
    int daemonPid = Daemon::getPidFromFile(TEST_PID_FILE);
    if (daemonPid <= 0) {
        ERROR("第一次启动失败，无法测试重复启动防护");
        return;
    }

    // 尝试第二次启动
    pid_t pid2 = fork();
    if (pid2 == 0) {
        // 重定向输出到临时文件以便检查
        int fd = open("daemon_test_dup.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        dup2(fd, STDERR_FILENO);
        Daemon::process("start", TEST_PID_FILE);
        _exit(0);
    }
    int status;
    waitpid(pid2, &status, 0);
    bool secondStartFailed = (WEXITSTATUS(status) == 1);

    // 清理
    Daemon::stop(TEST_PID_FILE);
    unlink("daemon_test_dup.log");

    DEBUG("重复启动返回值检查：%s", secondStartFailed ? "失败（符合预期）" : "成功（不符合预期）");

    bool testOk = secondStartFailed;
    DEBUG("重复启动防护功能测试：%s", testOk ? "通过" : "失败");
    DEBUG("=== 重复启动防护 功能测试结束 ===\n");
}

// -------------------------- 测试入口函数 --------------------------
void run_all_tests() {
    // 1. 初始化日志和环境
    initTestLogger();
    cleanTestEnv();

    // 2. 依次执行所有测试
    test_get_pid_from_file();
    test_start();
    test_stop();
    test_restart();
    test_start_protection();
    test_signal_handler();

    // 3. 最终清理和日志关闭
    cleanTestEnv();
    destroyTestLogger();
}

}  // namespace daemonTest
}  // namespace handy

// 主函数：启动测试
int main() {
    handy::daemonTest::run_all_tests();
    return 0;
}