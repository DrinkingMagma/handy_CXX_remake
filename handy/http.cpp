/**
 * @file http.cpp
 * @brief HTTP协议实现文件，包含HTTP消息解析、构建及服务器核心逻辑
 * @details 
 *  该文件实现了HTTP协议的核心功能，包括HTTP请求/响应消息的编码、解码，以及HTTP服务器的路由管理和连接处理，核心特性如下：
 *  1. HTTP消息处理：
 *     - HttpMsg类：提供HTTP消息的基础解析逻辑，支持头部字段提取、内容长度计算；
 *     - HttpRequest类：实现HTTP请求行解析、URI解析（含查询参数提取）、请求消息编码；
 *     - HttpResponse类：实现HTTP状态行构建、响应消息编码，支持默认状态描述映射；
 *  2. HTTP连接管理：
 *     - HttpConnPtr类：封装TCP连接，提供HTTP请求/响应的发送、文件发送、数据清理等接口；
 *     - 支持100 Continue状态码处理，适配大请求体场景；
 *  3. HTTP服务器核心：
 *     - HttpServer类：继承自TcpServer，提供路由注册机制，支持按URI匹配处理器；
 *     - 内置默认404处理器，支持自定义连接创建器和路由处理器；
 *  4. 编码解码优化：
 *     - 采用Slice类高效处理缓冲区数据，减少内存拷贝；
 *     - 头部字段自动转换为小写，确保大小写不敏感匹配；
 * @note 
 *  1. 依赖组件：需配合http.h（类声明）、net.h（TCP连接）、utils.h（工具函数）等头文件使用；
 *  2. 线程安全：HttpServer的路由注册需在服务器启动前完成，连接处理逻辑由事件循环线程执行，确保线程安全；
 *  3. 协议支持：目前仅支持HTTP/1.1，默认启用Keep-Alive连接；
 *  4. 解码限制：请求行和状态行解析依赖空格分隔，头部字段需符合"Key: Value"格式；
 *  5. 错误处理：解析失败时会关闭连接，建议在处理器中捕获异常并返回500状态码。
 */

#include "http.h"
#include "logger.h"
#include "utils.h"
#include "status.h"
#include "file.h"
#include <string>
#include <unordered_map>

namespace handy
{
    void HttpMsg::clear()
    {
        m_headers.clear();
        m_version = "HTTP/1.1";
        m_body.clear();
        m_body2.clear();
        m_completed = false;
        m_contentLen = 0;
        m_scannedLen = 0;
    }

    std::string HttpMsg::_getValueFromMap(const std::map<std::string, std::string>& mapTable, const std::string& key) const
    {
        std::string lowerKey = key;
        for(char& c : lowerKey)
            c = tolower(c);
        auto it = mapTable.find(lowerKey);
        return (it != mapTable.end()) ? it->second : "";
    }

