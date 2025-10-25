#pragma once
#include <string.h>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string>
#include "utils.h"
#include <cstdarg>
#include <iostream>
#include <type_traits>

namespace handy
{
    /**
     * @brief 获取当前线程的系统错误信息字符串
     * @return const char* 系统错误信息字符串，生命周期与当前线程绑定
     * @note 1. 线程安全版本的strerror，使用线程局部存储的缓冲区存储错误信息
     * @note 2. 避免了标准strerror的线程安全问题
    */
    inline const char* errstr()
    {
        // 线程局部存储，避免线程竞争
        static thread_local char buf[256];
        // 兼容XSI版本：返回值为指向错误信息的指针
        const char* ret = strerror_r(errno, buf, sizeof(buf));
        // 若返回的指针在buf范围内，说明成功（XSI版本逻辑）
        if (ret >= buf && ret < buf + sizeof(buf)) {
            return ret;
        }
        // 否则使用POSIX版本的判断（返回0表示成功）
        if (ret == nullptr) { // 部分实现中失败返回nullptr
            return "Unknown error";
        }
        return ret;
    }

    /**
     * @class Status
     * @brief 表示操作的状态信息，包含错误码和描述信息
     * @note 1. 线程安全的状态封装类，用于传递操作结果和错误信息
     * @note 2. 采用值语义设计，支持移动操作以提高性能
    */
    class Status
    {
        public:
            /**
             * @brief 构造函数，创建一个表示“成功”的Status对象
            */
            Status() noexcept : m_state(nullptr) {}

            /**
             * @brief 创建包含指定状态码和错误信息的Status对象
             * @param code 状态码，0表示成功
             * @param msg 错误描述信息
            */
            Status(int code, const char* msg);

            /**
             * @brief 创建包含指定状态码和错误信息的Status对象
             * @param code 状态码，0表示成功
             * @param msg 错误描述信息
            */
            Status(int code, const std::string& msg) : Status(code, msg.c_str()) {}

            /**
             * @brief 拷贝构造寒素
             * @param other 要拷贝的Status对象
            */
            Status(const Status& other)
            {
                m_state = copyState(other.m_state);
            }

            /**
             * @brief 移动构造函数
             * @param other 要移动的Status对象
            */
            Status(Status&& other) noexcept : m_state(other.m_state)
            {
                other.m_state = nullptr;
            }

            /**
             * @brief 析构函数
            */
            ~Status() { delete[] m_state; }

            /**
             * @brief 拷贝赋值运算符
             * @param other 要拷贝的Status对象
             * @return Status& 自身引用
            */
            Status& operator=(const Status& other)
            {
                if(this != &other)
                {
                    delete[] m_state;
                    m_state = copyState(other.m_state);
                }
                return *this;
            }

            /**
             * @brief 移动赋值运算符
             * @param other 要移动的Status对象
             * @return Status& 自身引用
            */
            Status& operator=(Status&& other) noexcept
            {
                if(this != &other)
                {
                    delete[] m_state;
                    m_state = other.m_state;
                    other.m_state = nullptr;
                }
                return *this;
            }

            /**
             * @brief 根据当前线程的系统错误(errno)创建Status对象
             * @return Status 包含系统错误信息的Status对象
            */
            static Status fromSystem()
            {
                return Status(errno, errstr());
            }

            /**
             * @brief 根据指定的系统错误码创建Status对象
             * @param err 系统错误码
             * @return 包含对应错误信息的Status对象
            */
            static Status fromSystem(int err)
            {
                thread_local char buf[256];
                const char* msg;

                // 利用返回值类型自动适配POSIX/XSI版本
                auto ret = strerror_r(err, buf, sizeof(buf));
                
                // POSIX版（返回int）：0表示成功；XSI版（返回char*）：非空即有效
                if constexpr (std::is_same_v<decltype(ret), int>) {
                    msg = (ret == 0) ? buf : "Unknown error";
                } else {
                    msg = ret;  // XSI版直接使用返回的指针（内部已处理成功/失败）
                }

                return Status(err, msg);
            }

            /**
             * @brief 根据格式化字符串创建Status对象
             * @param code 状态码
             * @param fmt 格式化字符串
             * @param ... 格式化参数
             * @return 包含格式化错误信息的Status对象
            */
            static Status fromFormat(int code, const char* fmt, ...) noexcept;

            /**
             * @brief 创建I/O操作错误的Status对象
             * @param op 操作名称
             * @param name 操作的对象名称（如文件名）
             * @return 包含I/O错误信息的Status对象
            */
            static Status ioError(const std::string& op, const std::string& name)
            {
                return fromFormat(errno, "I/O error: %s %s: %s", op.c_str(), name.c_str(), errstr());
            }

            /**
             * @brief 获取状态码
             * @return int 状态码，0表示成功
            */
            int code() const noexcept
            {
                return m_state ? *reinterpret_cast<const uint32_t*>(m_state + 4) : 0;
            }

            /**
             * @brief 获取错误信息字符串
             * @return const char* 错误信息，成功时返回空字符串
            */
            const char* msg() const noexcept
            {
                return m_state ? m_state + 8 : "";
            }

            /**
             * @brief 检查操作是否成功
             * @return bool true：操作成功；false：操作失败
            */
            bool ok() const noexcept { return code() == 0; }

            /**
             * @brief 将状态信息转换为字符串
             * @return 包含状态码和错误信息的字符串
            */
            std::string toString() const
            {
                return utils::format("error code: %d, error msg: %s", code(), msg());
            }

        private:
            // 状态信息（存储格式：0-3 总长度；4-7 状态码、8- 错误信息，以'\0'结尾）
            const char* m_state;

            /**
             * @brief 深拷贝状态信息
             * @param state 要拷贝的状态信息
             * @return 新分配的状态信息指针
            */
            const char* copyState(const char* state)
            {
                if(!state)
                    return nullptr;
                
                const uint32_t size = *reinterpret_cast<const uint32_t*>(state);
                char* newState = new char[size];
                std::memcpy(newState, state, size);
                return newState;
            }
    };

    inline Status::Status(int code, const char* msg)
    {
        if(!msg)
            msg = "";
        
        // 包含终止符
        const uint32_t msgLen = static_cast<uint32_t>(std::strlen(msg)) + 1;
        const uint32_t totalLen = 4 + 4 + msgLen;

        char* newState = new char[totalLen];
        *reinterpret_cast<uint32_t*>(newState) = totalLen;
        *reinterpret_cast<uint32_t*>(newState + 4) = code;
        std::memcpy(newState + 8, msg, msgLen);

        m_state = newState;
    }

    inline Status Status::fromFormat(int code, const char* fmt, ...) noexcept
    {
        if(!fmt)
            return Status(code, "");

        // 第一次调用获取所需缓冲区的大小
        va_list ap;
        va_start(ap, fmt);
        const int msgLen = vsnprintf(nullptr, 0, fmt, ap);
        va_end(ap);

        if(msgLen < 0)
            return Status(code, "Format error");

        // 分配缓冲区并格式化字符串
        const uint32_t totalLen = 4 + 4 + static_cast<uint32_t>(msgLen) + 1;
        char* newState = new (std::nothrow) char[totalLen];
        if(!newState)
            return Status(code, "Memory allocation failed");

        *reinterpret_cast<uint32_t*>(newState) = totalLen;
        *reinterpret_cast<uint32_t*>(newState + 4) = code;

        va_start(ap, fmt);
        vsnprintf(newState + 8, msgLen + 1, fmt, ap);
        va_end(ap);

        Status result;
        result.m_state = newState;
        return result;
    }
} // namespace handy
