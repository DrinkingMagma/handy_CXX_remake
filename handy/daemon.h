/**
 * @file daemon.h
 * @brief 守护进程管理和信号处理封装类的头文件
 * @details 
 *  该文件声明了Daemon类和Signal类，提供守护进程的创建、停止、重启等管理功能，以及系统信号的注册和处理接口。
 *  核心特性如下：
 *  1. Daemon类：
 *     - 提供静态方法实现守护进程的启动（start）、停止（stop）、重启（restart）；
 *     - 支持PID文件管理，通过PID文件实现进程唯一性检查和状态查询；
 *     - 提供process方法，支持通过命令行参数（"start"、"stop"、"restart"）管理守护进程；
 *     - 支持进程切换（changeTo方法），在父进程退出后执行新程序；
 *     - 内部通过两次fork+setsid实现守护进程脱离终端和会话，重定向标准输入/输出/错误到/dev/null。
 *  2. Signal类：
 *     - 提供信号注册接口（signal方法），支持使用C++函数对象（std::function）作为信号处理函数；
 *     - 线程安全的信号处理机制，通过互斥锁保护信号处理函数的注册和调用；
 *     - 内部封装系统信号处理逻辑，自动处理信号分发和异常捕获。
 * @note 
 *  1. Daemon类的方法均为静态方法，无需实例化即可使用；
 *  2. 启动守护进程时需指定PID文件路径，用于存储进程ID，确保进程唯一性；
 *  3. Signal类支持注册任意系统信号（如SIGINT、SIGTERM、SIGHUP等），但需注意部分信号（如SIGKILL、SIGSTOP）无法被捕获或忽略；
 *  4. 信号处理函数中应避免使用非线程安全的操作，且执行时间不宜过长，以免影响程序稳定性；
 *  5. 依赖C++11及以上标准，编译时需启用C++11特性。
 */

#pragma once
#include <csignal>
#include <functional>
#include <string>

namespace handy
{
    /**
     * @class Daemon
     * @brief 守护进程管理类，负责守护进程的启动、停止、重启等操作
     * @note 提供了一套完整的守护进程管理接口，包括创建、停止、重启守护进程以及从PID文件中读取进程ID等功能
    */
    class Daemon
    {
        public:
            /**
             * @brief 启动一个新的守护进程
             * @param pidFilePath PID文件路径，用于存储守护进程的进程ID
             * @return int 0: 启动成功，-1: 启动失败
             * @note 父进程在成功创建子进程后会退出
            */
            static int start(const char* pidFilePath);

            /**
             * @brief 重启守护进程
             * @param pidFilePath PID文件路径
             * @return int 0: 重启成功，-1: 重启失败
             * @note 会先停止当前运行的守护进程（若存在），再启动新的守护进程
            */
            static int restart(const char* pidFilePath);

            /**
             * @brief 停止当前运行的守护进程
             * @param pidFilePath PID文件路径
             * @return int 0: 停止成功，-1: 停止失败
            */
            static int stop(const char* pidFilePath);

            /**
             * @brief 从指定的pid文件中读取守护进程的PID
             * @param pidFilePath PID文件路径
             * @return int 守护进程的PID，-1: 读取失败
            */
            static int getPidFromFile(const char* pidFilePath);

            /**
             * @brief 根据命令执行守护进程的对应操作
             * @param cmd 要执行的操作命令（"start"、"restart"、"stop"）
             * @param pidFilePath PID文件路径
             * @note 1. 若出错，则调用exit(1)退出
             * @note 2. 启动或重启成功时父进程会调用exit(0)退出
             * @note 3. 子进程在启动或重启成功后会返回
            */
            static void process(const char* cmd, const char* pidFilePath);

            /**
             * @brief 创建子进程并在父进程退出后执行新程序
             * @param argv 要执行的程序路径和参数列表
             * @note 可用于在程序内部实现重启功能
            */
            static void changeTo(const char* argv[]);
        private:
            /**
             * @brief 将当前进程的PID写入指定的PID文件
             * @param pidFilePath PID文件路径
             * @return int 0: 成功，-1: 失败
             * @note 内部辅助函数，会对PID文件加锁以防止多个进程同时写入
            */
            static int _writePidFile(const char* pidFilePath);
    };

    /**
     * @class Signal
     * @brief 信号处理类，用于注册和处理系统信号
     * @note 封装了系统信号处理的功能，允许使用C++函数对象作为信号处理函数
    */
    class Signal
    {
        public:
            /**
             * @brief 注册信号处理函数
             * @param sig 要处理的信号编号（如SIGINT、SIGTERM等）
             * @param handler 信号处理函数对象
             * @note 线程安全，可以在多线程环境中调用
            */
            static void signal(int sig, const std::function<void()>& handler);
        private:
            /**
             * @brief 信号处理分发函数
             * @param sig 接收到的信号编号
             * @note 内部使用，作为系统signal函数的回调，负责调用注册的处理函数
            */
            static void _signalHandler(int sig);
    };
}   // namespace handy