    HttpMsg::Result HttpMsg::_tryDecode(Slice buf, bool isCopyBody, Slice* firstLine)
    {
        if(m_completed)
            return Result::Complete;

        // 解析头部
        if(m_contentLen == 0)
        {
            const char* data = buf.data();
            size_t bufSize = buf.size();

            // 查找头部结束标记"\r\n\r\n"
            while(m_scannedLen + 4 <= bufSize)
            {
                if(memcmp(data + m_scannedLen, "\r\n\r\n", 4) == 0)
                {
                    // 提取头部内容（不含结束标记）
                    Slice header(data, m_scannedLen);
                    // 提取第一行（请求行/状态行）
                    *firstLine = header.eatLine();

                    // 解析头部字段
                    while (header.size() > 0)
                    {
                        Slice line = header.eatLine();
                        // 若为空，说明头部解析结束
                        if(line.empty())
                            break;

                        // 提取key（到":"为止)
                        Slice key = line.eatUntil(':');
                        if(key.empty() || line.empty())
                        {
                            ERROR("Invalid header line: %.*s", (int)line.size(), line.data());
                            return Result::Error;
                        }
                        // 移除':'
                        key = key.sub(0, key.size() - 1);
                        // 去除value的前后空格
                        line.trimSpace();

                        // key转换为小写后存入头部映射
                        std::string lowerKey(key.data(), key.size());
                        for(char& c : lowerKey)
                            c = tolower(c);
                        m_headers[lowerKey] = std::string(line.data(), line.size());
                    }

                    // 跳过头部结束标记
                    m_scannedLen += 4;
                    // 解析内容长度
                    std::string len = getHeaderValue("content-length");
                    if(len != "")
                        m_contentLen = stoull(len, nullptr, 10);

                    // 检查是否需要发送100 Continue
                    if(bufSize < m_scannedLen + m_contentLen && !getHeaderValue("expect").empty())
                        return Result::Continue100;
                    break;
                }
                m_scannedLen++;
            }

            // 头部未解析完成
            if(m_contentLen == 0)
                return Result::NotComplete;
        }

        // 解析消息体
        if(buf.size() >= m_scannedLen + m_contentLen)
        {
            if(isCopyBody)
                m_body.assign(buf.data() + m_scannedLen, m_contentLen);
            else
                m_body2 = Slice(buf.data() + m_scannedLen, m_contentLen);

            m_scannedLen += m_contentLen;
            m_completed =true;
            return Result::Complete;
        }

        return Result::NotComplete;
    }

    int HttpRequest::encode(Buffer& buf)
    {
        size_t initialSize = buf.size();

        // 写入请求行（方法 URI HTTP版本）
        buf.append(Utils::format("%s %s %s\r\n", m_method.c_str(), m_queryUri.c_str(), m_version.c_str()));

        // 写入头部字段
        for(const auto& kv : m_headers)
        {
            buf.append(Utils::format("%s: %s\r\n", kv.first.c_str(), kv.second.c_str()));
        }

        // 写入默认头部（Connection: Keep-Alive 和 Content-Length）
        buf.append("Connection: Keep-Alive\r\n");
        buf.append(Utils::format("Content-Length: %zu\r\n", getBody().size()));

        // 写入空行分隔头部和消息体
        buf.append("\r\n");

        // 写入消息体
        buf.append(getBody());

        return buf.size() - initialSize;
    }

    HttpMsg::Result HttpRequest::tryDecode(Slice buf, bool isCopyBody)
    {
        Slice firstLine;
        Result result = _tryDecode(buf, isCopyBody, &firstLine);

        if(!firstLine.empty())
        {
            // 解析请求行：方法URI版本
            m_method = firstLine.eatWord();
            m_queryUri = firstLine.eatWord();
            m_version = firstLine.eatWord();

            // 验证URI格式（必须以'/'开头）
            if(m_queryUri.empty() || m_queryUri[0] != '/')
            {
                ERROR("Invalid URI: %.*s (must start with '/')", (int)m_queryUri.size(), m_queryUri.data());
                m_method.clear();
                m_queryUri.clear();
                m_version.clear();
                return Result::Error;
            }

            // 解析URI中的查询参数（分割?前后部分）
            size_t queryPos = m_queryUri.find('?');
            if(queryPos != std::string::npos)
            {
                m_uri = m_queryUri.substr(0, queryPos);
                Slice queryStr(m_queryUri.data() + queryPos + 1, m_queryUri.size() - queryPos - 1);

                // 解析键值对（格式：key1=value1&key2=value2&...）
                size_t pos = 0;
                while(pos < queryStr.size())
                {
                    // 查找=的位置
                    size_t eqPos = queryStr.find('=', pos);
                    size_t andPos = queryStr.find('&', pos);
                    size_t keyEnd = Slice::npos;

                    // 确定keyEnd为两者中较小的有效位置
                    if(eqPos != Slice::npos && andPos != Slice::npos)
                    {
                        keyEnd = std::min(eqPos, andPos);
                    }
                    else if(eqPos != Slice::npos)
                    {
                        keyEnd = eqPos;
                    }
                    else if(andPos != Slice::npos)
                    {
                        keyEnd = andPos;
                    }
                    else
                    {
                        // 未找到
                        keyEnd = queryStr.size();
                    }

                    Slice key = queryStr.sub(pos, keyEnd);
                    if(key.empty())
                    {
                        pos = keyEnd + 1;
                        continue;
                    }

                    // 找到value结束位置（&）
                    pos = keyEnd + (keyEnd < queryStr.size() && queryStr[keyEnd] == '=' ? 1 : 0);
                    size_t valEnd = queryStr.find('&', pos);
                    if(valEnd == Slice::npos)
                        valEnd = queryStr.size();
                    Slice val = queryStr.sub(pos, valEnd);

                    // 存入参数映射
                    m_args[std::string(key.data(), key.size())] = std::string(val.data(), val.size());
                    pos = valEnd + 1;
                }
            }
            else
            {
                // 无查询参数，URI即为路径
                m_uri = m_queryUri;
            }
        }

        return result;
    }

