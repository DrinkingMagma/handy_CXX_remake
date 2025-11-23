/**
 * @file http.h
 * @brief HTTP协议核心类定义头文件，包含HTTP消息封装、连接管理和服务器框架
 * @details 
 *  该文件是HTTP协议功能的核心定义，提供了从消息解析到服务器路由的完整解决方案，主要包含：
 *  1. HTTP消息模型：
 *     - HttpMsg类：HTTP消息基类，封装请求和响应的通用特性（头部处理、编码解码接口）；
 *     - HttpRequest类：HTTP请求具体实现，支持请求行解析、URI解析、查询参数提取；
 *     - HttpResponse类：HTTP响应具体实现，支持状态码管理、默认状态描述映射；
 *  2. HTTP连接管理：
 *     - HttpConnPtr类：封装TCP连接，提供HTTP请求/响应的发送、文件传输、数据清理等便捷接口；
 *     - 内置HTTP上下文管理，自动关联请求和响应对象，支持Keep-Alive连接；
 *  3. HTTP服务器框架：
 *     - HttpServer类：继承自TcpServer，提供路由注册机制（支持GET、POST等任意方法）；
 *     - 支持自定义连接类型、默认请求处理器，适配不同业务场景；
 *  4. 核心特性：
 *     - 头部字段大小写不敏感处理，自动转换为小写存储；
 *     - 支持消息体的复制和引用两种模式，优化内存使用；
 *     - 内置100 Continue状态码处理，支持大请求体场景；
 * @note 
 *  1. 依赖组件：需配合conn.h（TCP连接）、slice.h（缓冲区处理）、non_copy_able.h（不可拷贝基类）使用；
 *  2. 线程安全：HttpServer的路由注册需在服务器启动前完成，连接处理逻辑由事件循环线程执行；
 *  3. 协议支持：目前仅支持HTTP/1.1，默认启用Keep-Alive，需手动处理连接关闭；
 *  4. 消息解析：采用流式解析方式，支持分块接收数据，适合处理大文件传输；
 *  5. 扩展建议：如需支持HTTPS，可在TcpConn层面添加SSL/TLS加密层，HttpServer无需修改。
 */

#pragma once
#include "non_copy_able.h"
#include "conn.h"
#include "slice.h"
#include <map>

namespace handy
{
    /**
     * @class HttpMsg
     * @brief HTTP消息基类，封装HTTP请求和响应的通用功能
     * @note 提供HTTP消息的解析（解码）和序列化（编码）基础逻辑
    */
    class HttpMsg : private NonCopyAble
    { 
        public:
            /// 消息解析结果状态
            enum class Result
            {
                Error,          /// 解析错误
                Complete,       /// 解析完成
                NotComplete,    /// 解析未完成（需要更多数据）
                Continue100     /// 需要发送100Continue响应
            };

            /**
             * @brief 默认构造函数
            */
            HttpMsg() { clear(); }

            /**
             * @brief 纯虚函数，将消息编码到缓冲区
             * @param [out] buf 输出缓冲区
             * @return int 编码的实际字节数
            */
            virtual int encode(Buffer& buf) = 0;

            /**
             * @brief 纯虚函数，尝试从缓冲区解码消息
             * @param buf 输入缓冲区
             * @param isCopyBody 是否赋值消息体（true: 将消息体复制到m_body, false: 使用m_body2进行引用）
             * @return Result 解析状态
            */
            virtual Result tryDecode(Slice buf, bool isCopyBody=true) = 0;

            /**
             * @brief 清空消息所有字段，恢复初始状态
            */
            virtual void clear();

            /**
             * @brief 获取头部字段
             * @return std::map<std::string, std::string> 头部字段m_headers
            */
            std::map<std::string, std::string> getHeaders() const
            {
                return m_headers;
            }

            /**
             * @brief 整体设置所有头信息（替换整个映射）(需保证key全为小写)
             * @param headers 头部信息
            */
            void setHeaders(const std::map<std::string, std::string>& headers)
            {
                m_headers = headers;
            }

            /**
             * @brief 设置单个头部字段（添加/覆盖单个键值对）
             * @param key 头部字段名
             * @param value 头部字段值
            */
            void setHeader(const std::string& key, const std::string& value)
            {        
                std::string lowerKey = key;
                for(char& c : lowerKey)
                    c = tolower(c);
                
                // 用小写 key 存储
                m_headers[lowerKey] = value;
            }

            /**
             * @brief 获取指定头部字段对应的值
             * @param name 头部字段名（不区分大小写）
             * @return std::string 头部字段值（空字符串表示未找到）
            */
            std::string getHeaderValue(const std::string& name) const
            {
                return _getValueFromMap(m_headers, name);
            }

            /**
             * @brief 获取消息体内容
             * @return Slice 消息体切片（优先使用m_body2，当其不存在时使用m_body）
            */
            Slice getBody() const
            {
                return m_body2.empty() ? Slice(m_body) : m_body2;
            }

            /**
             * @brief 设置m_body消息体内容
             * @param msg 消息体
            */
            void setBody(const std::string& msg)
            {
                m_body = msg;
            }

