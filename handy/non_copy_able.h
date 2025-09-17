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