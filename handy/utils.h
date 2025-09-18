#pragma once
#include "non_copy_able.h"
#include <string>
#include <cstring>
#include <functional>

namespace handy
{
    /**
     * @brief 通用工具函数集合
     * @note 提供字符串处理、时间获取、文件描述符操作等通用功能
     * @note 所有函数均为线程安全实现
    */
    struct utils
    {
        /**
         * @brief 格式化字符串
         * @param fmt 格式化字符串
         * @param ... 可变参数列表
         * @return 格式化后的std::string（空字符串表示失败）
         * @note 1. 自动调整缓冲区大小，最大限制1MB（防止内存耗尽
         * @note 2. 支持所有printf兼容的占位符（%d/%s/%f等）
         * @note 3. 线程安全，内部使用vsnprintf实现
         */
        static std::string format(const char* fmt, ...) noexcept;

        /**
         * @brief 获取当前系统时间(微秒级，线程安全)
         * @return 自Unix纪元（1970-01-01 00:00:00 UTC）以来的微秒数
         * @note 1. 线程安全，但可能受系统时间调整影响（如NTP同步），不适合高精度计时
         * @note 2. 异常时返回-1（如系统时钟不可用）
         */
        static int64_t timeMicro() noexcept;

        /**
         * @brief 获取当前系统时间(毫秒级)
         * @return 自纪元以来的毫秒数
         * @note 线程安全，但可能受系统时间调整影响
         */
        static int64_t timeMilli() noexcept { return timeMicro() / 1000; }

        /**
         * @brief 获取稳定时钟时间(微秒级，线程安全)
         * @return 自稳定时钟纪元以来的微秒数（纪元由系统决定）
         * @note 1. 不受系统时间调整影响，适合高精度计时（如性能测试、定时器）
         * @note 2. 异常时返回-1（如稳定时钟不可用）
         */
        static int64_t steadyMicro() noexcept;

        /**
         * @brief 获取稳定时钟时间(毫秒级)
         * @return 自稳定时钟纪元以来的毫秒数
         * @note 线程安全，不受系统时间调整影响，适合计时
         */
        static int64_t steadyMilli() noexcept { return steadyMicro() / 1000; }

        /**
         * @brief 将time_t时间戳转换为可读字符串（线程安全）
         * @param t 待转换的time_t时间戳（Unix纪元时间）
         * @return 格式为"YYYY-MM-DD HH:MM:SS"的字符串
         * @note 1. 错误时返回"invalid time"或"error formatting time"
         * @note 2. 使用线程局部tm结构，避免多线程竞争（localtime_r线程安全
         */
        static std::string readableTime(time_t t) noexcept;

        /**
         * @brief 将字符串区间转换为整数
         * @param b 字符串起始地址（非空）
         * @param e 字符串结束地址(不包含)（非空，且b < e）
         * @return 转换后的整数，失败返回0
         * @note 1. 忽略字符串末尾的非数字字符（如"123abc"返回123）
         * @note 2. 支持正负号（如"-456"返回-456）
         */
        static int64_t atoi(const char* b, const char* e) noexcept;

        /**
         * @brief 将字符串区间严格转换为整数（需完全匹配，线程安全）
         * @param b 字符串起始地址（非空）
         * @param e 字符串结束地址(不包含)（非空，且b < e）
         * @return 转换后的int64_t值；失败（未完全匹配）时返回-1
         * @note 1. 仅当整个区间均为数字时才返回有效结果（如"123"返回123，"123a"返回-1）
         * @note 2. 支持正负号（如"+789"返回789）
         */
        static int64_t atoi2(const char* b, const char* e) noexcept;

        /**
         * @brief 将字符串转换为整数
         * @param b 以null结尾的字符串
         * @return 转换后的整数
         */
        static int64_t atoi(const char* b) noexcept { return atoi(b, b + std::strlen(b)); }

        /**
         * @brief 为文件描述符添加标志（线程安全）
         * @param fd 目标文件描述符（需 >= 0）
         * @param flag 要添加的标志
         * @return 成功返回0，失败返回-1并设置errno
         * @note 1. 若标志已存在，直接返回0（避免重复操作）
         * @note 2. 使用fcntl的F_GETFD/F_SETFD操作，保证原子性
         */
        static int addFdFlag(int fd, int flag) noexcept;
    };

    /**
     * @brief 程序退出时执行指定函数
     * @note 利用RAII机制，在对象析构时执行注册的函数
     * @note 通常用于资源释放、日志关闭等清理操作
    */
    class ExitCaller : private NonCopyAble
    {
        public: 
            /**
             * @brief 构造函数，注册退出时执行的函数
             * @param functor 要执行的函数对象
             * @note explicit: 禁止隐式转换，必须显示调用构造函数
            */
            explicit ExitCaller(std::function<void()>&& functor) : m_functor(std::move(functor)) {}

            /**
             * @brief 析构函数，执行注册的函数
            */
            ~ExitCaller() noexcept
            {
                try
                {
                    if(m_functor)
                        m_functor();
                }
                catch(...)
                {
                    // 析构函数中不允许抛出异常
                }
            }
        private:
            std::function<void()> m_functor;
    };
} // namespace handy
