#include "../handy/thread_pool.h"
#include "logger.h"
#include <cstdio>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <sstream>

// 测试用全局原子变量（多线程任务计数、同步）
std::atomic<int> g_taskExecCount(0);
std::atomic<bool> g_taskFlag(false);
std::atomic<int> g_multiProducerCount(0);
std::atomic<int> g_multiConsumerCount(0);

namespace handy {
namespace threadsTest {

// -------------------------- 测试辅助函数 --------------------------
/**
 * @brief 初始化测试日志（输出到threads_test.log）
 */
void initTestLogger() {
    Logger::getInstance().setLogFileName("threads_test.log");
    Logger::getInstance().setLogLevel(Logger::LogLevel::LDEBUG);
    INFO("=== threads_test 测试开始 ===");
}

/**
 * @brief 测试结束清理日志
 */
void destroyTestLogger() {
    INFO("=== threads_test 测试结束 ===");
}

/**
 * @brief 重置全局测试变量（避免测试间干扰）
 */
void resetGlobalVars() {
    g_taskExecCount.store(0, std::memory_order_relaxed);
    g_taskFlag.store(false, std::memory_order_relaxed);
    g_multiProducerCount.store(0, std::memory_order_relaxed);
    g_multiConsumerCount.store(0, std::memory_order_relaxed);
}

/**
 * @brief 测试用基础任务（原子计数+状态标记）
 */
void baseTestTask() {
    g_taskExecCount.fetch_add(1, std::memory_order_relaxed);
    g_taskFlag.store(true, std::memory_order_relaxed);
    DEBUG("基础任务执行：当前计数=%d", g_taskExecCount.load());
}

/**
 * @brief 测试用带参数任务（通过lambda捕获传递参数）
 * @param num 任务编号
 * @param delayMs 任务执行延迟（毫秒）
 */
void paramTestTask(int num, int delayMs) {
    std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    g_taskExecCount.fetch_add(1, std::memory_order_relaxed);
    DEBUG("带参数任务执行：编号=%d，延迟=%dms，当前计数=%d", 
          num, delayMs, g_taskExecCount.load());
}

// -------------------------- SafeQueue 测试函数 --------------------------
/**
 * @brief 测试 SafeQueue 基础功能（push/popWait/size/exit）
 */
void testSafeQueueBasic() {
    DEBUG("=== 开始测试 SafeQueue 基础功能 ===");
    resetGlobalVars();

    // 1. 测试无容量限制队列
    SafeQueue<Task> queueUnlimited;
    bool test1Ok = true;

    // 推送3个任务
    for (int i = 0; i < 3; ++i) {
        if (!queueUnlimited.push(Task(baseTestTask))) {
            test1Ok = false;
            DEBUG("无容量队列push任务%d失败", i);
        }
    }

    // 验证队列大小
    if (queueUnlimited.size() != 3) {
        test1Ok = false;
        DEBUG("无容量队列size错误：实际=%zu，预期=3", queueUnlimited.size());
    }

    // 取出3个任务并执行
    Task task;
    for (int i = 0; i < 3; ++i) {
        if (!queueUnlimited.popWait(&task)) {
            test1Ok = false;
            DEBUG("无容量队列pop任务%d失败", i);
            continue;
        }
        task(); // 执行任务
    }

    // 验证任务执行计数
    if (g_taskExecCount.load() != 3) {
        test1Ok = false;
        DEBUG("无容量队列任务执行计数错误：实际=%d，预期=3", g_taskExecCount.load());
    }

    DEBUG("测试1（无容量限制队列）：%s", test1Ok ? "通过" : "失败");

    // 2. 测试有容量限制队列（容量=2）
    SafeQueue<Task> queueLimited(2);
    bool test2Ok = true;

    // 推送2个任务（应成功）
    for (int i = 0; i < 2; ++i) {
        if (!queueLimited.push(Task(baseTestTask))) {
            test2Ok = false;
            DEBUG("有容量队列push任务%d失败（预期成功）", i);
        }
    }

    // 推送第3个任务（应失败，队列满）
    if (queueLimited.push(Task(baseTestTask))) {
        test2Ok = false;
        DEBUG("有容量队列push第3个任务成功（预期失败）");
    }

    // 验证队列大小
    if (queueLimited.size() != 2) {
        test2Ok = false;
        DEBUG("有容量队列size错误：实际=%zu，预期=2", queueLimited.size());
    }

    // 取出1个任务，释放容量
    if (queueLimited.popWait(&task)) {
        task();
    } else {
        test2Ok = false;
        DEBUG("有容量队列pop任务失败");
    }

    // 再次推送任务（应成功）
    if (!queueLimited.push(Task(baseTestTask))) {
        test2Ok = false;
        DEBUG("有容量队列释放后push任务失败");
    }

    DEBUG("测试2（有容量限制队列）：%s", test2Ok ? "通过" : "失败");

    // 3. 测试队列exit功能
    SafeQueue<Task> queueExit;
    bool test3Ok = true;

    // 退出队列后push（应失败）
    queueExit.exit();
    if (queueExit.push(Task(baseTestTask))) {
        test3Ok = false;
        DEBUG("退出后队列push成功（预期失败）");
    }

    // 退出后pop（应失败，队列空）
    if (queueExit.popWait(&task)) {
        test3Ok = false;
        DEBUG("退出后空队列pop成功（预期失败）");
    }

    // 验证isExited状态
    if (!queueExit.isExited()) {
        test3Ok = false;
        DEBUG("队列exit后isExited返回false（预期true）");
    }

    DEBUG("测试3（队列exit功能）：%s", test3Ok ? "通过" : "失败");
    DEBUG("=== SafeQueue 基础功能测试结束 ===\n");
}

/**
 * @brief 测试 SafeQueue 超时等待功能（popWait超时）
 */
void testSafeQueueTimeout() {
    DEBUG("=== 开始测试 SafeQueue 超时等待功能 ===");
    resetGlobalVars();

    SafeQueue<Task> queue;
    bool testOk = true;
    Task task;
    const int timeoutMs = 500; // 超时时间500ms

    // 测试空队列超时等待（无任务，应超时返回false）
    auto start = std::chrono::steady_clock::now();
    bool popResult = queue.popWait(&task, timeoutMs);
    auto end = std::chrono::steady_clock::now();
    auto costMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    // 验证返回结果（应false）
    if (popResult) {
        testOk = false;
        DEBUG("空队列超时等待返回true（预期false）");
    }

    // 验证等待时间（应接近timeoutMs，误差允许±100ms）
    if (costMs < timeoutMs - 100 || costMs > timeoutMs + 100) {
        testOk = false;
        DEBUG("超时等待时间异常：实际=%lldms，预期≈%dms", costMs, timeoutMs);
    }

    // 测试有任务时超时（任务存在，应立即返回true）
    queue.push(Task(baseTestTask));
    start = std::chrono::steady_clock::now();
    popResult = queue.popWait(&task, timeoutMs);
    end = std::chrono::steady_clock::now();
    costMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    // 验证返回结果（应true）
    if (!popResult) {
        testOk = false;
        DEBUG("有任务时超时等待返回false（预期true）");
    } else {
        task(); // 执行任务
    }

    // 验证等待时间（应<100ms，无明显延迟）
    if (costMs > 100) {
        testOk = false;
        DEBUG("有任务时等待时间过长：实际=%lldms，预期<100ms", costMs);
    }

    DEBUG("测试（超时等待功能）：%s", testOk ? "通过" : "失败");
    DEBUG("=== SafeQueue 超时等待功能测试结束 ===\n");
}

/**
 * @brief 测试 SafeQueue 多生产者多消费者线程安全
 */
void testSafeQueueMultiThread() {
    DEBUG("=== 开始测试 SafeQueue 多生产者多消费者线程安全 ===");
    resetGlobalVars();

    const int PRODUCER_NUM = 3;  // 3个生产者线程
    const int CONSUMER_NUM = 2;  // 2个消费者线程
    const int TASK_PER_PRODUCER = 5; // 每个生产者推送5个任务
    const int TOTAL_TASK = PRODUCER_NUM * TASK_PER_PRODUCER;
    SafeQueue<Task> queue(10); // 队列容量10（避免频繁阻塞）
    std::vector<std::thread> threads;

    // 生产者线程函数：推送任务
    auto producerFunc = [&queue]() {
        for (int i = 0; i < TASK_PER_PRODUCER; ++i) {
            // 推送任务：增加生产者计数
            queue.push([&]() {
                g_multiProducerCount.fetch_add(1, std::memory_order_relaxed);
                DEBUG("多线程任务执行：生产者计数=%d", g_multiProducerCount.load());
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(50)); // 模拟生产间隔
        }
    };

    // 消费者线程函数：取出并执行任务
    auto consumerFunc = [&queue]() {
        Task task;
        while (g_multiConsumerCount.load() < TOTAL_TASK) {
            if (queue.popWait(&task, 100)) { // 100ms超时重试
                task();
                g_multiConsumerCount.fetch_add(1, std::memory_order_relaxed);
            }
        }
    };

    // 创建生产者线程
    for (int i = 0; i < PRODUCER_NUM; ++i) {
        threads.emplace_back(producerFunc);
    }

    // 创建消费者线程
    for (int i = 0; i < CONSUMER_NUM; ++i) {
        threads.emplace_back(consumerFunc);
    }

    // 等待所有线程结束
    for (auto& t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    // 验证任务执行总数
    bool testOk = (g_multiProducerCount.load() == TOTAL_TASK) && 
                  (g_multiConsumerCount.load() == TOTAL_TASK);
    DEBUG("生产者任务总数：实际=%d，预期=%d", g_multiProducerCount.load(), TOTAL_TASK);
    DEBUG("消费者任务总数：实际=%d，预期=%d", g_multiConsumerCount.load(), TOTAL_TASK);
    DEBUG("测试（多生产者多消费者线程安全）：%s", testOk ? "通过" : "失败");
    DEBUG("=== SafeQueue 多生产者多消费者线程安全测试结束 ===\n");
}

// -------------------------- ThreadPool 测试函数 --------------------------
/**
 * @brief 测试 ThreadPool 基础功能（构造/start/addTask/exit/join）
 */
void testThreadPoolBasic() {
    DEBUG("=== 开始测试 ThreadPool 基础功能 ===");
    resetGlobalVars();

    bool testOk = true;
    const int THREAD_NUM = 2;  // 2个工作线程
    const int TASK_NUM = 5;    // 5个测试任务

    // 1. 测试立即启动线程池（isStartImmediately=true）
    ThreadPool poolImmediate(THREAD_NUM, 10, true);
    DEBUG("立即启动线程池：isStarted=%d，isExited=%d", poolImmediate.isStarted(), poolImmediate.isExited());

    // 验证初始状态
    if (!poolImmediate.isStarted() || poolImmediate.isExited()) {
        testOk = false;
        DEBUG("立即启动线程池状态错误：isStarted=%d，isExited=%d", 
              poolImmediate.isStarted(), poolImmediate.isExited());
    }

    // 添加TASK_NUM个任务
    for (int i = 0; i < TASK_NUM; ++i) {
        if (!poolImmediate.addTask(Task(baseTestTask))) {
            testOk = false;
            DEBUG("立即启动线程池addTask%d失败", i);
        }
    }

    // 等待任务执行完成（最多等2秒）
    auto start = std::chrono::steady_clock::now();
    while (g_taskExecCount.load() < TASK_NUM) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto cost = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count();
        if (cost > 2) {
            testOk = false;
            DEBUG("立即启动线程池任务执行超时（2秒）");
            break;
        }
    }

    // 验证任务执行计数
    if (g_taskExecCount.load() != TASK_NUM) {
        testOk = false;
        DEBUG("立即启动线程池任务计数错误：实际=%d，预期=%d", g_taskExecCount.load(), TASK_NUM);
    }

    // 退出并等待线程池
    poolImmediate.exit();
    poolImmediate.join();
    DEBUG("立即启动线程池退出后：isExited=%d", poolImmediate.isExited());

    // 2. 测试延迟启动线程池（isStartImmediately=false）
    resetGlobalVars();
    ThreadPool poolDelay(THREAD_NUM, 10, false);
    DEBUG("延迟启动线程池：isStarted=%d，isExited=%d", poolDelay.isStarted(), poolDelay.isExited());

    // 验证初始状态（未启动）
    if (poolDelay.isStarted() || poolDelay.isExited()) {
        testOk = false;
        DEBUG("延迟启动线程池初始状态错误：isStarted=%d，isExited=%d", 
              poolDelay.isStarted(), poolDelay.isExited());
    }

    // 启动线程池
    poolDelay.start();
    if (!poolDelay.isStarted()) {
        testOk = false;
        DEBUG("延迟启动线程池start()后状态错误：isStarted=%d", poolDelay.isStarted());
    }

    // 添加TASK_NUM个任务
    for (int i = 0; i < TASK_NUM; ++i) {
        if (!poolDelay.addTask(Task(baseTestTask))) {
            testOk = false;
            DEBUG("延迟启动线程池addTask%d失败", i);
        }
    }

    // 等待任务执行完成（最多等2秒）
    start = std::chrono::steady_clock::now();
    while (g_taskExecCount.load() < TASK_NUM) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto cost = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count();
        if (cost > 2) {
            testOk = false;
            DEBUG("延迟启动线程池任务执行超时（2秒）");
            break;
        }
    }

    // 验证任务执行计数
    if (g_taskExecCount.load() != TASK_NUM) {
        testOk = false;
        DEBUG("延迟启动线程池任务计数错误：实际=%d，预期=%d", g_taskExecCount.load(), TASK_NUM);
    }

    // 退出并等待线程池
    poolDelay.exit();
    poolDelay.join();

    DEBUG("测试（线程池基础功能）：%s", testOk ? "通过" : "失败");
    DEBUG("=== ThreadPool 基础功能测试结束 ===\n");
}

/**
 * @brief 测试 ThreadPool 异常处理（非法参数、状态错误）
 */
void testThreadPoolExceptions() {
    DEBUG("=== 开始测试 ThreadPool 异常处理 ===");
    resetGlobalVars();

    bool testOk = true;

    // 1. 测试构造函数异常（线程数≤0）
    bool except1Caught = false;
    try {
        ThreadPool pool(0, 10); // 线程数0（非法）
    } catch (const std::invalid_argument& e) {
        except1Caught = true;
        DEBUG("构造函数异常1捕获成功：%s", e.what());
    } catch (...) {
        testOk = false;
        DEBUG("构造函数捕获到未知异常（预期invalid_argument）");
    }
    if (!except1Caught) {
        testOk = false;
        DEBUG("构造函数未捕获线程数≤0的异常");
    }

    // 2. 测试启动已退出线程池异常
    bool except2Caught = false;
    try {
        ThreadPool pool(2, 10);
        pool.exit();
        pool.start(); // 已退出线程池不能启动
    } catch (const std::logic_error& e) {
        except2Caught = true;
        DEBUG("start()异常捕获成功：%s", e.what());
    } catch (...) {
        testOk = false;
        DEBUG("start()捕获到未知异常（预期logic_error）");
    }
    if (!except2Caught) {
        testOk = false;
        DEBUG("start()未捕获已退出线程池的异常");
    }

    // 3. 测试未启动线程池添加任务异常
    bool except3Caught = false;
    try {
        ThreadPool pool(2, 10, false); // 未启动
        pool.addTask(Task(baseTestTask)); // 未启动不能添加任务
    } catch (const std::logic_error& e) {
        except3Caught = true;
        DEBUG("addTask()异常捕获成功：%s", e.what());
    } catch (...) {
        testOk = false;
        DEBUG("addTask()捕获到未知异常（预期logic_error）");
    }
    if (!except3Caught) {
        testOk = false;
        DEBUG("addTask()未捕获未启动线程池的异常");
    }

    // 4. 测试join()时机错误异常
    bool except4Caught = false;
    try {
        ThreadPool pool(2, 10);
        pool.join(); // 未退出不能join
    } catch (const std::logic_error& e) {
        except4Caught = true;
        DEBUG("join()异常捕获成功：%s", e.what());
    } catch (...) {
        testOk = false;
        DEBUG("join()捕获到未知异常（预期logic_error）");
    }
    if (!except4Caught) {
        testOk = false;
        DEBUG("join()未捕获未退出线程池的异常");
    }

    DEBUG("测试（线程池异常处理）：%s", testOk ? "通过" : "失败");
    DEBUG("=== ThreadPool 异常处理测试结束 ===\n");
}

/**
 * @brief 测试 ThreadPool 多线程任务执行与容量控制
 */
void testThreadPoolMultiTask() {
    DEBUG("=== 开始测试 ThreadPool 多线程任务执行与容量控制 ===");
    resetGlobalVars();

    const int THREAD_NUM = 3;    // 3个工作线程
    const int TASK_NUM = 10;     // 10个测试任务
    const int QUEUE_CAPACITY = 5; // 队列容量5
    ThreadPool pool(THREAD_NUM, QUEUE_CAPACITY);

    // 添加TASK_NUM个带延迟的任务（验证并发执行）
    for (int i = 0; i < TASK_NUM; ++i) {
        // 每个任务延迟100ms，模拟计算耗时
        pool.addTask(std::bind(paramTestTask, i, 100));
    }

    // 验证等待任务数量（队列应先满后逐渐减少）
    std::this_thread::sleep_for(std::chrono::milliseconds(300)); // 等待部分任务执行
    size_t waitingCount = pool.getWaitingTaskCount();
    DEBUG("任务执行中等待数量：%zu（应小于等于%d）", waitingCount, QUEUE_CAPACITY);

    // 等待所有任务执行完成（最多等3秒）
    auto start = std::chrono::steady_clock::now();
    while (g_taskExecCount.load() < TASK_NUM) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto cost = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count();
        if (cost > 3) {
            DEBUG("多任务执行超时（3秒），当前计数=%d", g_taskExecCount.load());
            break;
        }
    }

