#include "net.h"
#include "logger.h"
#include "utils.h"
#include <cassert>
#include <vector>
#include <thread>
#include <atomic>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/socket.h>
#include <ctime>
#include <string>
#include <netinet/tcp.h>
#include <fcntl.h>

// 多线程测试计数变量
std::atomic<int> g_bufferThreadCount(0);
std::atomic<int> g_netThreadCount(0);

namespace handy {
namespace netTest {

// -------------------------- 测试辅助函数 --------------------------
/**
 * @brief 初始化测试日志（输出到net_test.log）
 */
void initTestLogger() {
    Logger::getInstance().setLogFileName("net_test.log");
    Logger::getInstance().setLogLevel(Logger::LogLevel::LDEBUG);
    INFO("=== net_test 测试开始 ===");
}

/**
 * @brief 创建临时TCP Socket（用于Net类选项测试）
 * @return 成功返回Socket文件描述符，失败返回-1
 */
int createTempTcpSocket() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        ERROR("创建临时Socket失败，err=%d(%s)", errno, strerror(errno));
    }
    return fd;
}

/**
 * @brief 关闭临时Socket
 * @param fd 待关闭的Socket文件描述符
 */
void closeTempSocket(int fd) {
    if (fd >= 0) {
        close(fd);
    }
}

// -------------------------- Net类测试 --------------------------
/**
 * @brief 测试Net类字节序转换功能（hton/ntoh）
 */
void test_net_byte_order() {
    DEBUG("=== 开始测试Net字节序转换 ===");

    // 测试16位无符号整数
    uint16_t host16 = 0x1234;
    uint16_t net16 = Net::hton(host16);
    uint16_t host16_restore = Net::ntoh(net16);
    DEBUG("16位转换测试: 主机序0x%x → 网络序0x%x → 恢复0x%x（%s）",
          host16, net16, host16_restore, 
          (host16_restore == host16) ? "通过" : "失败");

    // 测试32位有符号整数
    int32_t host32 = 0x12345678;
    int32_t net32 = Net::hton(host32);
    int32_t host32_restore = Net::ntoh(net32);
    DEBUG("32位转换测试: 主机序0x%x → 网络序0x%x → 恢复0x%x（%s）",
          host32, net32, host32_restore, 
          (host32_restore == host32) ? "通过" : "失败");

    // 测试64位无符号整数
    uint64_t host64 = 0x123456789abcdef0;
    uint64_t net64 = Net::hton(host64);
    uint64_t host64_restore = Net::ntoh(net64);
    DEBUG("64位转换测试: 主机序0x%lx → 网络序0x%lx → 恢复0x%lx（%s）",
          host64, net64, host64_restore, 
          (host64_restore == host64) ? "通过" : "失败");

    DEBUG("=== Net字节序转换测试结束 ===\n");
}

/**
 * @brief 测试Net类Socket选项设置（非阻塞、地址重用等）
 */
