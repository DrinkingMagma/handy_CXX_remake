#include "codec.h"
#include <iostream>

namespace handy
{
    bool LineCodec::isLegalEot(Slice data) const
    {
        // EOT(0x04)必须单独作为一条消息，不能与其他数据共存
        return (data.size() == 1 && static_cast<uint8_t>(data[0]) == 0x04);
    }

    int LineCodec::tryDecode(Slice data, Slice& msg)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // 1. 优先检查EOT结束符（传输结束标记）
        if(!data.empty() && isLegalEot(data))
        {
            msg = data;
            return 1;
        }

        // 寻找换行符(\r\n或\n)
        for(size_t i = 0; i < data.size(); ++i)
        {
            if(data[i] == '\n')
            {
                if(i > 0 && data[i - 1] == '\r')
                {
                    msg = Slice(data.data(), i - 1);
                    return static_cast<int>(i + 1); // 包含\r\n
                }
                else
                {
                    msg = Slice(data.data(), i);
                    return static_cast<int>(i + 1); // 仅包含\n
                }
            }
        }

        return 0;
    }

    void LineCodec::encode(Slice msg, Buffer& buf)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // 检查消息是否包含换行符（避免编码后混淆消息边界）
        if(msg.find('\n') != Slice::npos)
        {
            throw std::invalid_argument("LineCodec: message contains '\\n' which will break line codec");
        }

        // 标准格式：消息体 + \r\n
        buf.append(msg).append("\r\n");
    }

    bool LengthCodec::checkMagic(const char* header) const
    {
        return memcmp(header, kMagic, strlen(kMagic)) == 0;
    }

    int LengthCodec::tryDecode(Slice data, Slice& msg)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // 1. 检查头部是否为足够
        if(data.size() < kHeaderLen)
            return 0;

        // 2. 校验消息合法性
        if(!checkMagic(data.data()))
            return static_cast<int>(LengthCodec::DecodeErr::kInvalidMagic);

        // 3. 解析长度（网络字节序转换为主机字节序，避免类型转换溢出）
        int32_t netLen = 0;
        // 安全拷贝4字节长度（避免直接强转导致的内存对齐问题）
        memcpy(&netLen, data.data() + 4, sizeof(netLen));
        int32_t hostLen = Net::ntoh(netLen);

        // 4. 检查长度合法性（避免负数、0、超最大限制）
        size_t maxLen = getMaxMsgLenUnSafe();
        if(hostLen <= 0 || static_cast<size_t>(hostLen) > maxLen)
            return static_cast<int>(LengthCodec::DecodeErr::kInvalidLength);

        // 5. 检查数据是否完整（总长度 = 头部长度 + 消息长度）
        size_t totalNeeded  = kHeaderLen + static_cast<size_t>(hostLen);
        if(data.size() < totalNeeded)
            return 0;

        // 6. 提取消息（确保Slice不越界）
        msg = Slice(data.data() + kHeaderLen, static_cast<size_t>(hostLen));
        return static_cast<int>(totalNeeded);
    }

    void LengthCodec::encode(Slice msg, Buffer& buf)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // 1. 检查消息长度是否超出限制
        size_t msgLen = msg.size();
        size_t maxLen = getMaxMsgLenUnSafe();
        constexpr size_t MAX_INT32 = static_cast<size_t>(INT32_MAX);
        if(msgLen > maxLen || msgLen > MAX_INT32)
        {
            throw std::out_of_range(
                "message length " + std::to_string(msgLen) + 
                " is out of range, max length is " + std::to_string(maxLen)
            );
        }

        // 2. 编码头部（魔法字+大端序长度）
        int32_t netLen = Net::hton(static_cast<int32_t>(msgLen));
        buf.append(kMagic).appendValue(netLen).append(msg);
    }
} // namespace handy