            /**
             * @brief 获取已解析的字节数（仅当tryDecode返回Complete时有效）
             * @return 已解析的字节数
            */
            size_t getScannedBytes() const { return m_scannedLen; }

            /**
             * @brief 获取HTTP版本
             * @return std::string HTTP版本
            */
            std::string getVersion() const { return m_version; }
        protected:
            std::map<std::string, std::string> m_headers;   /// 头部字段（键为小写）
            std::string m_version;                          /// HTTP版本（如"HTTP/1.1"）
            std::string m_body;                             /// 消息体(复制模式)
            Slice m_body2;                                  /// 消息体（引用模式）
            bool m_completed;                               /// 消息解析完成标志
            size_t m_contentLen;                            /// 消息体长度（从Content-Length中获取）
            size_t m_scannedLen;                            /// 已解析的字节数

            /**
             * @brief 内部解析辅助函数，处理通用HTTP消息结构
             * @param buf 输入缓冲区
             * @param isCopyBody 是否复制消息体
             * @param [out] firstLine 存储消息的第一行（请求行/状态行）
             * @return Result 解析状态 
            */
            Result _tryDecode(Slice buf, bool isCopyBody, Slice* firstLine);

            /**
             * @brief 内部函数，从映射表中获取指定键的值（不区分大小写）
             * @param mapTable 键值对映射表
             * @param key 键名
             * @return std::string 键对应的值（空字符串表示未找到）
            */
            std::string _getValueFromMap(const std::map<std::string, std::string>& mapTable,
                                            const std::string& key) const;

    };

    /**
     * @class HttpRequest
     * @brief HTTP请求类，继承自HttpMsg
     * @note 包含HTTP请求特有的方法、URI、查询参数等信息
    */
    class HttpRequest : public HttpMsg
    {
        public:
            /**
             * @brief 默认构造函数
            */
            HttpRequest() { clear(); }

            /**
             * @brief 编码请求到缓冲区
             * @param [out] buf 输出缓冲区
             * @return int 编码的字节数
            */
            int encode(Buffer& buf) override;

            /**
             * @brief 从缓冲区解码请求
             * @param buf 输入缓冲区
             * @param isCopyBody
             * @return 解析状态
            */
            Result tryDecode(Slice buf, bool isCopyBody=true) override;

            /**
             * @brief 清空请求所有字段
            */
            void clear() override;

            /**
             * @brief 获取指定查询参数的值
             * @param name 参数名
             * @return 参数值(空字符串表示未找到)
            */
            std::string getArg(const std::string& name) const{
                return _getValueFromMap(m_args, name);
            }

            std::map<std::string, std::string> m_args;  /// 查询参数
            std::string m_method;                       /// 请求方法（如“GET”、“POST”等）
            std::string m_uri;                          /// 路径部分（不含查询字符串）
            std::string m_queryUri;                     /// 完整URI（含查询字符串）
    };

    /**
     * @class HttpResponse
     * @brief HTTP响应类，继承自HttpMsg
     * @note 包含HTTP响应特有的状态码、状态描述等信息
    */
    class HttpResponse : public HttpMsg 
    {
        public: 
            /**
             * @brief 默认构造函数
            */
            HttpResponse() { clear(); }

            /**
             * @brief 编码响应到缓冲区
             * @param [out] buf 输出缓冲区
             * @return int 编码的字节数
            */
            int encode(Buffer& buf) override;

            /**
             * @brief 从缓冲区解码响应
             * @param buf 输入缓冲区
             * @param isCopyBody 是否复制消息体
             * @return 解析状态
            */
            Result tryDecode(Slice buf, bool isCopyBody=true) override;

            /**
             * @brief 清空响应所有字段
            */
            void clear() override;

            /**
             * @brief 设置404 Not Found状态
            */
            void setNotFound() { setStatus(404, "Not Found"); }

            /**
             * @brief 设置响应码和状态
             * @param status 状态码（如200、404、500）
             * @param msg 状态描述（如"OK"、"Not Found"、"Internal Server Error"）
            */
            void setStatus(int status, const std::string& msg="") {
                m_status = status;
                m_statusMsg = msg.empty() ? _getDefaultStatusMsg(status) : msg;
                // 默认消息体为状态描述
                m_body = m_statusMsg;
            }

            int m_status;               /// 状态码
            std::string m_statusMsg;   /// 状态描述

        private:
            /**
             * @brief 获取状态码对应的默认描述
             * @param status 状态码
             * @return std::string 默认状态描述
            */
            std::string _getDefaultStatusMsg(int status) const;
    };

    /**
     * @class HttpConnPtr
     * @brief Http连接智能指针封装，关联底层TCP连接
     * @note 提供HTTP请求/响应的便捷操作接口
    */
    class HttpConnPtr
    {
        public:
            /// HTTP消息处理回调函数类型
            using HttpCallBack = std::function<void(const HttpConnPtr&)>;

            /**
             * @brief 构造函数，从TCP连接指针创建
             * @param tcpConn TCP连接智能指针
            */
            explicit HttpConnPtr(const TcpConnPtr& tcpConn) : m_tcp(tcpConn) {}