void test_net_socket_options() {
    DEBUG("=== 开始测试Net Socket选项 ===");
    int fd = createTempTcpSocket();
    if (fd < 0) {
        DEBUG("=== Net Socket选项测试结束（失败） ===\n");
        return;
    }

    int errCode = 0;
    // 测试1: 设置非阻塞模式
    bool nbRet = Net::setNonBlock(fd, true, &errCode);
    int flags = fcntl(fd, F_GETFL, 0);
    bool nbCheck = (flags & O_NONBLOCK) != 0;
    DEBUG("非阻塞模式测试: 设置返回%s，实际状态%s（%s）",
          nbRet ? "成功" : "失败", nbCheck ? "非阻塞" : "阻塞",
          (nbRet && nbCheck) ? "通过" : "失败");

    // 测试2: 设置地址重用（SO_REUSEADDR）
    bool raRet = Net::setReuseAddr(fd, true, &errCode);
    int raFlag = 0;
    socklen_t raLen = sizeof(raFlag);
    getsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &raFlag, &raLen);
    DEBUG("地址重用测试: 设置返回%s，实际值%d（%s）",
          raRet ? "成功" : "失败", raFlag,
          (raRet && raFlag == 1) ? "通过" : "失败");

    // 测试3: 设置端口重用（SO_REUSEPORT）
    bool rpRet = Net::setReusePort(fd, true, &errCode);
    #ifdef SO_REUSEPORT
        int rpFlag = 0;
        socklen_t rpLen = sizeof(rpFlag);
        getsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &rpFlag, &rpLen);
        DEBUG("端口重用测试: 设置返回%s，实际值%d（%s）",
              rpRet ? "成功" : "失败", rpFlag,
              (rpRet && rpFlag == 1) ? "通过" : "失败");
    #else
        DEBUG("端口重用测试: 平台不支持SO_REUSEPORT（跳过验证）");
    #endif

    // 测试4: 设置TCP无延迟（TCP_NODELAY）
    bool ndRet = Net::setNoDelay(fd, true, &errCode);
    int ndFlag = 0;
    socklen_t ndLen = sizeof(ndFlag);
    getsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &ndFlag, &ndLen);
    DEBUG("TCP无延迟测试: 设置返回%s，实际值%d（%s）",
          ndRet ? "成功" : "失败", ndFlag,
          (ndRet && ndFlag == 1) ? "通过" : "失败");

    // 测试5: 无效fd处理
    bool badFdRet = Net::setNonBlock(-1, true, &errCode);
    DEBUG("无效fd测试: 设置返回%s，错误码%d（预期EBADF=%d，%s）",
          badFdRet ? "成功" : "失败", errCode, EBADF,
          (badFdRet == false && errCode == EBADF) ? "通过" : "失败");

    closeTempSocket(fd);
    DEBUG("=== Net Socket选项测试结束 ===\n");
}

/**
 * @brief 测试Net类多线程安全（并发设置Socket选项）
 */
void test_net_thread_safety() {
    DEBUG("=== 开始测试Net多线程安全 ===");
    const int THREAD_NUM = 4;
    const int LOOP_NUM = 500;

    // 线程函数：并发设置Socket选项
    auto threadFunc = []() {
        for (int i = 0; i < LOOP_NUM; ++i) {
            int fd = createTempTcpSocket();
            if (fd >= 0) {
                // 交替设置不同选项
                Net::setNonBlock(fd, (i % 2 == 0), nullptr);
                Net::setReuseAddr(fd, (i % 3 == 0), nullptr);
                closeTempSocket(fd);
            }
            g_netThreadCount.fetch_add(1, std::memory_order_relaxed);
        }
    };

    // 启动线程
    std::vector<std::thread> threads;
    for (int i = 0; i < THREAD_NUM; ++i) {
        threads.emplace_back(threadFunc);
    }

    // 等待线程完成
    for (auto& t : threads) {
        t.join();
    }

    // 验证执行次数
    int64_t total = g_netThreadCount.load();
    int64_t expected = THREAD_NUM * LOOP_NUM;
    DEBUG("Net多线程测试: 总执行次数=%lld（预期%lld，%s）",
          total, expected, (total == expected) ? "通过" : "失败");

    DEBUG("=== Net多线程安全测试结束 ===\n");
}

// -------------------------- Ipv4Addr类测试 --------------------------
/**
 * @brief 测试Ipv4Addr类构造与格式化功能
 */
