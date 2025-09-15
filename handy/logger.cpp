#include "logger.h"
#include <cstdarg>
#include <memory>
#include <sys/stat.h>
#include <sstream>
#include <algorithm>

namespace handy 
{ 
    // 初始化日志级别对应的字符串
    const char* Logger::m_levelStrings[] = 
    {
        "FATAL",
        "ERROR",
        "WARN",
        "INFO",
        "DEBUG",
        "TRACE",
        "ALL"
    };

    void Logger::logv(int level, const char* file, int line, const char* func, const char* fmt, ...)
    {
        // 检查日志级别
        if(level < LFATAL || level > LALL || level > getLogLevel()) 
        {
            return;
        }

        // 检查并进行日志轮转
        checkAndRotateLogFile();

        // 获取当前时间
        // 获取高精度的当前时间点
        auto now = std::chrono::system_clock::now();
        // 转换为C语言传统的time_t类型（秒级精度，便于兼容传统时间函数）
        std::time_t nowC = std::chrono::system_clock::to_time_t(now);
        struct tm tm_info;
        // 使用线程安全版本的localtime_r函数将时间戳转换为包含年月日时分秒的结构体
        localtime_r(&nowC, &tm_info);

        // 格式化时间字符串
        char timeStr[32];
        // 按格式字符串生成时间字符串
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &tm_info);

        // 获取毫秒
        // now.time_since_epoch()：获取从纪元时间（1970-01-01 00:00:00）到当前的总时长
        // std::chrono::milliseconds：将总时长转换为毫秒
        // % std::chrono::seconds(1).count()：对 1 秒（1000 毫秒）取余，得到当前秒内的毫秒数（0-999）
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % std::chrono::seconds(1).count();

        // 构建日志前缀
        char logPrefix[256];
        int prefixLength = snprintf(logPrefix, sizeof(logPrefix), "[%s.%03lld] [%s] [%s:%d %s] ", timeStr, static_cast<long long>(ms.count()), getLogLevelString(static_cast<LogLevel>(level)), file, line, func);

        // 处理可变参数
        // C标准库类型，用于访问可变参数
        va_list args;
        // 初始化args，绑定到fmt后的可变参数（第二个参数必须是最后一个固定参数名）
        // va_start必须与后续的va_end配对使用，否则会导致资源泄漏。
        va_start(args, fmt);

        // 计算日志内容长度
        va_list argsCopy;
        va_copy(argsCopy, args);
        // vsnprintf会消耗参数，需保留原参数用于后续真正格式化
        int contentLength = vsnprintf(nullptr, 0, fmt, argsCopy);
        va_end(argsCopy);

        // 分配缓冲区
        std::unique_ptr<char[]> contentBuffer(new char[contentLength + 1]);
        // 将可变参数按fmt格式写入缓冲区
        // vsnprintf 是一个 C 风格的函数，它需要的是一个原始字符指针（char*） 作为输出缓冲区，而不是 std::unique_ptr 类型
        // contentBuffer.get() 可以获取智能指针内部保存的原始指针
        vsnprintf(contentBuffer.get(), contentLength + 1, fmt, args);
        va_end(args);

        // 加锁并写入日志
        std::lock_guard<std::mutex> lock(m_mutex);

        // 确保文件已经打开
        if(!m_fd)
            m_fd = stdout;

        // 写入日志
        size_t totalLength = prefixLength + contentLength + 2;  // 添加换行符\n和字符串结束符\0
        std::unique_ptr<char[]> logBuffer(new char[totalLength]);
        snprintf(logBuffer.get(), totalLength, "%s%s\n", logPrefix, contentBuffer.get());

        // 使用C标准库函数 fwrite 将完整日志写入文件描述符 m_fd
        size_t written = fwrite(logBuffer.get(), 1, totalLength, m_fd);
        // fwrite 通常会先将数据写入内存缓冲区，而非直接写入磁盘或控制台
        // 调用 fflush 强制将缓冲区中的数据立即写入目标设备
        fflush(m_fd);   // 确保日志立即写入

