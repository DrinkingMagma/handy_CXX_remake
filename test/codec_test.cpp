#include "codec.h"
#include "net.h"
#include "logger.h"
#include <vector>
#include <thread>
#include <atomic>
#include <cassert>
#include <stdexcept>
#include <string>
#include <iostream>

// 测试用全局原子变量（用于多线程测试计数）
std::atomic<int> g_codecTestCount(0);

namespace handy {
namespace codecTest {

// -------------------------- 测试辅助函数 --------------------------
/**
 * @brief 初始化Logger（输出到codec_test.log）
 */
void initTestLogger() {
    Logger::getInstance().setLogFileName("codec_test.log");
    Logger::getInstance().setLogLevel(Logger::LogLevel::LDEBUG);
    INFO("=== codec_test 测试开始 ===");
}

// -------------------------- LineCodec 单元测试 --------------------------
/**
 * @brief 测试LineCodec基本编码解码功能
 */
void test_LineCodec_basic() {
    DEBUG("=== 开始LineCodec基本功能测试 ===");
    LineCodec codec;
    Buffer buf;
    Slice msg;

    // 测试1：编码普通消息（带\r\n）
    std::string testStr = "hello_line_codec";
    codec.encode(testStr, buf);
    bool encodeOk = (buf.data() == testStr + "\r\n");
    DEBUG("LineCodec编码测试1: 输入=%s → 输出=%s（%s）",
          testStr.c_str(), buf.data().c_str(), encodeOk ? "通过" : "失败");

    // 测试2：解码完整消息（带\r\n）
    size_t decodeLen = codec.tryDecode(Slice(buf.peek()), msg);
    bool decodeOk1 = (decodeLen == testStr.size() + 2) && (msg.toString() == testStr);
    DEBUG("LineCodec解码测试1: 输入=%s → 解析=%s（长度=%d，%s）",
          buf.data().c_str(), msg.toString().c_str(), decodeLen, decodeOk1 ? "通过" : "失败");

    // 测试3：解码带\n的消息
    buf.clear();
    buf.append("test_without_cr\n");
    decodeLen = codec.tryDecode(Slice(buf.peek()), msg);
    bool decodeOk2 = (decodeLen == 15) && (msg.toString() == "test_without_cr");
    if(decodeLen != 15)
        DEBUG("length is not same, decodeLen = %d", decodeLen);
    if(msg.toString() != "test_without_cr")
        DEBUG("msg is not same, msg = %s", msg.toString().c_str());
    DEBUG("LineCodec解码测试2: 输入=test_without_cr\\n → 解析=%s（%s）",
          msg.toString().c_str(), decodeOk2 ? "通过" : "失败");

    // 测试4：解码不完整消息（无换行符）
    buf.clear();
    buf.append("incomplete_message");
    decodeLen = codec.tryDecode(Slice(buf.data()), msg);
    bool decodeOk3 = (decodeLen == 0);  // 应返回0表示不完整
    DEBUG("LineCodec解码测试3: 不完整消息 → 返回值=%d（%s）",
          decodeLen, decodeOk3 ? "通过" : "失败");

    // 测试5：EOT结束符测试（0x04）
    buf.clear();
    buf.appendValue(static_cast<char>(0x04));  // 单独EOT
    decodeLen = codec.tryDecode(Slice(buf.data()), msg);
    bool decodeOk4 = (decodeLen == 1) && (msg.size() == 1) && (static_cast<uint8_t>(msg[0]) == 0x04);
    DEBUG("LineCodec EOT测试: 单独EOT → 解析长度=%d（%s）",
          decodeLen, decodeOk4 ? "通过" : "失败");

    // 测试6：EOT与其他数据混合（非法场景）
    buf.clear();
    buf.append("data").appendValue(static_cast<char>(0x04));
    decodeLen = codec.tryDecode(Slice(buf.data()), msg);
    bool decodeOk5 = (decodeLen == 0);  // 应无法解析
    DEBUG("LineCodec非法EOT测试: EOT混合数据 → 返回值=%d（%s）",
          decodeLen, decodeOk5 ? "通过" : "失败");

    // 测试7：编码含\n的消息（应抛出异常）
    bool exceptionThrown = false;
    try {
        codec.encode("invalid\nmessage", buf);
    } catch (const std::invalid_argument& e) {
        exceptionThrown = true;
    }
    DEBUG("LineCodec异常测试: 编码含\\n的消息 → %s（%s）",
          exceptionThrown ? "捕获预期异常" : "未捕获异常", exceptionThrown ? "通过" : "失败");

    DEBUG("=== LineCodec基本功能测试结束 ===\n");
}

// -------------------------- LengthCodec 单元测试 --------------------------
/**
 * @brief 测试LengthCodec基本编码解码功能
 */
void test_LengthCodec_basic() {
    DEBUG("=== 开始LengthCodec基本功能测试 ===");
    LengthCodec codec;
    Buffer buf;
    Slice msg;

    // 测试1：编码普通消息
    std::string testStr = "hello_length_codec";
    codec.encode(Slice(testStr), buf);
    
    // 验证头部+数据格式：mBdT(4) + 长度(4) + 数据
    bool headerOk = (memcmp(buf.peek(), LengthCodec::kMagic, 4) == 0);
    int32_t netLen = 0;
    memcpy(&netLen, buf.peek() + 4, sizeof(netLen));
    int32_t hostLen = Net::ntoh(netLen);
    bool lenOk = (hostLen == static_cast<int32_t>(testStr.size()));
    bool dataOk = (std::string(buf.peek() + 8, testStr.size()) == testStr);
    bool encodeOk = headerOk && lenOk && dataOk;
    DEBUG("LengthCodec编码测试: 输入长度=%zu → 头部验证=%s，长度验证=%s（%s）",
          testStr.size(), headerOk ? "通过" : "失败", lenOk ? "通过" : "失败", encodeOk ? "通过" : "失败");

    // 测试2：解码完整消息
    int decodeLen = codec.tryDecode(Slice(buf.peek()), msg);
    size_t totalLen = 8 + testStr.size();  // 头部8字节+数据长度
    bool decodeOk1 = (decodeLen == static_cast<int>(totalLen)) && (msg.toString() == testStr);
    DEBUG("LengthCodec解码测试1: 总长度=%zu → 解析长度=%d，内容=%s（%s）",
          totalLen, decodeLen, msg.toString().c_str(), decodeOk1 ? "通过" : "失败");

    // 测试3：解码不完整消息（头部不足）
    buf.clear();
    buf.append("mBdT12");  // 仅6字节（不足8字节头部）
    decodeLen = codec.tryDecode(Slice(buf.peek()), msg);
    bool decodeOk2 = (decodeLen == 0);  // 应返回0表示不完整
    DEBUG("LengthCodec解码测试2: 头部不完整 → 返回值=%d（%s）",
          decodeLen, decodeOk2 ? "通过" : "失败");

    // 测试4：解码不完整消息（数据不足）
    buf.clear();
    codec.encode("long_message", buf);
    decodeLen = codec.tryDecode(Slice(buf.data().c_str(), 10), msg);
    bool decodeOk3 = (decodeLen == 0);  // 应返回0表示不完整
    DEBUG("LengthCodec解码测试3: 数据不完整 → 返回值=%d（%s）",
          decodeLen, decodeOk3 ? "通过" : "失败");

    // 测试5：无效魔法字（解码失败）
    buf.clear();
    buf.append("badT");  // 错误魔法字
    buf.appendValue(Net::hton(10));  // 长度
    buf.append("testdata");
    decodeLen = codec.tryDecode(Slice(buf.data()), msg);
    bool decodeOk4 = (decodeLen == static_cast<int>(LengthCodec::DecodeErr::kInvalidMagic));
    DEBUG("LengthCodec解码测试4: 无效魔法字 → 返回值=%d（%s）",
          decodeLen, decodeOk4 ? "通过" : "失败");

    // 测试6：无效长度（负数）
    buf.clear();
    buf.append(LengthCodec::kMagic);
    buf.appendValue(Net::hton(-10));  // 负数长度
    decodeLen = codec.tryDecode(Slice(buf.data()), msg);
    bool decodeOk5 = (decodeLen == static_cast<int>(LengthCodec::DecodeErr::kInvalidLength));
    DEBUG("LengthCodec解码测试5: 负数长度 → 返回值=%d（%s）",
          decodeLen, decodeOk5 ? "通过" : "失败");

    // 测试7：超过最大长度限制
    codec.setMaxMsgLen(5);  // 设置最大长度5字节
    bool exceptionThrown = false;
    try {
        codec.encode("too_long", buf);  // 7字节，超过限制
    } catch (const std::out_of_range& e) {
        exceptionThrown = true;
    }
    DEBUG("LengthCodec异常测试: 超过最大长度 → %s（%s）",
          exceptionThrown ? "捕获预期异常" : "未捕获异常", exceptionThrown ? "通过" : "失败");

    DEBUG("=== LengthCodec基本功能测试结束 ===\n");
}

// -------------------------- 编解码器克隆功能测试 --------------------------
/**
 * @brief 测试CodecBase的clone()方法（多态拷贝）
 */
void test_Codec_clone() {
    DEBUG("=== 开始编解码器克隆功能测试 ===");

    // 测试LineCodec克隆
    CodecBase* lineCodec = new LineCodec();
    CodecBase* lineClone = lineCodec->clone();
    bool lineCloneOk = (dynamic_cast<LineCodec*>(lineClone) != nullptr);
    DEBUG("LineCodec克隆测试: %s", lineCloneOk ? "克隆对象类型正确" : "克隆对象类型错误");

    // 测试LengthCodec克隆
    LengthCodec* lenCodec = new LengthCodec();
    lenCodec->setMaxMsgLen(2048);  // 设置自定义参数
    CodecBase* lenClone = lenCodec->clone();
    LengthCodec* lenCloneCast = dynamic_cast<LengthCodec*>(lenClone);
    bool lenCloneOk = (lenCloneCast != nullptr) && (lenCloneCast->getMaxMsgLen() == 2048);
    DEBUG("LengthCodec克隆测试: %s（%s）",
          lenCloneCast ? "克隆对象类型正确" : "克隆对象类型错误",
          lenCloneOk ? "参数复制正确" : "参数复制错误");

    // 清理内存
    delete lineCodec;
    delete lineClone;
    delete lenCodec;
    delete lenClone;

    DEBUG("=== 编解码器克隆功能测试结束 ===\n");
}

// -------------------------- 多线程安全测试 --------------------------
/**
 * @brief 测试LineCodec多线程安全
 */
void test_LineCodec_threadSafe() {
    DEBUG("=== 开始LineCodec线程安全测试 ===");
    LineCodec codec;
    Buffer buf;
    const int THREAD_NUM = 5;
    const int LOOP_NUM = 100;

    // 线程函数：并发编码
    auto encodeFunc = [&]() {
        for (int i = 0; i < LOOP_NUM; ++i) {
            std::string data = "thread_" + std::to_string(pthread_self()) + "_" + std::to_string(i);
            codec.encode(data, buf);
            g_codecTestCount.fetch_add(1, std::memory_order_relaxed);
        }
    };

    // 线程函数：并发解码
    auto decodeFunc = [&]() {
        for (int i = 0; i < LOOP_NUM; ++i) {
            Slice msg;
            size_t dataSize = buf.size();
            if (dataSize > 0) {
                codec.tryDecode(Slice(buf.data()), msg);
            }
            g_codecTestCount.fetch_add(1, std::memory_order_relaxed);
        }
    };

    // 启动线程（2个编码，3个解码）
    std::vector<std::thread> threads;
    for (int i = 0; i < 2; ++i) threads.emplace_back(encodeFunc);
    for (int i = 0; i < 3; ++i) threads.emplace_back(decodeFunc);

    // 等待所有线程完成
    for (auto& t : threads) t.join();

    // 验证执行次数（无崩溃即线程安全）
    int total = g_codecTestCount.load();
    int expected = THREAD_NUM * LOOP_NUM;
    DEBUG("LineCodec线程安全测试: 总操作次数=%d（预期=%d），%s",
          total, expected, total == expected ? "计数正确" : "计数异常");
    DEBUG("=== LineCodec线程安全测试结束 ===\n");
}

/**
 * @brief 测试LengthCodec多线程安全
 */
void test_LengthCodec_threadSafe() {
    DEBUG("=== 开始LengthCodec线程安全测试 ===");
    LengthCodec codec;
    Buffer buf;
    const int THREAD_NUM = 5;
    const int LOOP_NUM = 100;

    // 线程函数：并发编码
    auto encodeFunc = [&]() {
        for (int i = 0; i < LOOP_NUM; ++i) {
            std::string data = "thread_" + std::to_string(pthread_self()) + "_" + std::to_string(i);
            codec.encode(data, buf);
            g_codecTestCount.fetch_add(1, std::memory_order_relaxed);
        }
    };

    // 线程函数：并发解码和修改最大长度
    auto mixedFunc = [&]() {
        for (int i = 0; i < LOOP_NUM; ++i) {
            // 交替执行解码和修改最大长度
            if (i % 2 == 0) {
                Slice msg;
                size_t dataSize = buf.size();
                if (dataSize > 0) {
                    codec.tryDecode(Slice(buf.data()), msg);
                }
            } else {
                codec.setMaxMsgLen(1024 + (i % 512));  // 动态修改参数
            }
            g_codecTestCount.fetch_add(1, std::memory_order_relaxed);
        }
    };

    // 启动线程（2个编码，3个混合操作）
    std::vector<std::thread> threads;
    for (int i = 0; i < 2; ++i) threads.emplace_back(encodeFunc);
    for (int i = 0; i < 3; ++i) threads.emplace_back(mixedFunc);

    // 等待所有线程完成
    for (auto& t : threads) t.join();

    // 验证执行次数（无崩溃即线程安全）
    int total = g_codecTestCount.load();
    int expected = THREAD_NUM * LOOP_NUM;
    DEBUG("LengthCodec线程安全测试: 总操作次数=%d（预期=%d），%s",
          total, expected, total == expected ? "计数正确" : "计数异常");
    DEBUG("=== LengthCodec线程安全测试结束 ===\n");
}

// -------------------------- 测试入口函数 --------------------------
void run_all_tests() {
    // 1. 初始化日志
    initTestLogger();

    // 2. 执行所有测试
    test_LineCodec_basic();
    test_LengthCodec_basic();
    test_Codec_clone();
    test_LineCodec_threadSafe();
    test_LengthCodec_threadSafe();

    // 3. 测试总结
    INFO("=== codec_test 所有测试执行完成 ===");
}

}  // namespace codecTest
}  // namespace handy

// 主函数：启动测试
int main() {
    handy::codecTest::run_all_tests();
    return 0;
}