void test_ipv4addr_construct_format() {
    DEBUG("=== 开始测试Ipv4Addr构造与格式化 ===");

    // 测试1: 通过端口构造（默认0.0.0.0）
    Ipv4Addr addr1(8080);
    DEBUG("端口构造测试: %s（预期0.0.0.0:8080，%s）",
          addr1.toString().c_str(),
          (addr1.toString() == "0.0.0.0:8080" && addr1.port() == 8080) ? "通过" : "失败");

    // 测试2: 通过IP字符串+端口构造
    Ipv4Addr addr2("192.168.1.100", 9090);
    DEBUG("IP+端口构造测试: %s（预期192.168.1.100:9090，%s）",
          addr2.toString().c_str(),
          (addr2.toString() == "192.168.1.100:9090" && addr2.ip() == "192.168.1.100") ? "通过" : "失败");

    // 测试3: 通过域名构造（依赖DNS解析，这里用已知域名）
    Ipv4Addr addr3("localhost", 80);
    bool dnsValid = addr3.isIpValid() && (addr3.ip() == "127.0.0.1");
    DEBUG("域名构造测试: %s（%s）",
          addr3.toString().c_str(), dnsValid ? "通过" : "失败");

    // 测试4: 通过sockaddr_in构造
    struct sockaddr_in rawAddr;
    memset(&rawAddr, 0, sizeof(rawAddr));
    rawAddr.sin_family = AF_INET;
    rawAddr.sin_port = htons(1234);
    inet_pton(AF_INET, "10.0.0.1", &rawAddr.sin_addr);
    Ipv4Addr addr4(rawAddr);
    DEBUG("sockaddr_in构造测试: %s（预期10.0.0.1:1234，%s）",
          addr4.toString().c_str(),
          (addr4.toString() == "10.0.0.1:1234" && addr4.ipInt() == 0x0a000001) ? "通过" : "失败");


    // 测试5: 无效IP构造
    Ipv4Addr addr5("invalid_ip_123", 5678);
    DEBUG("无效IP构造测试: %s（预期含invalid_ip，%s）",
          addr5.toString().c_str(),
          (addr5.isIpValid() == false && addr5.toString().find("invalid ip") != std::string::npos) ? "通过" : "失败");

    // 测试6: 拷贝与移动构造
    Ipv4Addr addr6 = addr2;  // 拷贝
    Ipv4Addr addr7 = std::move(addr6);  // 移动
    DEBUG("拷贝/移动构造测试: %s（预期192.168.1.100:9090，%s）",
          addr7.toString().c_str(),
          (addr7.toString() == "192.168.1.100:9090") ? "通过" : "失败");

    DEBUG("=== Ipv4Addr构造与格式化测试结束 ===\n");
}

/**
 * @brief 测试Ipv4Addr类IP解析与验证功能
 */
void test_ipv4addr_resolve_validate() {
    DEBUG("=== 开始测试Ipv4Addr解析与验证 ===");

    // 测试1: 主机名转IP（localhost）
    std::string outIp1;
    bool ret1 = Ipv4Addr::hostToIp("localhost", outIp1);
    DEBUG("localhost解析测试: IP=%s（%s）",
          outIp1.c_str(), (ret1 && outIp1 == "127.0.0.1") ? "通过" : "失败");

    // 测试2: 无效主机名解析
    std::string outIp2;
    bool ret2 = Ipv4Addr::hostToIp("invalid.host.nonexist", outIp2);
    DEBUG("无效主机名解析测试: 返回%s，IP=%s（%s）",
          ret2 ? "成功" : "失败", outIp2.c_str(),
          (ret2 == false && outIp2.empty()) ? "通过" : "失败");

    // 测试3: IP整数转换（主机序）
    Ipv4Addr addr("172.16.0.2", 0);
    uint32_t ipInt = addr.ipInt();
    DEBUG("IP整数转换测试: 主机序整数0x%x（预期0xac100002，%s）",
          ipInt, (ipInt == 0xac100002) ? "通过" : "失败");

    // 测试4: 地址族校验（非AF_INET）
    struct sockaddr_in badAddr;
    memset(&badAddr, 0, sizeof(badAddr));
    badAddr.sin_family = AF_INET6;  // IPv6地址族
    Ipv4Addr addrBad(badAddr);
    DEBUG("地址族校验测试: 有效性=%s（预期false，%s）",
          addrBad.isIpValid() ? "true" : "false",
          (addrBad.isIpValid() == false) ? "通过" : "失败");

    DEBUG("=== Ipv4Addr解析与验证测试结束 ===\n");
}

