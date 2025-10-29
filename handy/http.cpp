#include "http.h"
#include "logger.h"
#include "utils.h"
#include "status.h"
#include "file.h"
#include <string>

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

    HttpMsg::Result HttpMsg::_tryDecode(Slice buf, bool isCopyBody, Slice* line1)
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
                    *line1 = header.eatLine();

                    // 解析头部字段
                    while (header.size() > 0)
                    {
                        // 跳过前一行的"\r\n"
                        header.eat(2);
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
                    m_contentLen = stoull(getHeader("content-length"), nullptr, 10);

                    // 检查是否需要发送100 Continue
                    if(bufSize < m_scannedLen + m_contentLen && !getHeader("expect").empty())
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
        
    }
} // namespace handy
