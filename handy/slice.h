#pragma once
#include <stdexcept>
#include <cstring>
#include <vector>
#include <mutex>
#include <unistd.h>

namespace handy
{
    /**
     * @brief 非持有型字符序列试图（类似std::string_view，但兼容C++11）
     * @note 1. 不管理内存，仅持有外部字符序列的指针，需确保外部数据生命周期有效
     * @note 2. 线程安全：成员函数均为const/无状态操作，多线程只读访问安全
     * @note 3. 若需多线程修改（如eat/trimSpace），需外部加锁
     * @note 4. 禁止空指针访问，所有构造/操作均做空指针校验
    */
    class Slice
    {
        public:
            // 静态常量：表示“未找到”的位置
            static constexpr size_t npos = static_cast<size_t>(-1);

            // -------------------------- 构造函数（严格空指针校验） --------------------------
            /**
             * @brief 默认构造：空视图
            */
            Slice() noexcept : m_pb(kEmptyData), m_pe(kEmptyData) {}

            /**
             * @brief 从[b, e)区间构造（左闭右开）
             * @param b 起始指针，不可为nullptr
             * @param e 结束指针，不可为nullptr且e >= b
             * @throw std::invalid_argument 如果b或e为nullptr或e < b，则抛出此异常
            */
            Slice(const char* b, const char* e)
            {
                if(b == nullptr || e == nullptr)
                {
                    throw std::invalid_argument("Slice: b/e cannot be nullptr");
                }
                else if(e < b)
                {
                    throw std::invalid_argument("Slice: e cannot be less than b");
                }

                m_pb = b;
                m_pe = e;
            }

            /**
             * @brief 从指针+长度构造
             * @param d 数据指针（不可为 nullptr，空视图需传 kEmptyData）
             * @param n 长度（不可为负数）
             * @throw std::invalid_argument 若 d 为空且 n > 0
            */
            Slice(const char* d, size_t n)
            {
                if(d == nullptr)
                {
                    if(n > 0)
                    {
                        throw std::invalid_argument("Slice: d cannot be nullptr when n > 0");
                    }
                    m_pb = kEmptyData;
                    m_pe = kEmptyData;
                }
                else
                {
                    m_pb = d;
                    m_pe = d + n;
                }
            }

            /**
             * @brief 从 std::string 构造（持有 string 的 data() 指针）
             * @note 需确保 string 生命周期长于 Slice
            */
            Slice(const std::string& s) noexcept : m_pb(s.data()), m_pe(s.data() + s.size()) {}

            /**
             * @brief 从 C 风格字符串构造（自动计算长度）
             * @param s 以 '\0' 结尾的字符串（不可为 nullptr）
             * @throw std::invalid_argument 若 s 为空
            */
            Slice(const char* s)
            {
                if(s == nullptr)
                {
                    throw std::invalid_argument("Slice: s cannot be nullptr");
                }
                m_pb = s;
                m_pe = s + std::strlen(s);
            }

            // 是否禁止拷贝/移动
            Slice(const Slice&) = default;
            Slice& operator=(const Slice&) = default;
            Slice(Slice&&) = default;
            Slice& operator=(Slice&&) = default;

            // -------------------------- 基础访问接口（const 安全） --------------------------
            /**
             * @brief 获取数据起始指针（非空）
            */
            const char* data() const noexcept { return m_pb; }

            /**
             * @brief 获取起始迭代器（兼容STL算法）
            */
            const char* begin() const noexcept { return m_pb; }

            /**
             * @brief 获取结束迭代器（兼容STL算法）
            */
            const char* end() const noexcept { return m_pe; }

            /**
             * @brief 获取第一个字符
             * @throw std::out_of_range 若视图为空
            */
            char front() const
            {
                if(empty())
                {
                    throw std::out_of_range("Slice: front() on epmty slice");
                }
                return *m_pb;
            }

            /**
             * @brief 获取最后一个字符
             * @throw std::out_of_range 若视图为空
            */
            char back() const
            {
                if(empty())
                {
                    throw std::out_of_range("Slice: back() on epmty slice");
                }
                return *(m_pe - 1);
            }

            /**
             * @brief 获取视图长度
            */
            size_t size() const noexcept { return static_cast<size_t>(m_pe - m_pb); }

            /**
             * @brief 判断视图是否为空
            */
            bool empty() const noexcept { return m_pb == m_pe; }

            // -------------------------- 视图修改接口（非线程安全，需外部同步） --------------------------
            /**
             * @brief 调整视图长度（仅允许缩小，避免越界）
             * @param sz 目标长度
             * @throw std::out_of_range 若sz > 当前 size()
            */
            void resize(size_t sz)
            {
                if(sz > size())
                {
                    throw std::out_of_range("Slice: resize() exceeds current size");
                }
                m_pe = m_pb + sz;
            }

