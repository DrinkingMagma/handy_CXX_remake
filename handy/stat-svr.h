#pragma once

#include "non_copy_able.h"
#include "event_base.h"
#include "http.h"
#include "slice.h"
#include <string>
#include <map>
#include <functional>

namespace handy
{
    /**
     * @brief HTTP请求处理回调函数
     * @param req 输入的HTTP请求
     * @param[out] resp 输出的HTTP响应
    */
    using StatCallBack = std::function<void(const HttpRequest&, HttpResponse&)>;

    /**
     * @brief 信息获取回调函数，返回字符串类型的状态信息
     * @return std::string 状态信息字符串
    */
    using InfoCallBack = std::function<std::string()>;

    /**
     * @brief 数值获取回调函数，返回int64_t类型的状态值
     * @return int64_t 状态数值
    */
    using IntCallBack = std::function<int64_t()>;

    /**
     * @class StatServer
     * @brief 状态监控服务器，提供HTTP接口用于查询状态、展示页面和执行命令
     * @note 线程安全：所有注册的回调函数需保证线程安全，内部数据访问在事件循环线程中进行
    */
    class StatServer : private NonCopyAble 
    {
        public:
            // 请求类型枚举，区分不同的处理逻辑
            enum class StatType
            {
                STATE,      // 状态信息（如计数器、状态值）
                PAGE,       // 页面信息（如HTML、JSON）
                CMD         // 命令信息（如设置参数、执行命令）
            };

            /**
             * @brief 构造函数
             * @param base 事件循环对象，服务器运行依赖的事件驱动基础
            */
            explicit StatServer(EventBase* base);

            /**
             * @brief 将服务器绑定到指定的主机和端口
             * @param host 绑定的主机地址（如0.0.0.0）
             * @param port 绑定的端口号
             * @return int 0: 绑定成功, 非0: 绑定失败（错误码未errno）
            */
           int bind(const std::string& host, unsigned short port)
           {
                return m_server.bind(host, port);
           }

           /**
            * @brief 注册请求处理回调函数
            * @param type 请求类型（STATE/PAGE/CMD）
            * @param key 唯一标识（用于HTTP路径匹配）
            * @param desc 描述信息（用于生成首页帮助）
            * @param cb 处理回调函数
           */
           void onRequest(StatType type, const std::string& key, const std::string& desc, const StatCallBack& cb);

           /**
            * @brief 注册信息类型的请求处理回调函数（适配InfoCallBack）
            * @param type 请求类型（STATE/PAGE/CMD）
            * @param key 唯一标识（用于HTTP路径匹配）
            * @param desc 描述信息（用于生成首页帮助）
            * @param cb 信息获取回调函数
           */
           void onRequest(StatType type, const std::string& key, const std::string& desc, const InfoCallBack& cb);

           /**
            * @brief 注册状态信息回调（快捷接口）
            * @param state 状态标识
            * @param desc 状态描述
            * @param cb 信息获取回调函数
           */
           void onState(const std::string& state, const std::string& desc, const InfoCallBack& cb)
           {
                onRequest(StatType::STATE, state, desc, cb);
           }

           /**
            * @brief 注册数值型状态信息回调（快捷接口）
            * @param state 状态标识
            * @param desc 状态描述
            * @param cb 信息获取回调函数
           */
           void onState(const std::string& state, const std::string& desc, const IntCallBack& cb)
           {
                onRequest(StatType::STATE, state, desc, [cb] {
                    return utils::format("%ld", cb());
                });
           }

           /**
            * @brief 注册页面信息回调（快捷接口）
            * @param page 页面标识（对应HTTP路径）
            * @param desc 页面描述
            * @param cb 页面内容获取回调函数
           */
           void onPage(const std::string& page, const std::string& desc, const InfoCallBack& cb)
           {
                onRequest(StatType::PAGE, page, desc, cb);
           }

           /**
            * @brief 注册页面文件（将文件内容作为页面响应）
            * @param page 页面标识（对应HTTP路径）
            * @param desc 页面描述
            * @param file 本地文件路径
           */
           void onPageFile(const std::string& page, const std::string& desc, const std::string& file);

           /**
            * @brief 注册命令处理回调（快捷接口）
            * @param cmd 命令标识
            * @param desc 命令描述
            * @param cb 命令处理回调函数
           */
           void onCmd(const std::string& cmd, const std::string& desc, const InfoCallBack& cb)
           {
                onRequest(StatType::CMD, cmd, desc, cb);
           }

           /**
            * @brief 注册数值型命令处理回调（快捷接口）
            * @param cmd 命令标识
            * @param desc 命令描述
            * @param cb 命令结果数值获取回调函数
           */
           void onCmd(const std::string& cmd, const std::string& desc, const IntCallBack& cb)
           {
                onRequest(StatType::CMD, cmd, desc, [cb] 
                {
                    return utils::format("%lld", cb());
                });
           }
        private:
            HttpServer m_server;        // 内部HTTP服务器实例
            // 存储回调函数及描述的结构体
            struct CallBackInfo
            {
                std::string description;    // 回调函数对应的描述信息
                StatCallBack callback;      // 实际处理回调函数
            };

            std::map<std::string, CallBackInfo> m_statCallBacks;    // 状态回调函数映射表
            std::map<std::string, CallBackInfo> m_pageCallBacks;    // 页面回调函数映射表
            std::map<std::string, CallBackInfo> m_cmdCallBacks;     // 命令回调函数映射表
            std::map<std::string, StatCallBack> m_allCallBacks;     // 所有回调函数的统一映射表，用于快速查找
            
    };
} // namespace handy