        // 更新当前文件的大小
        m_currentFileSize += written;
    }

    void Logger::setLogFileName(const std::string& logFileName)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // 关闭当前日志文件
        if(m_fd != stdout && m_fd != stderr)
        {
            fclose(m_fd);
            m_fd = nullptr;
        }

        // 保存新的日志文件名
        m_logFileName = logFileName;

        // 打开新文件
        m_fd = fopen(logFileName.c_str(), "a");
        if(!m_fd)
        {
            // 若打开失败，则回退到标准输出
            m_fd = stdout;
            fprintf(stderr, "Failed to open log file %s, using stdout instead\n", logFileName.c_str());
        }
        else
        {
            // 获取当前文件大小
            // 用于存储文件状态信息的结构体
            struct stat fileStat;
            // 获取文件状态，若成功则返回0
            // fileno(m_fd)：将 C 标准库的FILE*转换为系统文件描述符（int类型），适配fstat函数。
            if(fstat(fileno(m_fd), &fileStat) == 0)
            {
                m_currentFileSize = fileStat.st_size;
            }
            else
            {
                m_currentFileSize = 0;
            }

            // 更新轮转时间
            m_lastRotateTime = getCurrentTimeStamp();
            // 将当前时间戳存入原子变量m_realRotateTime（多线程环境下安全访问的轮转时间标记）。
            m_realRotateTime.store(m_lastRotateTime);
        }
    }

    void Logger::setLogLevel(const std::string& logLevel)
    {
        std::string logLevelLower = logLevel;
        std::transform(logLevelLower.begin(), logLevelLower.end(), logLevelLower.begin(), ::tolower);

        // 默认为LINFO级别
        LogLevel newLevel = LogLevel::LINFO;

        if(logLevelLower == "fatal")
            newLevel = LogLevel::LFATAL;
        else if (logLevelLower == "error")
            newLevel = LogLevel::LERROR;
        else if (logLevelLower == "warn")
            newLevel = LogLevel::LWARN;
        else if (logLevelLower == "info")
            newLevel = LogLevel::LINFO;
        else if (logLevelLower == "debug")
            newLevel = LogLevel::LDEBUG;
        else if (logLevelLower == "trace")
            newLevel = LogLevel::LTRACE;
        else if (logLevelLower == "all")
            newLevel = LogLevel::LALL;
        
        setLogLevel(newLevel);
    }

    void Logger::checkAndRotateLogFile()
    {
        int64_t now = getCurrentTimeStamp();
        // load(std::memory_order_acquire)：原子地读取该变量的值
        // std::memory_order_acquire 是内存序参数，确保后续操作不会被编译器或 CPU 重排到该读取操作之前，保证多线程下的内存可见性。
        int64_t lastRotateTime = m_realRotateTime.load(std::memory_order_acquire);

        // 检查是否需要轮转
        bool isNeedRotate = false;

        // 时间触发
        if(now - lastRotateTime >= m_rotateInterval)
        {
            isNeedRotate = true;
        }
        // 文件大小触发
        else if(m_logFileName.empty() == false)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if(m_currentFileSize >= m_maxLogFileSize)
            {
                isNeedRotate = true;
            }
        }

        // 尝试进行轮转
        // m_realRotateTime.compare_exchange_strong(lastRotateTime, now)：原子比较并交换操作（CAS）
        // 如果 m_realRotateTime 当前的值等于 lastRotateTime（即上次读取的旧值，期间未被其他线程修改），则将其更新为 now（当前时间），返回 true；否则返回 false。
        // 避免多线程同时触发轮转（例如两个线程同时判断需要轮转，CAS 确保只有一个线程能成功执行后续操作）
        if(isNeedRotate && m_realRotateTime.compare_exchange_strong(lastRotateTime, now))
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            rotateLogFile();
            m_lastRotateTime = now;
        }
    }

    bool Logger::rotateLogFile()
    {
        if(m_logFileName.empty() || m_fd == stdout || m_fd == stderr)
        {
            return false;
        }

        // 关闭当前日志文件
        fclose(m_fd);
        m_fd = nullptr;

        // 获取当前时间作为文件名后缀
        auto now = std::chrono::system_clock::now();
        std::time_t nowC = std::chrono::system_clock::to_time_t(now);
        struct tm tmInfo;
        localtime_r(&nowC, &tmInfo);

        char timeStamp[32];
        strftime(timeStamp, sizeof(timeStamp), "%Y%m%d_%H%M%S", &tmInfo);

        // 构建备份文件名
        std::string backupFileName = m_logFileName + "." + timeStamp;

        // 尝试重命名当前日志文件为备份文件
        if (rename(m_logFileName.c_str(), backupFileName.c_str()) != 0) {
            // 若重命名失败（如权限不足），向标准错误输出错误信息，但不终止程序
            fprintf(stderr, "Failed to rotate log file: %s -> %s, because rename file failed!\n", 
                m_logFileName.c_str(), backupFileName.c_str());
        }

        // 打开新的日志文件
        m_fd = fopen(m_logFileName.c_str(), "a");
        if(!m_fd)
        {
            fprintf(stderr, "Failed to open new log file: %s\n", m_logFileName.c_str());
            m_fd = stdout;
            return false;
        }

        // 重置文件大小
        m_currentFileSize = 0;
        return true;
    }
}   // namesapce handy