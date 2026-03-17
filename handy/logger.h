/**
 * @file logger.h
 * @brief 日志系统核心头文件，提供线程安全的日志记录功能和便捷的日志宏定义
 * @details 
 *  该文件定义了Logger类和一系列日志宏，支持多级别日志记录、日志轮转、线程安全写入等功能，核心特性如下：
 *  1. 日志级别管理：定义了LFATAL、LERROR、LWARN、LINFO、LDEBUG、LTRACE、LALL七个级别，可动态调整日志级别，过滤低级别日志；
 *  2. 便捷日志宏：提供HLOG基础宏和TRACE、DEBUG、INFO、WARN、ERROR、FATAL等简化宏，支持文件、行号、函数名自动记录，调试模式下还会进行日志格式字符串语法检查；
 *  3. 条件日志宏：提供FATAL_IF、CHECK、EXIT_IF等条件宏，满足特定条件时自动记录日志并执行终止程序或退出操作；
 *  4. 日志配置接口：支持设置日志输出文件（setLogFileName）、日志级别（setLogLevel）、日志轮转间隔（setLogRotateInterval）、日志文件大小限制（setMaxLogFileSize）等；
 *  5. 线程安全设计：使用std::mutex保护日志写入和配置修改操作，std::atomic确保日志级别读取的原子性，支持多线程并发调用；
 *  6. 日志轮转机制：支持按时间间隔（默认1天）和文件大小（默认10MB）触发日志轮转，自动备份旧日志文件（命名格式：原文件名.时间戳）；
 * @note 
 *  1. 单例模式：Logger类采用单例模式设计，通过getInstance()获取唯一实例，禁止拷贝和实例化；
 *  2. 编译模式差异：调试模式（未定义NDEBUG）下，日志宏会进行格式字符串语法检查，发布模式下则关闭该检查以提升性能；
 *  3. 日志输出默认：默认输出到标准输出（stdout），若设置日志文件失败，会自动回退到标准输出；
 *  4. 轮转配置限制：日志轮转间隔最小为1小时，避免过于频繁的轮转；
 *  5. 线程安全范围：所有日志记录和配置修改操作均为线程安全，但日志宏中的条件判断（如level <= getLogLevel()）是非原子的，若在多线程环境下同时修改日志级别和记录日志，可能会出现少量低级别日志漏记或误记，不影响程序正确性。
 */

#pragma once
#include "non_copy_able.h"
#include <string>
#include <mutex>
#include <cstdio>
#include <atomic>


#ifdef __GNUC__  // GCC/Clang编译器
#define LOG_FORMAT_ATTR(fmt_pos, va_pos) __attribute__((format(printf, fmt_pos, va_pos)))
#else
#define LOG_FORMAT_ATTR(fmt_pos, va_pos)  // 空宏，兼容其他编译器
#endif

/// 日志宏定义：根据编译模式（调试/发布）提供不同的日志处理逻辑
#ifdef NDEBUG
// 若当前为发布模式（Release）
// 只有当当前日志级别小于等于日志系统设置的级别时，才进行日志记录 
#define HLOG(level, fmt, ...)                                                               \
    do {                                                                                    \
        if(level <= handy::Logger::getInstance().getLogLevel())                             \
        {                                                                                   \
            handy::Logger::getInstance().logv(level, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__);      \
        }                                                                                   \
    } while (0)
#else
/**
 *  若当前为调试模式（Debug）
 *  用日志格式字符串初始化一个静态常量，让编译器在编译器检查日志格式字符串的语法有效性
 *  static const char* checkFmt = fmt: 仅用格式字符串初始化，不含变量
 *  避免编译器警告"未使用的变量checkFmt"，将其强制转换为void，表示已使用
*/
#define HLOG(level, fmt, ...)                                                                    \
    do {                                                                                    \
        if(level <= handy::Logger::getInstance().getLogLevel())                             \
        {                                                                                   \
            static const char* checkFmt = fmt;                                              \
            (void)checkFmt;                                                                 \
            handy::Logger::getInstance().logv(level, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__);      \
        }                                                                                   \
    } while (0)
#endif

/// 简化日志宏：直接传递 level + fmt + 可变参数（与 HLOG 格式匹配）
#define TRACE(fmt, ...) HLOG(handy::Logger::LTRACE, fmt, ##__VA_ARGS__)
#define DEBUG(fmt, ...) HLOG(handy::Logger::LDEBUG, fmt, ##__VA_ARGS__)
#define INFO(fmt, ...) HLOG(handy::Logger::LINFO, fmt, ##__VA_ARGS__)
#define WARN(fmt, ...) HLOG(handy::Logger::LWARN, fmt, ##__VA_ARGS__)
#define ERROR(fmt, ...) HLOG(handy::Logger::LERROR, fmt, ##__VA_ARGS__)
/// std::abort()：终止程序运行
#define FATAL(fmt, ...) do { HLOG(handy::Logger::LFATAL, fmt, ##__VA_ARGS__); std::abort(); } while (0)

/// 条件日志宏：在特定条件满足时自动记录并执行相应的操作
#define  FATAL_IF(b, ...)                                   \
    do {                                                    \
        if((b)) {                                           \
            HLOG(handy::Logger::LFATAL, __VA_ARGS__);       \
            std::abort();                                   \
        }                                                   \
    } while(0)
    
