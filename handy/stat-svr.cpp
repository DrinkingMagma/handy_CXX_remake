#include "stat-svr.h"
#include "file.h"
#include "http.h"
#include "logger.h"
#include "utils.h"


namespace handy
{
    /**
     * @brief 生成带查询参数的HTML超链接
     * @param path 查询路径
     * @return std::string 格式化的HTML链接字符串
    */
    static std::string queryLink(const std::string& path)
    {
        return utils::format("<a href=\"/?stat=%s\">%s</a>", path.c_str(), path.c_str());
    }

    /**
     * @brief 生成页面跳转的HTML超链接
     * @param path 页面路径
     * @return std::string 格式化的HTML链接字符串
    */
    static std::string pageLink(const std::string& path)
    {
        return utils::format("<a href=\"/%s\">%s</a>", path.c_str(), path.c_str());
    }
} // namespace handy