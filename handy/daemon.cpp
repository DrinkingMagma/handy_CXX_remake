/**
 * @file daemon.cpp
 * @brief 守护进程管理和信号处理类的实现
*/

#include "daemon.h"
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace handy
{
    namespace
    {
        /**
         * @brief 作用域结束时自动执行清理操作的辅助类
        */
        class ExitCaller
        {
            public:
                /**
                 * @brief 构造函数，存储要执行的清理函数
                 * @param functor 清理函数对象
                */
                explicit ExitCaller(std::function<void()>&& functor)
                    : m_functor(std::move(functor)) {}

                /**
                 * @brief 析构函数，执行清理操作
                */
                ~ExitCaller() { m_functor(); }

                // 禁止拷贝和移动
                ExitCaller(const ExitCaller&) = delete;
                ExitCaller& operator=(const ExitCaller&) = delete;
                ExitCaller(ExitCaller&&) = delete;
                ExitCaller& operator=(ExitCaller&&) = delete;
            private:
                // 存储的清理函数
                std::function<void()> m_functor;
        };
    }   // namespace

    int Daemon::_writePidFile(const char* pidFilePath)
    {
        if(!pidFilePath || strlen(pidFilePath) == 0)
        {
            fprintf(stderr, "Pid file path is invalid\n");
            return -1;
        }

        // 打开或创建PID文件
        int lfp = open(pidFilePath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if(lfp < 0)
        {
            fprintf(stderr, "Open/create pid file(%s) failed: errno=%d, msg=%s\n",
                pidFilePath, errno, strerror(errno));
            return -1;
        }

        // 确保文件描述符会被关闭
        ExitCaller closeCaller([lfp]() { close(lfp); });

        // 尝试获取文件锁，防止多个进程同时写入
        if(lockf(lfp, F_TLOCK, 0) < 0)
        {
            fprintf(stderr, "Lock pid file(%s) failed: errno=%d, msg=%s\n",
                pidFilePath, errno, strerror(errno));
            return -1;
        }

        // 写入PID 到文件
        char str[32];
        int len = snprintf(str, sizeof(str), "%d\n", getpid());
        if(len <= 0 || static_cast<size_t>(len) >= sizeof(str))
        {
            fprintf(stderr, "Write pid to pid file(%s) failed: errno=%d, msg=%s\n",
                pidFilePath, errno, strerror(errno));
            return -1;
        }

        if(write(lfp, str, static_cast<size_t>(len)) != len)
        {
            fprintf(stderr, "Write pid to pid file(%s) failed: errno=%d, msg=%s\n",
                pidFilePath, errno, strerror(errno));
            return -1;
        }

        return 0;
    }

    int Daemon::getPidFromFile(const char* pidFilePath)
    {
        if(!pidFilePath || strlen(pidFilePath) == 0)
        {
            fprintf(stderr, "Pid file path is invalid\n");
            return -1;
        } 

        int lfp = open(pidFilePath, O_RDONLY);
        if(lfp < 0)
        {
            // 文件不存在，可能是程序未运行
            if(errno != ENOENT)
            {
                fprintf(stderr, "Open pid file(%s) failed: errno=%d, msg=%s\n",
                    pidFilePath, errno, strerror(errno));
            }
            return -1;
        }

        // 确保文件描述符会被关闭
        ExitCaller closeCaller([lfp]() { close(lfp); });

        char buf[64];
        ssize_t readed = read(lfp, buf, sizeof(buf) - 1);
        if(readed <= 0)
        {
            fprintf(stderr, "Read pid from pid file(%s) failed: errno=%d, msg=%s\n",
                pidFilePath, errno, strerror(errno));
            return -1;
        }

        // 确保字符串以null结尾
        buf[readed] = '\0';

        // 移除换行符
        char* p = strchr(buf, '\n');
        if(p != nullptr)
            *p = '\0';

        // 转换为整数
        char* endPtr;
        long pid = strtol(buf, &endPtr, 10);
        if(endPtr == buf || *endPtr != '\0' || pid <= 0)
        {
            fprintf(stderr, "Invalid pid in pid file(%s): %s\n", pidFilePath, buf);
            return -1;
        }
        return static_cast<int>(pid);
    }

    int Daemon::start(const char* pidFilePath)
    {
        if(!pidFilePath || strlen(pidFilePath) == 0)
        {
            fprintf(stderr, "Pid file path is invalid\n");
            return -1;
        }

        // 检查是否已有守护进程正在运行
        int pid = getPidFromFile(pidFilePath); 
        if(pid > 0)
        {
            // 检查进程是否真的存在
            if(kill(pid, 0) == 0 || errno == EPERM)
            {
                fprintf(stderr, "Daemon is already running, pid=%d, use restart instead\n", pid);
                return -1;
            }
            // 进程不存在，清理旧的PID文件
            unlink(pidFilePath);
        }

        // 检查当前是否已经是守护进程
        if(getppid() == 1)
        {
            fprintf(stderr, "Already running as a daemon, cannot start again\n");
            return -1; 
        }
        // 第一次fork，创建子进程
        pid_t  forkPid = fork();
        if(forkPid < 0)
        {
            fprintf(stderr, "Fork failed: errno=%d, msg=%s\n", errno, strerror(errno));
            return -1;
        }
        else if(forkPid > 0)
        {
            // 父进程退出，让子进程成为守护进程
            exit(0);
        }

        // 创建新的会话，脱离控制终端
        if (setsid() < 0)
        {
            fprintf(stderr, "Setsid failed: errno=%d, msg=%s\n", errno, strerror(errno));
            return -1;
        }

        // 第二次fork，确保进程不是会话组长，防止获取控制终端
        forkPid = fork();
        if(forkPid < 0)
        {
            fprintf(stderr, "Fork again failed: errno=%d, msg=%s\n", errno, strerror(errno));
            return -1;
        }
        else if(forkPid > 0)
        {
            // 父进程退出，让子进程成为守护进程
            exit(0);
        }

        // 设置工作目录为根目录
        if(chdir("/") < 0)
        {
            fprintf(stderr, "Chdir failed: errno=%d, msg=%s\n", errno, strerror(errno));
            return -1;
        }

        // 重定向标准输入、输出、错误到 /dev/null
        int fd = open("/dev/null", O_RDWR);
        if(fd < 0)
        {
            fprintf(stderr, "Open /dev/null failed: errno=%d, msg=%s\n", errno, strerror(errno));
            return -1;
        }

        // 保留原始文件描述符以便恢复
        int stdinFd = dup(STDIN_FILENO);
        int stdoutFd = dup(STDOUT_FILENO);
        int stderrFd = dup(STDERR_FILENO);

        // 重定向标准文件描述符
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);

        // 写入PID到文件
        int ret = _writePidFile(pidFilePath);
        if(ret != 0)
        {
            // 恢复标准文件描述符
            dup2(stdinFd, STDIN_FILENO);
            dup2(stdoutFd, STDOUT_FILENO);
            dup2(stderrFd, STDERR_FILENO);
            close(stdinFd);
            close(stdoutFd);
            close(stderrFd);
            return ret;
        }

        // 注册进程退出时删除PID文件的清理函数
        static ExitCaller del([pidFilePath]() { unlink(pidFilePath); });

        return 0;
    }

    int Daemon::stop(const char* pidFilePath)
    {
        if(!pidFilePath || strlen(pidFilePath) == 0) 
        {
            fprintf(stderr, "Pid file path is invalid\n");
            return -1;
        }

        int pid = getPidFromFile(pidFilePath);
        if(pid <= 0)
        {
            fprintf(stderr, "No such daemon process, pid file(%s) is empty or not exist or contains invalid pid\n", pidFilePath);
            return -1;
        }

        // 尝试优雅终止
        if(kill(pid, SIGTERM) < 0)
        {
            if(errno == ESRCH)
            {
                fprintf(stderr, "No such daemon process, pid=%d, removing pid file\n", pid);
                unlink(pidFilePath);
                return 0;
            }
            fprintf(stderr, "Send SIGTERM to kill daemon process(pid=%d) failed: errno=%d, msg=%s\n", pid, errno, strerror(errno));
            return -1;
        }
        
        // 等待进程退出，最多等待3秒
        for(int i = 0; i < 300; ++i)
        {
            usleep(10 * 1000);  // 10ms
            if(kill(pid, 0) < 0)
            {
                // 若进程已退出
                if(errno == ESRCH)
                {
                    fprintf(stderr, "Daemon process(pid=%d) has been killed\n", pid);
                    unlink(pidFilePath);
                    return 0;
                }
                fprintf(stderr, "Check daemon process(pid=%d) status failed: errno=%d, msg=%s\n", pid, errno, strerror(errno));
                return -1;
            }
        }

        // 优雅终止失败，尝试强制终止
        fprintf(stderr, "Graceful stop daemon process(pid=%d) failed, trying SIGKILL\n", pid);
        if(kill(pid, SIGKILL) < 0)
        {
            fprintf(stderr, "Send SIGKILL to kill daemon process(pid=%d) failed: errno=%d, msg=%s\n", pid, errno, strerror(errno));
            return -1;
        }

        // 等待强制终止
        for(int i = 0; i < 300; ++i)
        {
            usleep(10 * 1000);  // 10ms
            if(kill(pid, 0) < 0 && errno == ESRCH)
            {
                fprintf(stderr, "Daemon process(pid=%d) has been killed\n", pid);
                unlink(pidFilePath);
                return 0;
            }
        }

        fprintf(stderr, "Stop daemon process(pid=%d) failed, still alive\n", pid);
        return -1;
    }

    int Daemon::restart(const char* pidFilePath)
    {
        if(!pidFilePath || strlen(pidFilePath) == 0)
        {
            fprintf(stderr, "Pid file path is invalid\n");
            return -1;
        } 

        int pid = getPidFromFile(pidFilePath);
        if (pid > 0)
        {
            // 检查进程是否存在
            if(kill(pid, 0) == 0)
            {
                // 停止当前进程
                int ret = stop(pidFilePath);
                if(ret < 0)
                    return ret;
            }
            else if(errno != ESRCH && errno != EPERM)
            {
                fprintf(stderr, "Check daemon process(pid=%d) status failed: errno=%d, msg=%s\n", pid, errno, strerror(errno));
                return -1;
            }
        }

        // 启动新的守护进程
        return start(pidFilePath);
    }

    void Daemon::process(const char* cmd, const char* pidFilePath)
    {
        if(!pidFilePath || strlen(pidFilePath) == 0) 
        {
            fprintf(stderr, "Pid file path is invalid");
            exit(1);
        }

        int ret = 0;
        if(cmd == nullptr || strcmp(cmd, "start") == 0)
        {
            ret = start(pidFilePath);
        }
        else if(strcmp(cmd, "stop") == 0)
        {
            ret = stop(pidFilePath);
            if(ret == 0)
                exit(0);
        }
        else if(strcasecmp(cmd, "restart") == 0)
        {
            ret = restart(pidFilePath);
        }
        else
        {
            fprintf(stderr, "Invalid daemon command: %s\n", cmd);
            ret = -1;
        }

        if(ret != 0)
            exit(1);
    }

    void Daemon::changeTo(const char* argv[])
    {
        if(!argv || !argv[0]) 
        {
            fprintf(stderr, "Invalid arguments for changTo()\n");
            return;
        }

        pid_t parentPid = getpid();
        pid_t pid = fork();

        if(pid < 0)
        {
            fprintf(stderr, "Fork failed: errno=%d, msg=%s\n", errno, strerror(errno));
            return;
        }
        else if(pid > 0)
        {
            return;
        }
    }
}   // namespace handy