#define  CHECK(b, ...)                                      \
    do {                                                    \
        if(!(b)) {                                          \
            HLOG(handy::Logger::LFATAL, "Check failed: " __VA_ARGS__);       \
            std::abort();                                   \
        }                                                   \
    } while(0)

#define  EXIT_IF(b, ...)                                    \
    do {                                                    \
        if((b)) {                                           \
            HLOG(handy::Logger::LERROR, __VA_ARGS__);       \
            std::exit(1);                                   \
        }                                                   \
    } while(0)

/// 日志级别宏
#define SET_LOG_LEVEL(l) handy::Logger::getInstance().setLogLevel(l)
/// 文件设置宏
#define SET_LOG_FILE(f) handy::Logger::getInstance().setLogFileName(f)

namespace handy
{
    class Logger : private NonCopyAble
    {
        public:
            /// 日志级别枚举
            enum LogLevel
            {
                LFATAL = 0,
                LERROR,
                LWARN,
                LINFO,
                LDEBUG,
                LTRACE,
                LALL
            };

            // 获取日志类的单例实例
            static Logger& getInstance()
            {
                static Logger instance;
                return instance;
            }

            // 析构函数
            ~Logger () noexcept
            {
                closeLogFile();
            }

            /**
             * @brief 日志记录函数
             * @param level 日志级别
             * @param file 日志文件名
             * @param line 日志行号
             * @param func 日志函数名
             * @param fmt 日志格式化字符串
             * @note 1. 仅当日志级别小于等于当前日志系统级别时，记录日志
             *       2. __attribute__((format(printf, 6, 7)))： 让GCC/Clang编译器在编译时检查格式化字符串和参数是否匹配
            */
            ///使用宏定义，兼顾跨平台
            LOG_FORMAT_ATTR(6, 7)
            void logv(int level, const char* file, int line, const char* func, const char* fmt, ...);

            /// 设置日志文件名称
            void setLogFileName(const std::string& logFileName);

            /// 通过字符串形式（全小写）设置日志级别
            void setLogLevel(const std::string& logLevel);

            /// 通过LogLevel枚举形式设置日志级别
            void setLogLevel(LogLevel logLevel)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_level = std::min(LALL, std::max(LFATAL, logLevel));
            }

            /// 获取日志级别
            LogLevel getLogLevel() const
            {
                return m_level.load(std::memory_order_relaxed);
            }

            /// 获取日志级别对应的字符串表示
            const char* getLogLevelString(LogLevel logLevel) const 
            {
                if(logLevel < LFATAL || logLevel > LALL)
                {
                    return "UNKNOWN LOG LEVEL";
                }

                return m_levelStrings[logLevel];
            }

            /// 调整日志级别
            void adjustLogLevel(int adjust)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                LogLevel newLogLevel = static_cast<LogLevel>(m_level + adjust);
                m_level = std::min(LALL, std::max(LFATAL, newLogLevel));
            }

            /** 
             * 设置日志文件的轮转间隔（秒）
             * @note 轮转间隔至少1小时
            */
            void setLogRotateInterval(long rotateInterval_s)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                // 轮转间隔至少1小时
                m_rotateInterval = std::max(3600L, rotateInterval_s);
            }

            /// 设置日志文件大小限制（MB）
            void setMaxLogFileSize(size_t maxLogFileSize_MB)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_maxLogFileSize = maxLogFileSize_MB * 1024 * 1024;
            }

            /**
             * @brief 将当前日志文件清空
             * @return bool 是否成功清空
            */
            bool clear();
        private:
            // 构造函数
            Logger() : 
                m_fd(stdout),
                m_level(LINFO),
                m_lastRotateTime(0),
                m_realRotateTime(0),
                m_rotateInterval(86400L),   // 默认轮转时间为一天
                m_maxLogFileSize(10 * 1024 * 1024), // 默认日志文件大小限制为10MB
                m_currentFileSize(0)
                {}

            /// 关闭日志文件
            void closeLogFile()
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if(m_fd != stdout && m_fd != stderr)
                {
                    fclose(m_fd);
                    m_fd = stdout;
                }
            }

            /// 检查并进行日志轮转
            void checkAndRotateLogFile();

            /// 执行日志轮转
            bool rotateLogFile();

            /// 获取当前时间戳（秒）
            int64_t getCurrentTimeStamp() const
            {
                auto now = std::chrono::system_clock::now();
                return std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
            }

            /// 日志级别对应字符串
            static const char* m_levelStrings[];

            /// 日志文件描述符
            FILE* m_fd;
            /// 当前日志级别（原子类型确保线程安全）
            std::atomic<LogLevel> m_level;
            /// 最后一次轮转时间（秒）
            int64_t m_lastRotateTime;
            /// 当前轮转时间（秒）（原子类型确保线程安全）
            std::atomic<int64_t> m_realRotateTime;
            /// 轮转间隔（秒）
            long m_rotateInterval;
            /// 日志文件大小限制（字节）
            size_t m_maxLogFileSize;
            /// 当前日志文件大小（字节）
            size_t m_currentFileSize;
            /// 日志文件名
            std::string m_logFileName;
            /// 互斥锁，确保线程安全
            mutable std::mutex m_mutex;
    };
}