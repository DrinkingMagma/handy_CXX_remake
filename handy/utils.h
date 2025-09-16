#pragma once
#include <string>
#include <cstring>
#include <functional>

namespace handy
{
    /**
     * @brief 禁止派生类对象的拷贝构造和拷贝赋值操作
     * @note 用于需要确保资源独占性或者避免不必要复制开销的场景，如单例模式、文件句柄管理等场景
    */
    class NonCopyAble
        {
            protected:
                NonCopyAble() = default;
                ~NonCopyAble() noexcept = default;
                NonCopyAble(const NonCopyAble&) = delete;
                NonCopyAble& operator=(const NonCopyAble&) = delete;

                // 允许移动操作（根据实际需求添加
                NonCopyAble(NonCopyAble&&) = default;
                NonCopyAble& operator=(NonCopyAble&&) = default;
        };

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
         * @brief 将时间戳转换为可读字符串
         * @param t 时间戳
         * @return 格式为"YYYY-MM-DD HH:MM:SS"的字符串
         * @note 线程安全，使用线程局部存储的tm结构
         */
        static std::string readableTime(time_t t) noexcept;

        /**
         * @brief 将字符串区间转换为整数
         * @param b 字符串起始地址
         * @param e 字符串结束地址(不包含)
         * @return 转换后的整数，失败返回0
         */
        static int64_t atoi(const char* b, const char* e) noexcept;

        /**
         * @brief 将字符串区间转换为整数并验证范围
         * @param b 字符串起始地址
         * @param e 字符串结束地址(不包含)
         * @return 转换后的整数，若未完全消耗输入则返回-1
         */
        static int64_t atoi2(const char* b, const char* e) noexcept;

        /**
         * @brief 将字符串转换为整数
         * @param b 以null结尾的字符串
         * @return 转换后的整数
         */
        static int64_t atoi(const char* b) noexcept { return atoi(b, b + std::strlen(b)); }

        /**
         * @brief 为文件描述符添加标志
         * @param fd 文件描述符
         * @param flag 要添加的标志
         * @return 成功返回0，失败返回-1并设置errno
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
