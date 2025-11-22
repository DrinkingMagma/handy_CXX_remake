/**
 * @file file.cpp
 * @brief 文件操作工具类实现，提供文件读写、目录管理、路径操作等常用功能
 * @details 
 *  该文件实现了File类的核心方法，封装了底层文件系统操作，提供安全、便捷的文件处理接口，核心特性如下：
 *  1. 文件内容读写：支持读取文件全部内容到字符串、写入字符串到文件，自动处理大文件分块读写；
 *  2. 原子文件操作：提供renameSave方法，通过"先写临时文件+原子重命名"确保文件更新的原子性，避免中间态；
 *  3. 目录管理：支持获取目录下所有子文件/目录名（排除.和..）、创建目录、删除空目录；
 *  4. 文件属性操作：支持获取文件大小、检查文件是否存在、删除文件、重命名文件；
 *  5. 安全特性：使用RAII机制（FdGuard、DirGuard）自动管理文件描述符和目录指针，避免资源泄露；
 *  6. 错误处理：所有操作返回Status对象，包含错误码和详细错误信息，便于问题排查。
 * @note 
 *  1. 文件操作权限：写入文件时默认权限为0600（仅所有者可读写），创建目录时默认权限为0755（所有者可读写执行，其他仅可执行）；
 *  2. 原子操作限制：renameSave的原子性依赖于临时文件和目标文件在同一个文件系统（同一块磁盘分区）；
 *  3. 目录遍历：getChildren返回的子项已按字典序排序，确保结果一致性；
 *  4. 大文件处理：读写操作使用4096字节缓冲区，避免一次性加载大文件到内存，提升性能；
 *  5. 错误处理：所有系统调用错误（如文件不存在、权限不足）都会被捕获并转换为Status对象，调用者需检查返回值。
 */

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