            /**
             * @brief 清空试图（重置为空视图）
            */
            void clear() noexcept
            {
                m_pb = kEmptyData;
                m_pe = kEmptyData;
            }

            /**
             * @brief 吞噬前sz个字符（从视图中移除并返回）
             * @param sz 吞噬长度
             * @return 返回被吞噬的子视图
             * @throw std::out_of_range 若sz > 当前size()
            */
            Slice eat(size_t sz)
            {
                if(sz > size())
                {
                    throw std::out_of_range("Slice: eat() exceeds current size");
                }
                Slice res(m_pb, sz);
                m_pb += sz;
                return res;
            }

            /**
             * @brief 查找字符位置
             * @param ch 要查找的字符
             * @return size_t 位置索引，若未找到返回 npos
            */
            size_t find(char ch) const noexcept
            {
                const char* p = m_pb;
                while(p < m_pe)
                {
                    if(*p == ch)
                    {
                        return static_cast<size_t>(p - m_pb);
                    }
                    ++p;
                }
                return npos;
            }

            /**
             * @brief 吞噬一个单词（跳过前导空白，知道下一个空白)
             * @return 单词的子视图（无空白）
            */
            Slice eatWord() noexcept
            {
                // 跳过前导空白（isspace需处理unsigned char，避免符号错误）
                const char* b = m_pb;
                while(b < m_pe && std::isspace(static_cast<unsigned char>(*b)))
                    ++b;

                // 读取单词内容
                const char* e = b;
                while(e < m_pe && !std::isspace(static_cast<unsigned char>(*e)))
                    ++e;

                // 更新当前视图
                m_pb = e;
                return Slice(b, e);
            }

            /**
             * @brief 吞噬一行（直到 \n 或 \r，兼容 Windows/Linux 换行）
             * @return 行内容的子视图（不含换行符）
            */
            Slice eatLine() noexcept
            {
                const char* p = m_pb;
                // 跳过非换行字符
                while(m_pb < m_pe && *m_pb != '\n' && *m_pb != '\r')
                    ++m_pb;
                // 跳过换行符本身（避免残留）
                if(m_pb < m_pe && (*m_pb == '\n' || *m_pb == '\r'))
                {
                    ++m_pb;
                    // 处理Windows 换行（\r\n）
                    if(m_pb < m_pe && *m_pb == '\n' && *(m_pb - 1) == '\r')
                        ++m_pb;
                } 

                // 不含换行符
                return Slice(p, m_pb - 1);
            }
            
            /**
             * @brief 截取子视图（支持负偏移）
             * @param bOff 起始偏移（正数：从开头；负数：从结尾）
             * @param eOff 结束偏移（正数：从开头；负数：从结尾，默认 0 表示原结束）
             * @return 子视图
             * @throw std::out_of_range 若偏移越界
            */
            Slice sub(int bOff, int eOff = 0) const
            {
                // 计算实际起始位置
                const char* b = m_pb;
                if(bOff >= 0)
                    b += bOff;
                else
                    b += size() + bOff;

                // 计算实际结束位置
                const char* e = m_pe;
                if(eOff >= 0)
                    e = m_pb + eOff;
                else
                    e = m_pe + eOff;

                // 校验边界
                if(b < m_pb || b > e || e > m_pe)
                {
                    throw std::out_of_range("Slice: sub() offset out of range");
                }
                return Slice(b, e);
            }

            /**
             * @brief 移除前后空白字符（修改当前视图）
             * @return 自身应用（支持链式调用）
            */
            Slice& trimSpace() noexcept
            {
                // 移除前导空白
                while(m_pb < m_pe && std::isspace(static_cast<unsigned char>(*m_pb)))
                    ++m_pb;

                // 移除尾部空白
                while(m_pe > m_pb && std::isspace(static_cast<unsigned char>(*(m_pe - 1))))
                    --m_pe;
                return *this;
            }

            // -------------------------- 比较与转换接口 --------------------------
            /**
             * @brief 下标访问（支持随机访问）
             * @param n 索引
             * @return 对应字符
             * @throw std::out_of_range 若索引越界
            */
            char operator[](size_t n) const
            {
                if(n >= size())
                {
                    throw std::out_of_range("Slice: operator[] out of range");
                }
                return m_pb[n];
            }

            /**
             * @brief 转换为std::string
            */
            std::string toString() const
            {
                return std::string(m_pb, size());
            }

