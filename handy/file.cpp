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
    }
}   // namespace handy