#include "file.h"
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <algorithm>
#include <sys/stat.h>

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
        if(::rename(tmpFilename.c_str(), targetFilename.c_str()) < 0)
            return Status::ioError("rename", tmpFilename + " -> " + targetFilename);

        return Status();
    }

    Status File::getChildren(const std::string& dir, std::vector<std::string>* result)
    { 
        if(!result)
            return Status::fromFormat(EINVAL, "result pointer cannot be null");
        result->clear();

        // 打开目录
        DIR* dirPtr = opendir(dir.c_str());
        if(!dirPtr)
            return Status::ioError("opendir", dir);

        struct DirGuard
        {
            DIR* dir;
            ~DirGuard() { closedir(dir); }
        }  guard{dirPtr};

        // 实现清空错误码，避免遗留的错误码影响逻辑
        errno = 0;
        // 遍历目录项
        dirent* entry;
        while((entry = readdir(dirPtr)) != nullptr)
        {
            std::string name(entry->d_name);
            // 跳过.（当前目录）和..（父目录）
            if(name != "." && name != "..")
                result->push_back(name);
        }

        // 检查readdir()是否出错
        if(errno != 0)
            return Status::ioError("readdir", dir);

        // 排序目录项（确保结果一致性）
        sort(result->begin(), result->end());

        return Status();
    }

    Status File::deleteFile(const std::string& filename)
    {
        if(unlink(filename.c_str()) < 0)
            return Status::ioError("unlink", filename); 
        return Status();
    }

    Status File::createDir(const std::string& dirname)
    {
        if(mkdir(dirname.c_str(), 0755) < 0)
            return Status::ioError("mkdir", dirname);
        return Status();
    }

    Status File::deleteDir(const std::string& dirname)
    {
        if(rmdir(dirname.c_str()) < 0)
            return Status::ioError("rmdir", dirname);
        return Status();
    }

    Status File::getFileSize(const std::string& filename, uint64_t* size)
    {
        if(!size)
            return Status::fromFormat(EINVAL, "size pointer cannot be null");

        struct stat fileStat;
        if(stat(filename.c_str(), &fileStat) < 0)
        {
            *size = 0;
            return Status::ioError("stat", filename);
        }

        // 检查是否为常规文件
        if(!S_ISREG(fileStat.st_mode))
        {
            *size = 0;
            return Status::fromFormat(EINVAL, "%s is not a regular file", filename.c_str());
        }

        *size = static_cast<uint64_t>(fileStat.st_size);
        return Status();
    }

    Status File::rename(const std::string& src, const std::string& dst)
    { 
        if(::rename(src.c_str(), dst.c_str()) < 0)
            return Status::ioError("rename", src + " -> " + dst);
        return Status();
    }

    bool File::exists(const std::string& path)
    {
        return access(path.c_str(), F_OK) == 0;
    }
}   // namespace handy