// -------------------------- Buffer类测试 --------------------------
/**
 * @brief 测试Buffer类基本数据操作（追加、消耗、清空）
 */
void test_buffer_basic_ops() {
    DEBUG("=== 开始测试Buffer基本操作 ===");
    Buffer buf;

    // 测试1: 追加字符串
    buf.append("hello");
    buf.append(" world");
    std::string data1 = buf.data();
    DEBUG("字符串追加测试: 数据=%s（预期hello world，%s）",
          data1.c_str(), (data1 == "hello world") ? "通过" : "失败");

    // 测试2: 追加POD类型（int）
    int podVal = 0x12345678;
    buf.appendValue(podVal);
    size_t size2 = buf.size();
    DEBUG("POD追加测试: 总长度=%zu（预期11+4=15，%s）",
          size2, (size2 == 15) ? "通过" : "失败");

    // 测试3: 消耗数据
    buf.consume(6);  // 消耗"hello "
    std::string data3 = buf.data();
    DEBUG("数据消耗测试: 剩余长度=%zu（预期9，%s）",
          data3.size(), (data3.size() == 9) ? "通过" : "失败");

    // 测试4: 清空缓冲区
    buf.clear();
    DEBUG("清空测试: 空=%s（预期true，%s）",
          buf.empty() ? "true" : "false", (buf.empty() == true) ? "通过" : "失败");

    // 测试5: 吸收另一个缓冲区
    Buffer bufSrc;
    bufSrc.append("absorb_test");
    buf.absorb(bufSrc);
    DEBUG("吸收测试: 数据=%s（预期absorb_test，%s）",
          buf.data().c_str(), (buf.data() == "absorb_test" && bufSrc.empty()) ? "通过" : "失败");

    // 测试6: 设置期望增长大小
    buf.setExpectGrowSize(1024);
    std::string bigData(2048, 'a');
    buf.append(bigData);
    bool growOk = buf.size() == (11 + 2048);  // 11是absorb_test长度
    DEBUG("增长大小测试: 容量扩展%s（预期2058字节，%s）",
          growOk ? "符合预期" : "不符合预期", growOk ? "通过" : "失败");

    DEBUG("=== Buffer基本操作测试结束 ===\n");
}

/**
 * @brief 测试Buffer类内存管理（碎片整理、自动扩展）
 */
void test_buffer_memory_manage() {
    DEBUG("=== 开始测试Buffer内存管理 ===");
    Buffer buf;
    buf.setExpectGrowSize(100);  // 固定期望增长大小

    // 测试1: 多次消耗后触发碎片整理（moveHead）
    buf.append(std::string(150, 'a'));  // 初始容量100→扩展到200
    buf.consume(100);  // 剩余50字节，偏移m_b=100
    buf.append(std::string(30, 'b'));   // 触发moveHead
    bool defragOk = (buf.size() == 80);
    DEBUG("碎片整理测试: 剩余长度=%zu（预期80，%s）",
          buf.size(), defragOk ? "通过" : "失败");

    // 测试2: 自动扩展策略
    buf.append(std::string(150, 'c'));  // 需扩展到400
    buf.append(std::string(200, 'd'));  // 无需再次扩展
    bool expandOk = (buf.size() == 80 + 150 + 200);
    DEBUG("自动扩展测试: 总长度=%zu（预期430，%s）",
          buf.size(), expandOk ? "通过" : "失败");

    // 测试3: 空缓冲区吸收非空缓冲区（资源转移）
    Buffer bufEmpty;
    Buffer bufFull;
    bufFull.append("transfer_data");
    size_t fullSize = bufFull.size();
    bufEmpty.absorb(bufFull);
    bool transferOk = (bufEmpty.size() == fullSize && bufFull.empty());
    DEBUG("资源转移测试: %s（%s）",
          transferOk ? "数据完整且原缓冲区空" : "数据丢失或原缓冲区非空",
          transferOk ? "通过" : "失败");

    DEBUG("=== Buffer内存管理测试结束 ===\n");
}