    void HttpRequest::clear()
    {
        HttpMsg::clear();
        m_args.clear();
        m_method = "GET";
        m_uri.clear();
        m_queryUri.clear();
    }

    std::string HttpResponse::_getDefaultStatusMsg(int status) const
    {
        static const std::unordered_map<int, std::string> statusMsgs = {
            {200, "OK"},
            {201, "Created"},
            {400, "Bad Request"},
            {404, "Not Found"},
            {500, "Internal Server Error"}
        };

        auto it = statusMsgs.find(status);
        return (it != statusMsgs.end()) ? it->second : "Unknown Status";
    }

    int HttpResponse::encode(Buffer& buf)
    {
        size_t initialSize = buf.size();

        // 写入状态行（版本 状态码 描述）
        buf.append(Utils::format("%s %d %s\r\n", m_version.c_str(), m_status, m_statusMsg.c_str()));
    
        // 写入头部字段
        for(const auto it : m_headers)
        {
            buf.append(Utils::format("%s: %s\r\n", it.first.c_str(), it.second.c_str()));
        }

        // 写入默认头部
        buf.append("Connection: Keep-Alive\r\n");
        buf.append(Utils::format("Content-Length: %zu\r\n", getBody().size()));

        // 写入空行和消息体
        buf.append("\r\n");
        buf.append(getBody());

        return buf.size() - initialSize;
    }

    HttpMsg::Result HttpResponse::tryDecode(Slice buf, bool isCopyBody)
    {
        Slice firstLine;
        Result result = _tryDecode(buf, isCopyBody, &firstLine);

        if(!firstLine.empty())
        {
            // 解析状态行：版本 状态码 描述
            m_version = firstLine.eatWord();
            Slice statusCodeStr = firstLine.eatWord();
            m_status = stoi(std::string(statusCodeStr.data(), statusCodeStr.size()));
            m_statusMsg = firstLine.trimSpace().toString();
        }

        return result;
    }

    void HttpResponse::clear()
    {
        HttpMsg::clear();
        m_status = 200;
        m_statusMsg = "OK";
    }

    HttpRequest& HttpConnPtr::getRequest() const
    {
        return m_tcp->getInternalContext().context<HttpContext>().req;
    }

    HttpResponse& HttpConnPtr::getResponse() const
    {
        return m_tcp->getInternalContext().context<HttpContext>().resp;
    }

    void HttpConnPtr::sendRequest(HttpRequest& req) const
    {
        req.encode(m_tcp->getOutputBuffer());
        logOutput("HTTP Request");
        clearData();
        m_tcp->sendOutputBuffer();
    }

    void HttpConnPtr::sendResponse(HttpResponse& resp) const
    {
        resp.encode(m_tcp->getOutputBuffer());
        logOutput("HTTP Response");
        clearData();
        m_tcp->sendOutputBuffer();
    }

