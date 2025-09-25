#pragma once
#include "net.h"
#include "slice.h"

namespace handy
{
    /**
     * @brief 编解码器基类（线程安全）
     * @note 提供消息编解码同意接口，所有子类需实现线程安全的编解码逻辑
    */
    class CodecBase
    {
        public: 
            // 禁止拷贝构造和赋值（避免多线程下的浅拷贝风险）
            CodecBase(const CodecBase&) = delete;
            CodecBase& operator=(const CodecBase&) = delete;

            // 允许移动构造和赋值（资源高效转移）
            CodecBase(CodecBase&&) noexcept = default;
            CodecBase& operator=(CodecBase&&) noexcept = default;

            virtual ~CodecBase() = default;

            /**
             * @brief 尝试对消息进行解码
             * @param data 待解码的原始数据
             * @param[out] msg 解码后的完整消息，当且仅当返回值>0时有效
             * @return int >0：解码成功，返回值表示解码的消息长度；0: 数据不完整，需继续读取；<0: 解码失败
            */
            virtual int tryDecode(Slice data, Slice& msg) = 0;

            /**
             * @brief 对消息进行编码
             * @param msg 待编码的消息
             * @param[out] buf 编码后的数据将追加到buf中
            */
            virtual void encode(Slice msg, Buffer& buf) = 0;

            /**
             * @brief 克隆编解码器（多态拷贝，用于线程独立实例）
             * @return CodecBase* 新的编解码器实例（调用者需要负责释放）
            */
            virtual CodecBase* clone() const = 0;
        protected:
            // 基类构造函数（仅允许子类调用）
            CodecBase() = default;

            // 线程安全锁（保护编解码器内部状态，子类可直接使用）
            mutable std::mutex m_mutex;
    };

    /**
     * @brief 换行分隔符编解码器(\r\n或\n)
     * @note 支持两种换行格式：标准HTTP风格式(\r\n)和Unix格式(\n)，自动兼容
    */
    class LineCodec : public CodecBase
    { 
        public: 
            // 解码错误码（负数表示）
            enum class DecodeErr
            {
                kInvalidEot = -1, // 无效的EOT结束符(仅允许单独0x04)
            };

            int tryDecode(Slice data, Slice& msg) override;
            void encode(Slice msg, Buffer& buf) override;
            CodecBase* clone() const override { return new LineCodec(); }
        private:
            // 检查EOT结束符的合法性（防止0x04混入普通数据）
            bool isLegalEot(Slice data) const;
    };

    /**
     * @brief 长度前缀编解码器（固定8字节头：4字节魔法字+ 4字节长度）
     * @note 格式：[mBdT(4字节)][length(4字节大端序)][payload(length字节)]
     * @note 安全限制：单条消息最大长度为1MB，避免内存溢出
    */
    class LengthCodec : public CodecBase
    {
        public:
            // 解码错误码（负数表示）
            enum class DecodeErr {
                kInvalidMagic = -1,  // 无效的魔法字（仅允许"mBdT"）
                kInvalidLength, // 无效的消息长度
            };
            // 魔法字（用于校验消息合法性）
            static constexpr const char* kMagic = "mBdT";
            // 固定头部长度(4字节魔法字+ 4字节长度)
            static constexpr size_t kHeaderLen = 8;
            // 单条消息最大长度限制(1MB, 可通过setter调整)
            static constexpr size_t kDefaultMaxMsgLen = 1024 * 1024;

            LengthCodec() : m_maxMsgLen(kDefaultMaxMsgLen) {}

            /**
             * @brief 设置单条消息最大长度限制（线程安全）
             * @param maxLen 最大长度限制（字节）
            */
            void setMaxMsgLen(size_t maxLen)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if(maxLen == 0)
                    throw std::invalid_argument("max message length cannot be 0");
                m_maxMsgLen = maxLen;
            }

            /**
             * @brief 获取当前最大消息长度（线程安全）
             * @return size_t 最大消息长度（字节）
            */
            size_t getMaxMsgLen() const
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                return m_maxMsgLen;
            }

            /**
             * @brief 获取当前最大消息长度（非线程安全）
             * @return size_t 最大消息长度（字节）
            */
            size_t getMaxMsgLenUnSafe() const
            {
                return m_maxMsgLen;
            }

            int tryDecode(Slice data, Slice& msg) override;
            void encode(Slice msg, Buffer& buf) override;
            CodecBase* clone() const override { return new LengthCodec(); }
        private:
            // 检查魔法字的合法性
            bool checkMagic(const char* header) const;
            // 单条消息最大长度（线程安全访问）
            size_t m_maxMsgLen;
    };
} // namespace handy
