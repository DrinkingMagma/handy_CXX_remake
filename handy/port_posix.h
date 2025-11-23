/**
 * @file port_posix.h
 * @brief 跨平台端口工具函数头文件，提供字节序转换、主机名解析和线程ID获取等功能
 * @details 
 *  该文件定义了一组跨平台的工具函数，主要包含以下核心功能：
 *  1. 字节序转换：提供主机字节序与大端字节序（网络字节序）之间的转换，支持16/32/64位有符号和无符号整数；
 *  2. 主机名解析：通过getHostByName函数将主机名（域名或IP字符串）解析为IPv4地址结构（in_addr），线程安全；
 *  3. 线程ID获取：getCurrentThreadId函数返回当前线程的64位唯一标识，适配不同操作系统；
 *  4. IPv4地址与字符串转换：addrToString将in_addr结构转换为点分十进制字符串，stringToAddr则反之。
 * @note 
 *  1. 字节序转换依赖系统宏__BYTE_ORDER判断是否为小端字节序，仅在小端系统上执行字节交换；
 *  2. 主机名解析和线程ID获取的具体实现依赖平台，需配合对应的.cpp文件（如port_posix.cpp）使用；
 *  3. 所有函数均针对POSIX兼容系统设计，非POSIX系统可能需要额外适配；
 *  4. 线程安全说明：getHostByName通过互斥锁保证线程安全，其他函数为无状态设计，可并发调用。
 */

#pragma once
#include <netinet/in.h>
#include <string>
#include <cstdint>
#include <endian.h>

namespace handy
{
    namespace port
    {
        /**
         * @brief 表示当前系统是否为小端字节序
         * @details 非零值表示系统采用小端字节序，零值表示大端字节序
        */
        static const int kLittleEndian = (__BYTE_ORDER == __LITTLE_ENDIAN);

        /**
         * @brief 将16位无符号整数从主机字节序转换为大端字节序（网络字节序）
         * @param v 主机字节序的16位无符号整数
         * @return 大端字节序的16位无符号整数
        */
        inline uint16_t htobe(uint16_t v)
        {
            if(!kLittleEndian)
                return v;
            // 交换高低字节
            return (uint16_t)((v << 8) | (v >> 8));
        }

        /**
         * @brief 将32位无符号整数从主机字节序转换为大端字节序（网络字节序）
         * @param v 主机字节序的32位无符号整数
         * @return 大端字节序的32位无符号整数
        */
        inline uint32_t htobe(uint32_t v)
        {
            if(!kLittleEndian)
                return v;
            // 交换字节顺序
            return ((v << 24) & 0xff000000) | ((v << 8) & 0x00ff0000) |
                   ((v >> 8) & 0x0000ff00) | ((v >> 24) & 0x000000ff);
        }

        /**
         * @brief 将64位无符号整数从主机字节序转换为大端字节序（网络字节序）
         * @param v 主机字节序的64位无符号整数
         * @return 大端字节序的64位无符号整数
        */
        inline uint64_t htobe(uint64_t v)
        {
            if(!kLittleEndian)
                return v;
            // 交换字节顺序
            return ((v << 56) & 0xff00000000000000ULL) | ((v << 40) & 0x00ff000000000000ULL) |
                   ((v << 24) & 0x0000ff0000000000ULL) | ((v << 8) & 0x000000ff00000000ULL) |
                   ((v >> 8) & 0x00000000ff000000ULL) | ((v >> 24) & 0x0000000000ff0000ULL) |
                   ((v >> 40) & 0x000000000000ff00ULL) | ((v >> 56) & 0x00000000000000ffULL);
        }

        /**
         * @brief 将16位有符号整数从主机字节序转换为大端字节序（网络字节序）
         * @param v 主机字节序的16位有符号整数
         * @return 大端字节序的16位有符号整数
        */
        inline int16_t htobe(int16_t v)
        {
            return static_cast<int16_t>(htobe(static_cast<uint16_t>(v)));
        }

