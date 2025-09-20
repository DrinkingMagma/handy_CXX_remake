/**
 * @file port_posix.cpp
 * @brief 跨平台端口工具函数的POSIX实现(Linux/macOS)
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

// 平台宏定义，自动识别并定义对应操作系统宏
// Linux 平台检测
#if defined(__linux__) || defined(linux) || defined(LINUX)
    #if !defined(OS_LINUX)
        #define OS_LINUX 1
    #endif
#endif

// macOS 平台检测（__APPLE__ 和 __MACH__ 是苹果系统的标准预定义宏）
#if defined(__APPLE__) && defined(__MACH__)
    #if !defined(OS_MACOSX)
        #define OS_MACOSX 1
    #endif
#endif

// 可以继续添加其他平台，如 FreeBSD
#if defined(__FreeBSD__)
    #if !defined(OS_FREEBSD)
        #define OS_FREEBSD 1
    #endif
#endif

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