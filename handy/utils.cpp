#include "utils.h"
#include "pthread.h"
#include <memory>
#include <chrono>
#include <cstdarg>
#include <fcntl.h>

// 使用std命名空间显式限定（避免using namespace std潜在冲突）
using std::string;
using std::unique_ptr;
using std::make_unique;
using std::chrono::system_clock;
using std::chrono::steady_clock;
using std::chrono::microseconds;
using std::chrono::duration_cast;
using std::chrono::time_point;

namespace handy
{
// -------------------------- 线程局部存储（TLS）辅助函数 --------------------------
    // 线程局部存储的tm结构，避免多线程竞争（static限定在命名空间内可见）
    static pthread_key_t g_tmKey;
    static pthread_once_t g_tmKeyOnce = PTHREAD_ONCE_INIT;

    /**
     * @brief 初始化线程局部存储键（仅执行一次）
     * @note 用于管理tm结构的内存释放，避免内存泄露
    */
    static void initTmKey()
    {
        // 创建TLS键，指定析构函数：删除tm对象
        pthread_key_create(&g_tmKey, [](void* ptr) {
            delete static_cast<tm*>(ptr);
        });
    }

    /**
     * @brief 获取当前线程专属的tm结构
     * @return 指向当前线程tm结构的指针（自动出初始化，线程安全）
    */
    static tm* getThreadLocalTm()
    {
        // 确保TLS键仅初始化一次（多线程安全）
        pthread_once(&g_tmKeyOnce, initTmKey);

        // 从当前线程获取tm对象
        tm* t = static_cast<tm*>(pthread_getspecific(g_tmKey));
        if(!t)
        {
            // 首次调用时创建tm对象并绑定到当前线程
            t = new tm;
            pthread_setspecific(g_tmKey, t);
        }
        return t;
    }

// -------------------------- utils类静态成员函数实现 --------------------------
    string utils::format(const char* fmt, ...) noexcept
    {
        // 初始缓冲区大小（512字节，满足多数场景）
        const size_t initialBufSize = 512;
        // 最大缓冲区限制（1MB，防止恶意输入导致内存溢出）
        const size_t maxBufSize = 1024 * 1024;

        // 初始化缓冲区（使用智能指针自动释放内存，防止内存泄漏）
        auto buf = std::make_unique<char[]>(initialBufSize);
        size_t currentBufSize = initialBufSize;

        while (true)
        {
            va_list ap;
            // 初始化可变参数列表
            va_start(ap, fmt);

            // vsnprintf：安全格式化，返回需要的缓冲区大小（不含终止符'\0'）
            // 若返回值 < 0：格式化失败；若 >= currentBufSize：缓冲区不足
            int neededSize = vsnprintf(buf.get(), currentBufSize, fmt, ap);
            // 释放可变参数列表
            va_end(ap);

            // 处理格式化失败（如非法占位符）
            if(neededSize < 0)
                return "";

            // 缓冲区足够：直接返回格式化结果
            if(static_cast<size_t>(neededSize) < currentBufSize)
            {
                return string(buf.get(), static_cast<size_t>(neededSize));
            }

            // 缓冲区不够：重新分配缓冲区，并重新格式化
            // 计算新的缓冲区大小（+1用于存储终止符'\0'）
            currentBufSize = static_cast<size_t>(neededSize) + 1;
            // 若超出最大限制：拒绝扩容，避免内存耗尽
            if (currentBufSize > maxBufSize)
                return "";

            // 扩容缓冲区
            buf.reset(new char[currentBufSize]);
        }
        
    }

    int64_t utils::timeMicro() noexcept
    {
        try
        {
            time_point<system_clock> now = system_clock::now();
            // 转换为微秒级的时间戳
            return duration_cast<microseconds>(now.time_since_epoch()).count();
        }
        catch(...)
        {
            // 捕获所有异常（如时钟未初始化），返回错误值
            return -1;
        }
    }

    int64_t utils::steadyMicro() noexcept
    {
        try
        {
            time_point<steady_clock> now = steady_clock::now();
            return duration_cast<microseconds>(now.time_since_epoch()).count();
        }
        catch(...)
        {
            return -1;
        }
    }

    string utils::readableTime(time_t t) noexcept
    {
        try
        {
            // 获取当前线程的tm结构（线程安全，无竞争）
            tm* tm_ptr = getThreadLocalTm();
            // localtime_r：线程安全的时间转换（避免localtime的全局竞争）
            if(!localtime_r(&t, tm_ptr))
            {
                // 转换失败（如非法时间戳）
                return "invalid time";
            }

            // 格式化时间字符串（调用utils::format确保一致性)
            return format(
                "%04d-%02d-%02d %02d:%02d:%02d",
                tm_ptr->tm_year + 1900,
                tm_ptr->tm_mon + 1,
                tm_ptr->tm_mday,
                tm_ptr->tm_hour,
                tm_ptr->tm_min,
                tm_ptr->tm_sec
            );
        } catch (...)
        {
            // 捕获所有异常（如内存分配失败）
            return "error formatting time";
        }
    }

    int64_t utils::atoi(const char* b, const char* e) noexcept
    {
        // 校验参数的合法性
        if(!b || !e || b >= e)
            return 0;

        char* endPtr = nullptr;
        // strtoll：安全转换为long long(支持64位，避免溢出)
        long long val = strtoll(b, &endPtr, 10);

        // 即使部分转换（如endPtr < e），仍返回已转换的值
        return static_cast<int64_t>(val);
    }

    int64_t utils::atoi2(const char* b, const char* e) noexcept
    {
        if(!b || !e || b >= e)
            return -1;

        char* endPtr = nullptr;
        long long val = strtoll(b, &endPtr, 10);

        // 严格校验：必须完全消耗输入字符串（即endPtr == e）
        if(endPtr != e)
            return -1;

        return static_cast<int64_t>(val);
    }

    int utils::addFdFlag(int fd, int flag) noexcept
    {
        // 校验文件描述符的合法性
        if(fd < 0) 
        {
            // 设置错误码：无效的文件描述符
            errno = EBADF;
            return -1;
        }

        // 1. 获取当前文件描述符的标志
        int currentFlags = fcntl(fd, F_GETFD);
        if(currentFlags == -1)
        {
            // fcntl失败时，errno被系统自动设置
            return -1;
        }

        // 2. 检查标志是否已经存在（避免重复设置）
        if((currentFlags & flag) == flag)
            return 0;

        // 3. 添加标志并设置回文件描述符
        if(fcntl(fd, F_SETFD, currentFlags | flag) == -1)
        {
            // fcntl失败时，errno被系统自动设置
            return -1;
        }

        return 0;
    }
}