            /**
             * @brief 隐式转换为std::string（兼容字符串场景）
            */
            operator std::string() const { return toString(); }

            /**
             * @brief 三段式比较（兼容排序）
             * @param b 待比较的Slice
             * @return 0:相等，<0:小于，>0:大于
            */
            int compare(const Slice& b) const noexcept
            {
                const size_t minLen = std::min(size(), b.size());
                // 比较前minLen个字符(memcmp 处理二进制安全)
                const int cmp = std::memcmp(m_pb, b.m_pb, minLen);
                if(cmp != 0)
                    return cmp;

                // 长度不同时，短的更小
                return static_cast<int>(size() - b.size());
            }

            /**
             * @brief 判断是否以指定前缀开头
             * @param prefix 前缀视图
            */
            bool startsWith(const Slice& prefix) const noexcept
            {
                return (size() >= prefix.size() &&
                        (std::memcmp(m_pb, prefix.m_pb, prefix.size()) == 0));
            }

            /**
             * @brief 判断是否以指定后缀结尾
             * @param suffix 后缀视图
            */
            bool endsWith(const Slice& suffix) const noexcept
            {
                return (size() >= suffix.size() &&
                        (std::memcmp(m_pe - suffix.size(), suffix.m_pb, suffix.size()) == 0));
            }

            /**
             * @brief 按指定字符分割视图
             * @param ch 分割字符
             * @return 分割后的视图列表（不含分割符）
            */
            std::vector<Slice> split(char ch) const noexcept
            {
                std::vector<Slice> res;
                const char* cur = m_pb;
                // 遍历视图查找分割符
                for(const char* p = m_pb; p < m_pe; ++p)
                {
                    if(*p == ch)
                    {
                        res.emplace_back(cur, p);
                        cur = p + 1;
                    }
                }

                // 添加最后一个子视图（若不为空）
                if(cur < m_pe)
                    res.emplace_back(cur, m_pe);
                
                return res;
            }

            // -------------------------- 线程安全辅助接口（可选） --------------------------
            /**
             * @brief 获取全局空数据（避免空指针）
            */
            static const char* getEmptyData() noexcept { return kEmptyData; }

            /**
             * @brief 线程安全的Slice构造（多线程创建时避免空指针竞争）
             * @note 需外部保证data生命周期
            */
            static Slice createSafe(const char* data, size_t len)
            {
                static std::mutex mutex;
                std::lock_guard<std::mutex> lock(mutex);
                return Slice(data, len);
            }
        private:
            // 全局空数据（避免空指针，所有空视图指向此处）
            static constexpr const char* kEmptyData = "";

            // 起始指针（非空）
            const char* m_pb;
            // 结束指针(m_pe >= m_pb)
            const char* m_pe;
    };
    // -------------------------- 全局比较运算符（非成员函数） --------------------------
    inline bool operator<(const Slice& lhs, const Slice& rhs) noexcept
    {
        return lhs.compare(rhs) < 0;
    }

    inline bool operator==(const Slice &lhs, const Slice &rhs) noexcept
    {
        return lhs.compare(rhs) == 0;
    }

    inline bool operator!=(const Slice &lhs, const Slice &rhs) noexcept
    {
        return !(lhs == rhs);
    }

    inline bool operator<=(const Slice &lhs, const Slice &rhs) noexcept
    {
        return lhs.compare(rhs) <= 0;
    }

    inline bool operator>(const Slice &lhs, const Slice &rhs) noexcept
    {
        return lhs.compare(rhs) > 0;
    }

    inline bool operator>=(const Slice &lhs, const Slice &rhs) noexcept
    {
        return lhs.compare(rhs) >= 0;
    }

    // -------------------------- 辅助函数（增强易用性） --------------------------
    /**
     * @brief 从文件读取内容到Slice（需确保buffer的生命周期）
     * @param fd 文件描述符（已打开）
     * @param buffer 外部缓冲区（需足够大）
     * @param bufLen 缓冲区大小
     * @return 读取到的内容视图
     * @throw std::runtime_error 若读取失败
     * @throw std::invalid_argument 若buffer为nullptr或bufLen为0
    */
    inline Slice readFromFd(int fd, char* buffer, size_t bufLen)
    {
        if(buffer == nullptr || bufLen == 0)
        {
            throw std::invalid_argument("readFromFd: buffer is invalid");
        }

        const ssize_t n = read(fd, buffer, bufLen);
        if(n < 0)
        {
            throw std::runtime_error("readFromFd: read failed, errno=" + std::to_string(errno));
        }

        return Slice(buffer, static_cast<size_t>(n));
    }
} // namespace handy