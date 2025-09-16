#include "utils.h"
#include "pthread.h"
#include <memory>
#include <chrono>
#include <cstdarg>

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
        
    }
}