            /**
             * @brief 隐式转换为TCP连接指针
             * @return TcpConnPtr 底层TCP连接智能指针
            */
           operator TcpConnPtr() const { return m_tcp; }

           /**
            * @brief 重置->运算符，访问底层TCP连接
            * @return TcpConn* 底层TCP连接指针
           */
           TcpConn* operator->() const { return m_tcp.get(); }

           /**
            * @brief 重载<运算符，用于容器排序
            * @param other 另一个HttpConnPtr对象
            * @return bool m_tcp的比较结果
           */
           bool operator<(const HttpConnPtr& other) const { return m_tcp < other.m_tcp; }

           /**
            * @brief 获取当前请求对象
            * @return HttpRequest& 请求对象引用
           */
           HttpRequest& getRequest() const;

           /**
            * @brief 获取当前响应对象
            * @return HttpResponse& 响应对象引用
           */
           HttpResponse& getResponse() const;

           /**
            * @brief 发送当前请求对象
           */
           void sendRequest() const { sendRequest(getRequest()); }

           /**
            * @brief 发送指定的请求对象
            * @param req 要发送的请求对象
           */
           void sendRequest(HttpRequest& req) const;

           /**
            * @brief 发送当前响应对象
           */
           void sendResponse() const { sendResponse(getResponse()); }

           /**
            * @brief 发送指定响应的对象
            * @param res 要发送的响应对象
           */
           void sendResponse(HttpResponse& res) const;

           /**
            * @brief 发送文件作为响应
            * @param filename 文件名
           */
           void sendFile(const std::string& filename) const;

           /**
            * @brief 清楚当前连接的请求/响应数据
           */
           void clearData() const;

           /**
            * @brief 注册HTTP消息处理回调
            * @param cb 回调函数
           */
           void onHttpMsg(const HttpCallBack& cb) const;

        private:
            /// HTTP上下文，存储请求和响应对象
            struct HttpContext
            {
                HttpRequest req;    /// 请求对象
                HttpResponse resp;   /// 响应对象
            };

            TcpConnPtr m_tcp;   /// 底层TCP连接

            /**
             * @brief 处理读事件，解析HTTP消息
             * @param cb 消息处理回调
            */
            void handleRead(const HttpCallBack& cb) const;

            /**
             * @brief 记录输出日志
             * @param title 日志标题
            */
            void logOutput(const char* title) const;
            
    };

    /**
     * @class HttpServer
     * @brief HTTP服务器类，继承自Tcpserver
     * @note 提供HTTP请求路由、连接管理等功能
    */
    class HttpServer : public TcpServer 
    {
        public: 
            /**
             * @brief 构造函数
             * @param bases 时间循环管理器
            */
            explicit HttpServer(EventBases* bases);

            /**
             * @brief 设置连接类型（模板方法）
             * @tparam Conn 连接类型（需继承自TcpConn）
            */
            template <class Conn = TcpConn>
            void setConnType()
            {
                m_connCreator = []() { return TcpConnPtr(new Conn); };
            }

            /**
             * @brief 注册GET请求处理回调
             * @param uri 请求路径
             * @param cb 处理回调
            */
            void onGet(const std::string& uri, const HttpConnPtr::HttpCallBack& cb)
            {
                m_routeTable["GET"][uri] = cb;
            }

            /**
             * @brief 注册指定方法的请求处理回调
             * @param method 请求方法（如"POST"、"PUT"）
             * @param uri 请求路径
             * @param cb 处理回调
            */
            void onRequest(const std::string& method, const std::string& uri, const HttpConnPtr::HttpCallBack& cb)
            {
                m_routeTable[method][uri] = cb;
            }

            /**
             * @brief 设置默认请求处理回调（当无匹配路由时调用）
             * @param cb 处理回调
            */
            void onDefault(const HttpConnPtr::HttpCallBack& cb)
            {
                m_defaultHandler = cb;
            }

            /**
             * @brief 获取默认请求处理器
             * @return handler 默认请求处理器
            */
            const HttpConnPtr::HttpCallBack& getDefault()
            {
                return m_defaultHandler;
            }

            /**
             * @brief 根据请求方法和路径在路由表中查找对应的处理器
             * @param method 请求方法
             * @param uri 请求路径
             * @return HttpCallBack 对应方法的处理器，未找到返回默认处理器
            */
            HttpConnPtr::HttpCallBack getHandler(const std::string& method, const std::string& uri)
            {
                auto methodIt = m_routeTable.find(method);
                if(methodIt != m_routeTable.end())
                {
                    auto uriIt = methodIt->second.find(uri);
                    if(uriIt != methodIt->second.end())
                    {
                        return uriIt->second;
                    }
                }
                return m_defaultHandler;
            }
        private:
            HttpConnPtr::HttpCallBack m_defaultHandler; /// 默认处理器
            std::function<TcpConnPtr()> m_connCreator;   /// 连接创建器
            std::map<std::string, std::map<std::string, HttpConnPtr::HttpCallBack>> m_routeTable;   /// 路由表
    };
} // namespace handy