        /**
         * @brief 将32位有符号整数从主机字节序转换为大端字节序（网络字节序）
         * @param v 主机字节序的32位有符号整数
         * @return 大端字节序的32位有符号整数
        */
        inline int32_t htobe(int32_t v)
        {
            return static_cast<int32_t>(htobe(static_cast<uint32_t>(v)));
        }

        /**
         * @brief 将64位有符号整数从主机字节序转换为大端字节序（网络字节序）
         * @param v 主机字节序的64位有符号整数
         * @return 大端字节序的64位有符号整数
        */
        inline int64_t htobe(int64_t v)
        {
            return static_cast<int64_t>(htobe(static_cast<uint64_t>(v)));
        }

        /**
         * @brief 将16位无符号整数从大端字节序（网络字节序）转换为主机字节序
         * @param v 大端字节序的16位无符号整数
         * @return 主机字节序的16位无符号整数
        */
        inline uint16_t betoh(uint16_t v)
        { 
            // 字节序的转换是双向操作
            return htobe(v);
        }

        /**
         * @brief 将32位无符号整数从大端字节序（网络字节序）转换为主机字节序
         * @param v 大端字节序的32位无符号整数
         * @return 主机字节序的32位无符号整数
        */
        inline uint32_t betoh(uint32_t v)
        { 
            // 字节序的转换是双向操作
            return htobe(v);
        }

        /**
         * @brief 将64位无符号整数从大端字节序（网络字节序）转换为主机字节序
         * @param v 大端字节序的64位无符号整数
         * @return 主机字节序的64位无符号整数
        */
        inline uint64_t betoh(uint64_t v)
        { 
            // 字节序的转换是双向操作
            return htobe(v);
        }

        /**
         * @brief 将16位有符号整数从大端字节序（网络字节序）转换为主机字节序
         * @param v 大端字节序的16位有符号整数
         * @return 主机字节序的16位有符号整数
        */
        inline int16_t betoh(int16_t v)
        { 
            // 字节序的转换是双向操作
            return static_cast<int16_t>(betoh(static_cast<uint16_t>(v)));
        }

        /**
         * @brief 将32位有符号整数从大端字节序（网络字节序）转换为主机字节序
         * @param v 大端字节序的32位有符号整数
         * @return 主机字节序的32位有符号整数
        */
        inline int32_t betoh(int32_t v)
        { 
            // 字节序的转换是双向操作
            return static_cast<int32_t>(betoh(static_cast<uint32_t>(v)));
        }

        /**
         * @brief 将64位有符号整数从大端字节序（网络字节序）转换为主机字节序
         * @param v 大端字节序的64位有符号整数
         * @return 主机字节序的64位有符号整数
        */
        inline int64_t betoh(int64_t v)
        { 
            // 字节序的转换是双向操作
            return static_cast<int64_t>(betoh(static_cast<uint64_t>(v)));
        }

        /**
         * @brief 通过主机名获取对应的IPv4地址
         * @param host 主机名（域名或IP字符串）
         * @param[out] result 存储解析得到的IPv4地址结构
         * @return 成功返回true，失败返回false
         * @details 线程安全的主机名解析函数，支持IPv4地址解析
        */
        bool getHostByName(const std::string& host, struct in_addr& result);

        /**
         * @brief 获取当前线程的唯一标识
         * @return 当前线程ID的64位整数表示
         * @details 提供跨平台的线程ID获取功能，适用于日志记录和调试
        */
        uint64_t getCurrentThreadId();

        /**
         * @brief 将Ipv4地址转换为字符串
         * @param addr 指向IPv4地址结构的指针
         * @return IPV4地址对应的字符串表示（如：192.168.1.1）
        */
        std::string addrToString(const struct in_addr* addr);

        /**
         * @brief 将字符串形式的IPv4地址转换为in_addr结构
         * @param ip IPv4地址字符串
         * @param[out] addr 存储转换结果的IPv4地址结构
         * @return 成功返回true，失败返回false
        */
        bool stringToAddr(const std::string& ip, struct in_addr* addr);
    } // namespace port
} // namespace handy