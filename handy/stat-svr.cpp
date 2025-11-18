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
        return Utils::format("<a href=\"/?stat=%s\">%s</a>", path.c_str(), path.c_str());
    }

    /**
     * @brief 生成页面跳转的HTML超链接
     * @param path 页面路径
     * @return std::string 格式化的HTML链接字符串
    */
    static std::string pageLink(const std::string& path)
    {
        return Utils::format("<a href=\"/%s\">%s</a>", path.c_str(), path.c_str());
    }

    StatServer::StatServer(EventBase* base) : m_server(base)
    {
        m_server.onDefault([this](const HttpConnPtr& conn) {
            HttpRequest& req = conn.getRequest();
            HttpResponse& resp = conn.getResponse();
            Buffer buf;

            // 解析查询参数（优先从?stat=xxx获取，其次从URI路径获取）
            std::string query = req.getArg("stat");
            if(query.empty() && !req.m_uri.empty() && req.m_uri != "/")
            {
                // 去除URI中的开头'/'
                query = req.m_uri.substr(1);
            }

            // 处理具体查询
            if(!query.empty())
            {
                auto it = m_allCallBacks.find(query);
                if(it != m_allCallBacks.end())
                {
                    // 调用注册的回调函数
                    it->second(req, resp);
                }
                else
                {
                    // 未找到对应处理函数
                    resp.setNotFound();
                }
            }

            // 根路径显示汇总页面
            if(req.m_uri == "/")
            {
                // 刷新按钮
                buf.append("<a href=\"/\">刷新</a><br/>\n");

                // 状态信息表格
                buf.append("<table border=\"1\">\n");
                buf.append("<tr><th>状态<</th><th>描述</th><th>数量</th></tr>\n");
                for(const auto& entry : m_statCallBacks)
                {
                    HttpResponse tempResp;
                    HttpRequest tempReq;
                    tempReq.m_uri = entry.first;
                    entry.second.callback(tempReq, tempResp);

                    buf.append("<tr><td>")
                        .append(pageLink(entry.first))
                        .append("</td><td>")
                        .append(entry.second.description)
                        .append("</td><td>")
                        .append(tempResp.getBody())
                        .append("</td></tr>\n");
                }
                buf.append("</table>\n<br/>\n");

                // 页面信息表格
                buf.append("<table border=\"1\">\n");
                buf.append("<tr><th>页面</th><th>描述</th></tr>\n");
                for(const auto& entry : m_pageCallBacks)
                {
                    buf.append("<tr><td>")
                        .append(pageLink(entry.first))
                        .append("</td><td>")
                        .append(entry.second.description)
                        .append("</td></tr>\n");
                }
                buf.append("</table>\n<br/>\n");

                // 命令信息表格
                buf.append("<table border=\"1\">\n");
                buf.append("<tr><th>命令</th><th>描述</th></tr>\n");
                for(const auto& entry : m_cmdCallBacks)
                {
                    buf.append("<tr><td>")
                        .append(queryLink(entry.first))
                        .append("</td><td>")
                        .append(entry.second.description)
                        .append("</td></tr>\n");
                }
                buf.append("</table>\n");

                // 添加子查询结果（如果有）
                if(!resp.getBody().empty())
                { 
                    buf.append(Utils::format("<br/>子查询 %s 结果:<br/>%s", query.c_str(), resp.getBody().toString().c_str()));
                }

                resp.setBody(buf.data());
                resp.setHeader("Content-Type", "text/html; charset=utf-8");
            }

            // 发送响应并记录日志
            INFO("Response status: %d, content lenth: %zu", resp.m_status, resp.getBody().size());
            conn.sendResponse();
        });
    }

    void StatServer::onRequest(StatType type, const std::string& key, const std::string& desc, const StatCallBack& cb)
    {
        if(key.empty())
        {
            ERROR("register failed: key can not be empty");
            return;
        }

        // 根据类型存储回调
        CallBackInfo info{desc, cb};
        switch(type)
        {
            case StatType::STATE:
                m_statCallBacks[key] = info;
                break;
            case StatType::PAGE:
                m_pageCallBacks[key] = info;
                break;
            case StatType::CMD:
                m_cmdCallBacks[key] = info;
                break;
            default:
                ERROR("Unknown StatType: %d", static_cast<int>(type));
                return;
        }
        // 加入全局映射表
        m_allCallBacks[key] = cb;
    }

    void StatServer::onRequest(StatType type, const std::string& key, const std::string& desc, const InfoCallBack& cb)
    {
        // 将InfoCallBack适配为StatCallBack
        onRequest(type, key, desc, [cb](const HttpRequest&, HttpResponse& resp)
        {
            resp.setBody(cb());
            resp.setHeader("Content-Type", "text/plain; charset=utf-8");
        });
    }

    void StatServer::onPageFile(const std::string& page, const std::string& desc, const std::string& file)
    {
        onRequest(StatType::PAGE, page, desc, [file](const HttpRequest&, HttpResponse& resp){
            std::string content;
            Status st = File::getContent(file, content);
            if(!st.ok())
            {
                ERROR("get file content failed: %s, msg: %s", file.c_str(), st.toString().c_str());
                resp.setNotFound();
                return;
            }

            resp.setBody(content);
            // 根据文件后缀设置Content-Type（简化版）
            if(file.size() >= 5 && file.substr(file.size() - 4) == ".html")
            {
                resp.setHeader("Content-Type", "text/html; charset=utf-8");
            }
            else if(file.size() >= 4 && file.substr(file.size() - 3) == ".js")
            {
                resp.setHeader("Content-Type", "application/javascript; charset=utf-8");
            }
            else
            {
                resp.setHeader("Content-Type", "text/plain; charset=utf-8");
            }
        });
    }
} // namespace handy