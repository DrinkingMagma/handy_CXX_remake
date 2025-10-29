// File_test.cpp
#include "file.h"
#include "status.h"
#include "logger.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <thread>
#include <atomic>
#include <unistd.h>
#include <sys/stat.h>
#include <algorithm>
#include <sstream>

// 测试用全局原子变量（用于多线程测试计数）
std::atomic<int> g_fileThreadTestCount(0);

namespace handy {
namespace fileTest {

// -------------------------- 测试辅助函数 --------------------------
/**
 * @brief 初始化测试日志（输出到file_test.log）
 */
void initTestLogger() {
    Logger::getInstance().setLogFileName("file_test.log");
    Logger::getInstance().setLogLevel(Logger::LogLevel::LDEBUG);
    INFO("=== file_test 测试开始 ===");
}

/**
 * @brief 清理测试生成的文件和目录
 */
void cleanTestFiles() {
    // 清理临时文件
    File::deleteFile("test_file.txt");
    File::deleteFile("test_file_tmp.txt");
    File::deleteFile("rename_src.txt");
    File::deleteFile("rename_dst.txt");
    
    // 清理临时目录
    File::deleteFile("test_dir/file_in_dir.txt");
    File::deleteDir("test_dir");
    File::deleteDir("empty_dir");

    INFO("测试文件清理完成");
}

/**
 * @brief 验证状态是否符合预期
 * @param testName 测试名称
 * @param actual 实际状态
 * @param expectedCode 预期错误码（0表示成功）
 * @return 测试是否通过
 */
bool checkStatus(const std::string& testName, const Status& actual, int expectedCode = 0) {
    bool ok = (actual.code() == expectedCode);
    if (ok) {
        DEBUG("%s: 通过", testName.c_str());
    } else {
        ERROR("%s: 失败（预期码: %d, 实际码: %d, 消息: %s）",
              testName.c_str(), expectedCode, actual.code(), actual.msg());
    }
    return ok;
}

// -------------------------- 各功能测试函数 --------------------------
/**
 * @brief 测试文件内容读写
 */
void test_file_content() {
    DEBUG("=== 开始测试文件内容读写 ===");

    const std::string filename = "test_file.txt";
    const std::string content = "Hello, File Test!\n这是一段测试文本...";
    
    // 测试写入
    Status st = File::writeContent(filename, content);
    checkStatus("writeContent 正常写入", st);

    // 测试读取
    std::string readContent;
    st = File::getContent(filename, readContent);
    bool readOk = checkStatus("getContent 正常读取", st) && (readContent == content);
    DEBUG("getContent 内容匹配: %s", readOk ? "通过" : "失败");

    // 测试不存在的文件读取
    st = File::getContent("non_existent_file.txt", readContent);
    checkStatus("getContent 读取不存在文件", st, ENOENT);

    DEBUG("=== 文件内容读写测试结束 ===\n");
}

/**
 * @brief 测试安全写入（renameSave）
 */
void test_rename_save() {
    DEBUG("=== 开始测试安全写入 ===");

    const std::string target = "test_file.txt";
    const std::string tmp = "test_file_tmp.txt";
    const std::string newContent = "这是通过renameSave写入的新内容";

    // 先写入原始内容
    File::writeContent(target, "原始内容");

    // 测试安全写入
    Status st = File::renameSave(target, tmp, newContent);
    checkStatus("renameSave 正常写入", st);

    // 验证临时文件已被删除
    bool tmpExists = File::exists(tmp);
    DEBUG("renameSave 临时文件删除: %s", !tmpExists ? "通过" : "失败");

    // 验证目标文件内容正确
    std::string readContent;
    File::getContent(target, readContent);
    bool contentOk = (readContent == newContent);
    DEBUG("renameSave 内容正确: %s", contentOk ? "通过" : "失败");

    DEBUG("=== 安全写入测试结束 ===\n");
}

/**
 * @brief 测试目录操作
 */
void test_directory_operations() {
    DEBUG("=== 开始测试目录操作 ===");

    const std::string dirname = "test_dir";
    const std::string subfile = dirname + "/file_in_dir.txt";

    // 测试创建目录
    Status st = File::createDir(dirname);
    checkStatus("createDir 正常创建", st);

    // 测试目录已存在时创建
    st = File::createDir(dirname);
    checkStatus("createDir 重复创建", st, EEXIST);
    // // 重置错误码，避免影响到下个测试用例
    // errno = 0;

    // 在目录中创建文件
    File::writeContent(subfile, "目录中的文件内容");
    bool fileInDir = File::exists(subfile);
    DEBUG("createDir 子文件创建: %s", fileInDir ? "通过" : "失败");

    // 测试获取目录子项
    std::vector<std::string> children;
    st = File::getChildren(dirname, &children);
    bool childrenOk = checkStatus("getChildren 正常获取", st) && 
                      (children.size() == 1) && (children[0] == "file_in_dir.txt");
    DEBUG("getChildren 内容正确: %s", childrenOk ? "通过" : "失败");

    // 测试删除非空目录（应失败）
    st = File::deleteDir(dirname);
    checkStatus("deleteDir 删除非空目录", st, ENOTEMPTY);

    // 删除目录中的文件
    File::deleteFile(subfile);

    // 测试删除空目录
    st = File::deleteDir(dirname);
    checkStatus("deleteDir 删除空目录", st);

    DEBUG("=== 目录操作测试结束 ===\n");
}

/**
 * @brief 测试文件属性操作（大小、重命名、存在性）
 */
void test_file_attributes() {
    DEBUG("=== 开始测试文件属性操作 ===");

    const std::string filename = "test_file.txt";
    const std::string content = "1234567890"; // 10字节
    File::writeContent(filename, content);

    // 测试文件存在性
    bool exists = File::exists(filename);
    DEBUG("exists 存在文件: %s", exists ? "通过" : "失败");

    bool notExists = !File::exists("non_existent_file.txt");
    DEBUG("exists 不存在文件: %s", notExists ? "通过" : "失败");

    // 测试文件大小
    uint64_t size;
    Status st = File::getFileSize(filename, &size);
    bool sizeOk = checkStatus("getFileSize 正常文件", st) && (size == 10);
    DEBUG("getFileSize 大小正确: %s", sizeOk ? "通过" : "失败");

    // 测试目录大小（应失败）
    File::createDir("empty_dir");
    st = File::getFileSize("empty_dir", &size);
    checkStatus("getFileSize 目录类型", st, EINVAL);

    // 测试重命名
    const std::string src = "rename_src.txt";
    const std::string dst = "rename_dst.txt";
    File::writeContent(src, "重命名测试文件");
    st = File::rename(src, dst);
    bool renameOk = checkStatus("rename 正常重命名", st) && 
                    !File::exists(src) && File::exists(dst);
    DEBUG("rename 结果正确: %s", renameOk ? "通过" : "失败");

    DEBUG("=== 文件属性操作测试结束 ===\n");
}

/**
 * @brief 测试错误处理（空指针等无效参数）
 */
void test_error_handling() {
    DEBUG("=== 开始测试错误处理 ===");

    // 测试getChildren空指针
    Status st = File::getChildren("test_dir", nullptr);
    checkStatus("getChildren 空指针参数", st, EINVAL);

    // 测试getFileSize空指针
    st = File::getFileSize("test_file.txt", nullptr);
    checkStatus("getFileSize 空指针参数", st, EINVAL);

    // 测试删除不存在的文件
    st = File::deleteFile("non_existent_delete.txt");
    checkStatus("deleteFile 不存在文件", st, ENOENT);

    // 测试删除不存在的目录
    st = File::deleteDir("non_existent_dir");
    checkStatus("deleteDir 不存在目录", st, ENOENT);

    DEBUG("=== 错误处理测试结束 ===\n");
}

/**
 * @brief 多线程测试（并发读写文件）
 */
void test_thread_safe() {
    DEBUG("=== 开始测试文件操作线程安全 ===");

    const int THREAD_NUM = 4;    // 4个测试线程
    const int LOOP_NUM = 50;     // 每个线程循环50次
    const std::string baseFile = "thread_test_file_";
    std::vector<std::thread> threads;

    // 线程函数：并发创建、写入、读取文件
    auto thread_func = [&](int threadId) {
        for (int i = 0; i < LOOP_NUM; ++i) {
            // 每个线程操作不同的文件避免竞争
            std::string filename = baseFile + std::to_string(threadId) + "_" + std::to_string(i);
            std::string content = "线程" + std::to_string(threadId) + "循环" + std::to_string(i) + "的内容";

            // 写入文件
            Status st = File::writeContent(filename, content);
            if (st.ok()) {
                // 读取文件验证
                std::string readContent;
                st = File::getContent(filename, readContent);
                if (st.ok() && readContent == content) {
                    g_fileThreadTestCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
            // 清理临时文件
            File::deleteFile(filename);
        }
    };

    // 创建线程
    for (int i = 0; i < THREAD_NUM; ++i) {
        threads.emplace_back(thread_func, i);
    }

    // 等待所有线程结束
    for (auto& t : threads) {
        t.join();
    }

    // 验证总成功次数
    int64_t total = g_fileThreadTestCount.load();
    int64_t expected = THREAD_NUM * LOOP_NUM;
    bool threadOk = (total == expected);
    DEBUG("线程安全测试：总成功次数=%lld（预期：%lld，%s）",
          total, expected, threadOk ? "通过" : "失败");

    DEBUG("=== 文件操作线程安全测试结束 ===\n");
}

// -------------------------- 测试入口函数 --------------------------
void run_all_tests() {
    // 1. 初始化日志和清理环境
    initTestLogger();
    cleanTestFiles();

    // 2. 依次执行所有测试
    test_file_content();
    test_rename_save();
    test_directory_operations();
    test_file_attributes();
    test_error_handling();
    test_thread_safe();

    // 3. 最终清理
    cleanTestFiles();
    INFO("=== file_test 测试结束 ===\n");
}

}  // namespace fileTest
}  // namespace handy

// 主函数：启动测试
int main() {
    handy::fileTest::run_all_tests();
    return 0;
}