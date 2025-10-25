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
            // 消息解析结果状态
            enum class Result
            {
                Error,          // 解析错误
                Complete,       // 解析完成
                NotComplete,    // 解析未完成（需要更多数据）
                Continue100     // 需要发送100Continue响应
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
            virtual Result tryDecode(Slice buf, bool isCopyBody=true) 0;

            /**
             * @brief 清空消息所有字段，恢复初始状态
            */
            virtual void clear();

            /**
             * @brief 获取指定头部字段的值
             * @param name 头部字段名（不区分大小写）
             * @return std::string 头部字段值（空字符串表示未找到）
            */
            std::string getHeader(const std::string& name) const
            {
                return getValueFromMap(m_headers, name);
            }
        protected:
            std::map<std::string, std::string> m_headers;   // 头部字段（键为小写）
            std::string m_version;                          // HTTP版本（如"HTTP/1.1"）
            std::string body;                               // 消息体(复制模式)
            Slice m_body2;                                  // 消息体（引用模式）
            bool m_completed;                               // 消息解析完成标志
            size_t m_contentLen;                            // 消息体长度（从Content-Length中获取）
            size_t m_scannedLen;                            // 已解析的字节数

            /**
             * @brief 内部解析辅助函数，处理通用HTTP消息结构
             * @param buf 输入缓冲区
             * @param isCopyBody 是否复制消息体
             * @param [out] line1 存储消息的第一行（请求行/状态行）
             * @return Result 解析状态 
            */
            Result _tryDecode(Slice buf, bool isCopyBody, Slice& line1);

            /**
             * @brief 从映射表中获取指定键的值（不区分大小写）
             * @param map 键值对映射表
             * @param name 键名
             * @return std::string 键对应的值（空字符串表示未找到）
            */
            std::string _getValueFromMap(const std::map<std::string, std::string>& map,
                                            const std::string& name) const;

    };
} // namespace handy