    // 验证任务执行总数
    bool testOk = (g_taskExecCount.load() == TASK_NUM);
    DEBUG("多任务执行总数：实际=%d，预期=%d", g_taskExecCount.load(), TASK_NUM);

    // 测试队列容量控制（添加超过容量的任务）
    resetGlobalVars();
    const int OVERFLOW_NUM = QUEUE_CAPACITY + 2; // 超过容量2个
    int successCount = 0;
    for (int i = 0; i < OVERFLOW_NUM; ++i) {
        if (pool.addTask(Task(baseTestTask))) {
            successCount++;
        }
    }
    // 成功添加的任务数应≤容量（工作线程可能已执行部分任务，所以用≤）
    if (successCount > QUEUE_CAPACITY) {
        testOk = false;
        DEBUG("队列容量控制失败：成功添加=%d，容量=%d", successCount, QUEUE_CAPACITY);
    }
    DEBUG("超过容量添加任务：成功=%d，容量=%d", successCount, QUEUE_CAPACITY);

    // 清理线程池
    pool.exit();
    pool.join();

    DEBUG("测试（多线程任务执行与容量控制）：%s", testOk ? "通过" : "失败");
    DEBUG("=== ThreadPool 多线程任务执行与容量控制测试结束 ===\n");
}

/**
 * @brief 测试 ThreadPool 任务异常处理（任务抛出异常不影响线程池）
 */
void testThreadPoolTaskException() {
    DEBUG("=== 开始测试 ThreadPool 任务异常处理 ===");
    resetGlobalVars();

    const int THREAD_NUM = 2;
    const int NORMAL_TASK_NUM = 3;  // 正常任务数量
    const int EXCEPT_TASK_NUM = 2;  // 异常任务数量
    ThreadPool pool(THREAD_NUM);

    // 添加正常任务
    for (int i = 0; i < NORMAL_TASK_NUM; ++i) {
        pool.addTask(Task(baseTestTask));
    }

    // 添加会抛出异常的任务
    for (int i = 0; i < EXCEPT_TASK_NUM; ++i) {
        pool.addTask([]() {
            throw std::runtime_error("测试任务主动抛出异常");
        });
    }

    // 等待所有任务执行（最多等2秒）
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // 验证正常任务执行计数（异常任务不影响正常任务）
    bool testOk = (g_taskExecCount.load() == NORMAL_TASK_NUM);
    DEBUG("异常处理测试：正常任务执行数=%d，预期=%d", g_taskExecCount.load(), NORMAL_TASK_NUM);

    // 验证线程池仍可接受新任务（异常任务未导致线程退出）
    resetGlobalVars();
    pool.addTask(Task(baseTestTask));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    if (g_taskExecCount.load() != 1) {
        testOk = false;
        DEBUG("异常任务导致线程退出：新任务执行数=%d，预期=1", g_taskExecCount.load());
    }

    // 清理线程池
    pool.exit();
    pool.join();

    DEBUG("测试（任务异常处理）：%s", testOk ? "通过" : "失败");
    DEBUG("=== ThreadPool 任务异常处理测试结束 ===\n");
}

// -------------------------- 测试入口函数 --------------------------
void runAllTests() {
    // 1. 初始化日志
    initTestLogger();

    // 2. 执行SafeQueue测试
    testSafeQueueBasic();
    testSafeQueueTimeout();
    testSafeQueueMultiThread();

    // 3. 执行ThreadPool测试
    testThreadPoolBasic();
    testThreadPoolExceptions();
    testThreadPoolMultiTask();
    testThreadPoolTaskException();

    // 4. 清理日志
    destroyTestLogger();
}

}  // namespace threadsTest
}  // namespace handy

// 主函数：启动测试
int main() {
    handy::threadsTest::runAllTests();
    return 0;
}
