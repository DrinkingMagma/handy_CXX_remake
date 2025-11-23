/**
 * @file port_posix.cpp
 * @brief 跨平台端口工具函数的POSIX实现(Linux/macOS)
 * @details 
 *  该文件实现了POSIX兼容系统（Linux和macOS）下的核心工具函数，主要包括：
 *  1. 线程安全的主机名解析（getHostByName）：
 *     - 优先尝试将输入直接解析为IPv4地址（inet_pton）；
 *     - 若解析失败，使用线程安全的主机名解析函数（Linux下gethostbyname_r，macOS下gethostbyname加锁）；
 *     - 成功返回true并填充in_addr结构，失败返回false且将in_addr.s_addr设为INADDR_NONE。
 *  2. 当前线程ID获取（getCurrentThreadId）：
 *     - Linux下通过syscall(SYS_gettid)获取线程ID；
 *     - macOS下通过pthread_threadid_np获取线程ID；
 *     - 返回64位无符号整数表示的线程ID。
 *  3. IPv4地址与字符串转换：
 *     - addrToString：将in_addr结构转换为点分十进制IPv4字符串（inet_ntop）；
 *     - stringToAddr：将点分十进制IPv4字符串转换为in_addr结构（inet_pton）。
 * @note 
 *  1. 线程安全：主机名解析函数通过静态互斥锁保证多线程环境下的安全性；
 *  2. 平台差异：针对Linux和macOS的底层接口差异进行适配，提供统一的上层接口；
 *  3. 错误处理：解析失败时返回明确的布尔值，便于上层判断和处理；
 *  4. 依赖：依赖POSIX标准库（netdb.h、arpa/inet.h等），非POSIX系统不支持。
 */

#include <stdexcept>
#include <netdb.h>
#include <cstring>
#include <sys/syscall.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <mutex>
#include "port_posix.h"
#include <pthread.h>
#include "platform.h"

namespace handy
{
    namespace port {
        #ifdef OS_LINUX
            /** 
             * @brief 线程安全的主机名解析实现（Linux版本）
             * @param host 主机名（域名或IP字符串）
             * @param[out] result 存储解析得到的IPv4地址结构
             * @return 成功返回true，失败返回false
            */
            bool getHostByName(const std::string& host, struct in_addr& result)
            {
                // 尝试直接解析为IPv4地址
                if(inet_pton(AF_INET, host.c_str(), &result) == 1)
                    return true;

                // 主机名解析需要线程安全处理
                static std::mutex resolver_mutex;
                std::lock_guard<std::mutex> lock(resolver_mutex);

                struct hostent hent;
                struct hostent* he = nullptr;
                char buf[1024];
                int herrno = 0;

                // 使用线程安全版本的主机名解析函数
                int ret = gethostbyname_r(host.c_str(), &hent, buf, sizeof(buf), &he, &herrno);
                if(ret != 0 || !he || he->h_addrtype != AF_INET)
                {
                    result.s_addr = INADDR_NONE;
                    return false;
                }

                // 复制解析得到的IPv4地址
                result = *reinterpret_cast<struct in_addr*>(he->h_addr);
                return true;
            }

            /**
             * @brief 获取当前线程ID（Linux版本）
             * @return 64为线程ID
            */
            uint64_t getCurrentThreadId()
            {
                return static_cast<uint64_t>(syscall(SYS_gettid));
            }
        #elif defined(OS_MACOSX)
            /**
             * @brief 线程安全的主机名解析实现（macOS版本）
             * @param host 主机名或IPv4地址字符串
             * @param[out] result 存储解析得到的IPv4地址结构
             * @return 成功返回true，失败返回false
            */
            bool getHostByName(const std::string &host, struct in_addr& result)
            {
                if(inet_pton(AF_INET, host.c_str(), &result) == 1)
                    return true;

                // macOS的gethostbyname不是线程安全的，需要加锁保护
                static std::mutex resolver_mutex;
                std::lock_guard<std::mutex> lock(resolver_mutex);

                struct hostent *he = gethostbyname(host.c_str());
                if(!he || he->h_addrtype != AF_INET)
                {
                    result.s_addr = INADDR_NONE;
                    return false;
                }

                result = *reinterpret_cast<struct in_addr*>(he->h_addr);
                return true;
            }

            /**
             * @brief 获取当前线程ID（macOS版本）
             * @return 64位的线程ID
            */
            uint64_t getCurrentThreadId()
            {
                uint64_t tid;
                pthread_threadid_np(NULL, &tid);
                return tid;
            }
        #else
        #error "Unsupported POSIX platform"
        #endif

        /**
         * @brief 将IPv4地址结构转换为字符串表示
         * @param addr 指向in_addr结构的指针
         * @return IPv4地址的字符串表示
        */
        std::string addrToString(const struct in_addr* addr)
        {
            if(!addr)
                return "";

            char buf[INET_ADDRSTRLEN];
            const char* ret = inet_ntop(AF_INET, addr, buf, sizeof(buf));
            return ret ? std::string(ret) : "";
        }

        /**
         * @brief 将IPv4地址字符串转换为in_addr结构
         * @param str_ip IPv4地址字符串
         * @param[out] addr 存储转换结果的in_addr结构
         * @return 成功返回true，失败返回false
        */
        bool stringToAddr(const std::string& ip, struct in_addr* addr)
        {
            if(ip.empty() || !addr)
                return false;

            return inet_pton(AF_INET, ip.c_str(), addr) == 1;
        }
    } // namespace port 
} // namespace handy