#include "file.h"
#include <fcntl.h>
#include <unistd.h>

namespace handy 
{
    Status File::getContent(const std::string& filename, std::string& content)
    {
        content.clear(); 

        int fd = open(filename.c_str(), O_RDONLY | O_CLOEXEC);
        if(fd < 0)
            return Status::ioError("open", filename);

        // 使用RAII确保文件描述符即使发生异常也会自动关闭
        struct FdGuard
        {
            int fd;
            ~FdGuard() { close(fd); }
        } guard{fd};

        // 读取文件内容
        char buf[4096];
        while(true)
        {
            ssize_t curReaded = read(fd, buf, sizeof(buf));
            if(curReaded < 0)
                return Status::ioError("read", filename);
            else if(curReaded == 0)
                break;
            content.append(buf, static_cast<size_t>(curReaded));
        }

        return Status();
    }

    Status File::writeContent(const std::string& filename, const std::string& content)
    {
        int fd = open(filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        if(fd < 0)
            return Status::ioError("open", filename);

        struct FdGuard
        {
            int fd;
            ~FdGuard() { close(fd); }
        } guard{fd};

        // 写入全部内容（处理部分写入的情况）
        const char* data = content.data();
        size_t remaining = content.size();
        while(remaining > 0)
        {
            ssize_t written = write(fd, data, remaining);
            if(written < 0)
                return Status::ioError("write", filename);
            
            data += written;
            remaining -= static_cast<size_t>(written);
        }

        // 确保数据写入磁盘
        if(fsync(fd) < 0)
            return Status::ioError("fsync", filename);

        return Status();
    }

    Status File::renameSave(const std::string& targetFilename, const std::string& tmpFilename, const std::string& content)
    {
        // 先写入临时文件
        Status st = writeContent(tmpFilename, content);
        if(!st.ok())
            return st;

        // 原子重命名（确保目标文件要么是旧版本要么是新版本，避免中间态）
    }
}   // namespace handy