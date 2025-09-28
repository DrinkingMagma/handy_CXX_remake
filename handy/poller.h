#pragma once
#include "utils.h"
#include "logger.h"
#include "non_copy_able.h"

namespace handy 
{
    /**
     * @brief 事件轮询器基类（抽象类）
     * @details 1. 定义I/O事件轮询的统一接口，屏蔽不同操作系统(Linux/macOS)的底层差异
     *          2. 采用私有继承NonCopyAble，禁止拷贝与移动，确保对象唯一性
     *          3. 提供纯虚函数接口，由派生类实现具体的轮询逻辑（如epoll/kqueue）
     * @note 基类仅保证成员变量的原子性，派生类需自行保证事件操作的线程安全
    */
    class PollerBase : private NonCopyAble
    { 
    };
}   // namespace handy