    void HttpConnPtr::sendFile(const std::string& filename) const
    {
        std::string content;
        Status st = File::getContent(filename, content);
        HttpResponse& resp = getResponse();

        if(st.code() == ENOENT)
            resp.setNotFound();
        else if(!st.ok())
            resp.setStatus(500, st.msg());
        else
        {
            resp.getBody() = Slice(content);
            resp.getHeaders()["Content-Type"]= Utils::getMimeType(filename);
        }

        sendResponse();
    }

    void HttpConnPtr::clearData() const
    {
        // 客户端：清除响应数据
        if(m_tcp->isClient())
        {
            m_tcp->getInputBuffer().consume(getResponse().getScannedBytes());
            getResponse().clear();
        }
        // 服务端：清除请求数据
        else
        {
            m_tcp->getInputBuffer().consume(getRequest().getScannedBytes());
            getRequest().clear();
        }
    }

    void HttpConnPtr::onHttpMsg(const HttpCallBack& cb) const
    {
        m_tcp->onReadable([cb](const TcpConnPtr& tcp) {
            HttpConnPtr httpConn(tcp);
            httpConn.handleRead(cb);
        }); 
    }

    void HttpConnPtr::handleRead(const HttpCallBack& cb) const
    {
        // 服务端处理请求
        if(!m_tcp->isClient())
        {
            HttpRequest& req = getRequest();
            HttpMsg::Result result = req.tryDecode(m_tcp->getInputBuffer());

            if(result == HttpMsg::Result::Error)
            {
                m_tcp->close();
                return;
            }
            else if(result == HttpMsg::Result::Continue100)
            {
                // 发送100 Continue响应
                m_tcp->send("HTTP/1.1 100 Continue\r\n\r\n");
            }
            else if(result == HttpMsg::Result::Complete)
            {
                INFO("Received HTTP request: %s %s %s",
                    req.m_method.c_str(), req.m_uri.c_str(), req.getVersion().c_str());
                TRACE("Request data:\n%.*s", (int)m_tcp->getInputBuffer().size(), m_tcp->getInputBuffer().data());
                // 调用请求处理回调函数
                cb(*this);
            }
        }
        // 客户端处理响应
        else
        {
            HttpResponse& resp = getResponse();
            HttpMsg::Result result = resp.tryDecode(m_tcp->getInputBuffer());

            if(result == HttpMsg::Result::Error)
            {
                m_tcp->close();
                return;
            }
            else if(result == HttpMsg::Result::Complete)
            {
                INFO("Received HTTP response: %d %s", resp.m_status, resp.m_statusMsg.c_str());
                TRACE("Response data:\n%.*s", (int)m_tcp->getInputBuffer().size(), m_tcp->getInputBuffer().data());
                cb(*this);
            }
        }
    }

    void HttpConnPtr::logOutput(const char* title) const
    {
        Buffer& output = m_tcp->getOutputBuffer();
        TRACE("%s:\n%.*s", title, (int)output.size(), output.data());
    }

    HttpServer::HttpServer(EventBases* bases) : TcpServer(bases)
    {
        // 默认404处理器
        m_defaultHandler = [](const HttpConnPtr& conn) {
            HttpResponse& resp = conn.getResponse();
            resp.setNotFound();
            conn.sendResponse();
        };

        // 默认连接创建器
        m_connCreator = []() { return TcpConnPtr(new TcpConn); };

        // 注册连接创建回调
        onConnCreate([this]() {
            TcpConnPtr tcpConn = m_connCreator();
            HttpConnPtr httpConn (tcpConn);

            // 设置HTTP消息处理逻辑
            httpConn.onHttpMsg([this](const HttpConnPtr& conn) {
                const HttpRequest& req = conn.getRequest();
                auto methodIt = m_routeTable.find(req.m_uri);
                if(methodIt != m_routeTable.end())
                {
                    auto uriIt = methodIt->second.find(req.m_uri);
                    if(uriIt != methodIt->second.end())
                    {
                        // 调用匹配的路由处理器
                        uriIt->second(conn);
                        return;
                    }
                }
                // 若无匹配路由，调用默认处理器
                m_defaultHandler(conn);
            });

            return tcpConn;
        });
    }
} // namespace handy
