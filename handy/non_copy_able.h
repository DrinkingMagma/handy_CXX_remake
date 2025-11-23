/**
 * @file non_copy_able.h
 * @brief 不可拷贝基类定义头文件
 * @details 
 *  该文件定义了NonCopyAble类，作为其他类的基类，用于禁止派生类对象的拷贝构造和拷贝赋值操作。
 *  核心实现逻辑：
 *  1. 将拷贝构造函数和拷贝赋值运算符声明为delete，编译时直接禁止调用；
 *  2. 构造函数和析构函数声明为protected，确保该类只能被继承，不能直接实例化；
 *  3. 允许移动构造和移动赋值操作（默认实现），支持资源的高效转移，适用于需要所有权传递的场景；
 * @note 
 *  1. 适用场景：单例模式、文件句柄管理、网络连接等需要确保资源独占性的场景，避免因拷贝导致的资源泄漏或逻辑错误；
 *  2. 使用方式：派生类只需继承NonCopyAble即可，无需额外代码，自动获得不可拷贝的特性；
 *  3. 移动操作支持：若派生类需要自定义移动行为，可重写移动构造函数和移动赋值运算符，覆盖默认实现；
 *  4. 兼容性：该类的设计兼容C++11及以上标准，依赖于delete关键字和默认函数特性。
 */

#pragma once

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
}