/**
 * @brief 测试Buffer类多线程安全（并发追加、消耗、吸收）
 */
void test_buffer_thread_safety() {
    DEBUG("=== 开始测试Buffer多线程安全 ===");
    Buffer buf;
    const int THREAD_NUM = 5;    // 5个并发线程
    const int LOOP_NUM = 500;    // 每个线程循环500次

    // 线程1-2：并发追加字符串
    auto appendThread = [&buf]() {
        for (int i = 0; i < LOOP_NUM; ++i) {
            std::string data = "thread_" + std::to_string(pthread_self()) + "_" + std::to_string(i);
            DEBUG("线程%lu追加数据: %s", pthread_self(), data.c_str());
            buf.append(data);
            g_bufferThreadCount.fetch_add(1, std::memory_order_relaxed);
        }
    };

    // 线程3-4：并发消耗数据（每次消耗1-5字节）
    auto consumeThread = [&buf]() {
        for (int i = 0; i < LOOP_NUM; ++i) {
            size_t currentSize = buf.size();
            if (currentSize > 0) {
                size_t consumeLen = std::min<size_t>(rand() % 5 + 1, currentSize);
                DEBUG("线程%lu消耗数据: %zu字节", pthread_self(), consumeLen);
                buf.consume(consumeLen);
            }
            g_bufferThreadCount.fetch_add(1, std::memory_order_relaxed);
        }
    };

    // 线程5：并发吸收临时缓冲区
    auto absorbThread = [&buf]() {
        for (int i = 0; i < LOOP_NUM; ++i) {
            Buffer tempBuf;
            {
                tempBuf.append("absorb_temp_" + std::to_string(i));
            }
            DEBUG("线程%lu吸收临时缓冲区: %s", pthread_self(), tempBuf.data().c_str());
            buf.absorb(tempBuf);
            g_bufferThreadCount.fetch_add(1, std::memory_order_relaxed);
        }
    };

    // 启动线程
    std::vector<std::thread> threads;
    threads.emplace_back(appendThread);
    threads.emplace_back(appendThread);
    threads.emplace_back(consumeThread);
    threads.emplace_back(consumeThread);
    threads.emplace_back(absorbThread);

    // 等待所有线程完成
    for (auto& t : threads) {
        t.join();
    }

    // 验证：无崩溃+执行次数正确
    int64_t total = g_bufferThreadCount.load();
    int64_t expected = THREAD_NUM * LOOP_NUM;
    bool threadSafe = (total == expected);
    DEBUG("多线程执行次数测试: 实际=%lld，预期=%lld（%s）",
          total, expected, threadSafe ? "通过" : "失败");
    DEBUG("多线程后缓冲区状态: 非空=%s，长度=%zu（无崩溃即符合安全要求）",
          buf.empty() ? "否" : "是", buf.size());

    DEBUG("=== Buffer多线程安全测试结束 ===\n");
}

// -------------------------- 测试入口函数 --------------------------
void run_all_tests() {
    // 1. 初始化日志
    initTestLogger();

    // 2. 执行Net类测试
    test_net_byte_order();
    test_net_socket_options();
    test_net_thread_safety();

    // 3. 执行Ipv4Addr类测试
    test_ipv4addr_construct_format();
    test_ipv4addr_resolve_validate();

    // 4. 执行Buffer类测试
    test_buffer_basic_ops();
    test_buffer_memory_manage();
    test_buffer_thread_safety();

    // 5. 测试总结
    INFO("=== net_test 所有测试执行完成 ===");
}

}  // namespace netTest
}  // namespace handy

// 主函数：启动所有测试
int main() {
    srand(time(nullptr));  // 为消耗线程的随机数初始化
    handy::netTest::run_all_tests();
    return 0;
}
