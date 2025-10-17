#pragma once
#include "port_posix.h"
#include "slice.h"
#include "netinet/in.h"
#include <mutex>
#include <memory>

namespace handy
{
    /**
     * @brief 网络工具类，提供网络字节序转换和Socket选项设置等静态方法
     * @note 该类为纯工具类，不允许实例化，所有方法均为静态方法
     * @note 主要功能：
     * @note 1. 主机字节序与网络字节序的转换
     * @note 2. Socket选项设置（非阻塞、地址重用、端口重用、Nagle算法控制）
    */
    class Net
    {
    public:
        /**
         * @brief 禁止实例化（工具类模式）
        */
        Net() = delete;
        ~Net() = delete;
        Net(const Net &) = delete;
        Net& operator=(const Net &) = delete;

        /**
         * @brief 将主机字节序转换为网络字节序
         * @tparam T 整数类型，仅支持16/32/64位有/无符号整数
         * @param v 主机字节序的整数
         * @return T 转换后的网络字节序整数
         * @note 编译时会检查类型合法性，不支持的类型会触发编译错误
        */
        template <class T>
        static T hton(T v)
        {
            static_assert(
                std::is_same<T, int16_t>::value || std::is_same<T, uint16_t>::value ||
                std::is_same<T, int32_t>::value || std::is_same<T, uint32_t>::value ||
                std::is_same<T, int64_t>::value || std::is_same<T, uint64_t>::value,
                "Net::hton only supports 16/32/64-bit integers"
            );
            return port::htobe(v);
        }

        /**
         * @brief 将网络字节序转换为主机字节序
         * @tparam T 整数类型，仅支持16/32/64位有/无符号整数
         * @param v 网络字节序整数
         * @return T 转换后的主机字节序整数
         * @note 编译时会检查类型合法性，不支持的类型会触发编译错误
        */
        template <class T>
        static T ntoh(T v)
        {
            static_assert(
                std::is_same<T, int16_t>::value || std::is_same<T, uint16_t>::value ||
                std::is_same<T, int32_t>::value || std::is_same<T, uint32_t>::value ||
                std::is_same<T, int64_t>::value || std::is_same<T, uint64_t>::value,
                "Net::ntoh only supports 16/32/64-bit integers"
            );
            return port::betoh(v);
        }

        /**
         * @brief 设置文件描述符的非阻塞模式
         * @param fd 目标文件描述符
         * @param value true：设置为非阻塞模式，false：设置为阻塞模式
         * @param errCode[out] 错误码，为nullptr时表示不输出
         * @return bool 操作成功返回true，失败返回false
        */
        static bool setNonBlock(int fd, bool value = true, int* errCode = nullptr);

        /**
         * @brief 设置Socket的地址重用选项(SO_REUSEADDR)
         * @param fd 目标Socket文件描述符
         * @param value true：允许地址重用，false：不允许地址重用
         * @param errCode[out] 错误码，为nullptr时表示不输出
         * @return bool 操作成功返回true，失败返回false
         * @note 允许绑定到已处于TIME-wait状态的地址，常用于服务器快速重启
        */
        static bool setReuseAddr(int fd, bool value = true, int* errCode = nullptr);

        /**
         * @brief 设置Socket的端口重用选项(SO_REUSEPORT)
         * @param fd 目标Socket文件描述符
         * @param value true：允许端口重用，false：不允许端口重用
         * @param errCode[out] 错误码，为nullptr时表示不输出
         * @return bool 操作成功返回true，失败返回false
         * @note 1. 允许多个进程/线程绑定到同一端口，常用于负载均衡
         * @note 2. 部分平台可能不支持SO_REUSEPORT选项
        */
        static bool setReusePort(int fd, bool value = true, int* errCode = nullptr);

        /**
         * @brief 设置TCP的无延迟选项(TCP_NODELAY)
         * @param fd 目标TCP Socket文件描述符
         * @param value true: 关闭延迟发送，false: 启用延迟发送
         * @param errCode[out] 错误码，为nullptr时不输出
         * @return bool 操作成功返回true，否则返回false\
         * @note 禁用Nagle算法，减少小数据包的传输延迟，但可能增加网络负载
        */
        static bool setNoDelay(int fd, bool value = true, int* errCode = nullptr);
    };

    /**
     * @brief IPv4地址封装类，提供IP地址解析、格式化和合法性校验功能
     * @note 1. 封装了sockaddr_in结构体，提供友好的接口用于处理IPv4地址和端口
     * @note 2. 支持通过主机名、IP字符串或sockaddr_in结构体初始化
    */
    class Ipv4Addr
    {
        private:
            // 内部存储的socketaddr_in结构体(网络字节序)
            struct sockaddr_in m_addr;

            /**
             * @brief 初始化sockaddr_in结构体的辅助函数
             * @param port 端口号（主机字节序）
             * @param ipNetOrder IP地址（网络字节序）
            */
            void initAddr(unsigned short port, uint32_t ipNetOrder);
        public:
            /**
             * @brief 通过主机名和端口初始化IPv4地址
             * @param host 主机名，可以是域名或IP字符串
             * @param port 端口号（主机字节序）
            */
            explicit Ipv4Addr(const std::string& host, unsigned short port);

            /**
             * @brief 通过端口初始化IPv4地址（默认监听所有接口）
             * @param port 端口号（主机字节序），默认为0
             * @note 主机地址将被设置为INADDR_ANY(0.0.0.0)，即监听所有网络接口
            */
            explicit Ipv4Addr(unsigned short port = 0);

            // /**
            //  * @brief 构造函数
            //  * @note 用于外部使用默认构造函数
            // */
            // Ipv4Addr() { Ipv4Addr(0); }

            /**
             * @brief 通过sockaddr_in结构体初始化IPv4地址
             * @param addr 已初始化的sockaddr_in结构体
             * @note 会校验地址族是否为AF_INET，非法地址族将被标记为无效
            */
            explicit Ipv4Addr(const struct sockaddr_in& addr);

            /**
             * @brief 禁止默认构造函数（避免未初始化的无效地址）
            */
            Ipv4Addr() = delete;

            // 拷贝构造函数
            Ipv4Addr(const Ipv4Addr&) = default;
            // 拷贝赋值运算符
            Ipv4Addr& operator=(const Ipv4Addr&) = default;
            // 移动构造函数
            Ipv4Addr(Ipv4Addr&&) noexcept = default;
            // 移动赋值运算符
            Ipv4Addr& operator=(Ipv4Addr&&) noexcept = default;

            /**
             * @brief 获取IP地址和端口的字符串表示
             * @return std::string 格式为"ip:port", 无效地址返回"invalid_ip:0"
            */
            std::string toString() const;

            /**
             * @brief 获取纯IP地址的字符串表示
             * @return std::string 纯IP地址的字符串表示, 无效地址返回"invalid_ip"
            */
            std::string ip() const;

            /**
             * @brief 获取主机字节序的端口号
             * @return unsigned short 主机字节序的端口号
            */
            unsigned short port() const;

            /**
             * @brief 获取主机字节序的IP地址整数表示
             * @return uint32_t 主机字节序的IP地址整数表示，无效地址返回0
            */
            uint32_t ipInt() const;

            /**
             * @brief 检查IP地址是否有效
             * @return bool true:有效，false:无效
            */
            bool isIpValid() const;

            /**
             * @brief 获取内部的sockaddr_in结构体（只读）
             * @return const struct sockaddr_in& 内部存储的sockref_in结构体引用
            */
            const struct sockaddr_in& getAddr() const;

            /**
             * @brief 将主机名解析为IP地址字符串
             * @param host 主机名（域名或IP字符串）
             * @param outIp[out] 存储解析结果的字符串
             * @return bool true:成功，false:失败
            */
            static bool hostToIp(const std::string& host, std::string& outIp);
    };

    /**
     * @brief 线程安全的缓冲区类，用于数据的存储和操作
     * @note 提供安全的内存管理和数据操作接口，支持多线程并发访问
     * @note 主要功能包括：
     * @note 1. 数据的追加、消耗、清空
     * @note 2. 缓冲区的自动扩展和内存碎片整理
     * @note 3. 线程安全的拷贝和移动操作
    */
    class Buffer
    {
        private:
            // 动态分配的内存缓冲区
            char* m_buf;
            // 数据起始偏移量（头部已消耗的部分）
            size_t m_b;
            // 数据结束偏移量（尾部未使用的部分）
            size_t m_e;
            // 缓冲区总容量（字节数）
            size_t m_cap;
            // 期望增长大小（字节数），用于减少内存分配次数
            size_t m_exp;
            // 互斥锁，保证多线程访问的线程安全
            std::unique_ptr<std::mutex> m_mutex;

            /**
             * @brief 确保缓冲区有足够空间容纳指定长度的数据
             * @param len 需要添加的空间大小（字节数）
             * @return char* 指向可写入数据位置的指针
             * @note 内部方法，调用前需先加锁
            */
            char* _makeRoom(size_t len);

            /**
             * @brief 扩展缓冲区容量
             * @param len 至少需要新增的空间大小（字节数）
             * @note 内部方法，调用前需先加锁
            */
            void _expand(size_t len);

            /**
             * @brief 将数据移动到缓冲区头部，减少内存碎片
             * @note 内部方法，调用前需先加锁
            */
            void _moveHead();

            /**
             * @brief 从另一个缓冲区深拷贝数据和状态
             * @param other 被拷贝的缓冲区
             * @note 内部方法，调用前需确保已释放当前资源且加锁
            */
            void _copyFrom(const Buffer& other);

            /**
             * @brief 交换两个缓冲区的资源
             * @param other 要交换的缓冲区
             * @note 内部方法，调用前需加锁
            */
            void _swap(Buffer& other) noexcept;
        public:
            /**
             * @brief 默认构造函数，初始化一个空缓冲区，期望增长大小为512字节
            */
            Buffer();

            /**
             * @brief 析构函数，释放所有动态分配的内存
            */
            ~Buffer();

            /**
             * @brief 拷贝构造函数，线程安全地深拷贝另一个缓冲区的内容
             * @param other 被拷贝的缓冲区
            */
            Buffer(const Buffer& other);

            /**
             * @brief 拷贝赋值运算符，线程安全地深拷贝另一个缓冲区的内容
             * @param other 被拷贝的缓冲区
             * @return Buffer& 当前缓冲区的引用
            */
            Buffer& operator=(const Buffer& other);

            /**
             * @brief 移动构造函数，线程安全地转移另一个缓冲区的资源所有权
             * @param other 被转移的缓冲区
            */
            Buffer(Buffer&& other) noexcept;

            /**
             * @brief 移动赋值运算符，线程安全地转移另一个缓冲区的资源所有权
             * @param other 被转移的缓冲区
             * @return Buffer& 当前缓冲区的引用
            */
            Buffer& operator=(Buffer&& other) noexcept;

            /**
             * @brief 清空缓冲区，释放多有动态内存，重置所有偏移量和容量
            */
            void clear();

            /**
             * @brief 获取当前缓冲区中的数据长度
             * @return size_t 数据长度（字节数）
            */
            size_t size() const;

            /**
             * @brief 判断缓冲区是否为空
             * @return bool true: 空，false: 非空
            */
            bool empty() const;

            /**
             * @brief 获取缓冲区中的数据副本
             * @return std::string 包含缓冲区中所有数据的字符串
             * @note 返回的是数据副本，避免外部直接操作内部数据
            */
            std::string data() const;

            /**
             * @brief 获取缓冲区中的数据开头指针
             * @return char* 指向缓冲区开头的指针
            */
            char* begin() const;

            /**
             * @brief 获取缓冲区中的数据末尾指针
             * @return char* 指向缓冲区中的数据末尾的指针
            */
            char *end() const;

            /**
             * @brief 返回缓冲区中当前剩余的可用空间（字节数）
             * @return size_t 缓冲区中当前剩余的可用空间（字节数）
            */
            size_t space() const;

            /**
             * @brief 返回内部缓冲区的只读指针
             * @return const char* 内部缓冲区的只读指针
             * @note 无法修改内部数据
            */
            const char* peek() const;

            /**
             * @brief 当缓冲区剩余空间小于m_exp时，扩展缓冲区
             * @note 用于按照m_exp扩展缓冲区
            */
            void makeRoom();

            /**
             * @brief 确保缓冲区有足够空间来存储指定长度的数据
             * @return char* 当前最后有效数据的位置指针
            */
            char* makeRoom(size_t len);

            /**
             * @brief 更新缓冲区的实际数据长度
             * @param sz 新增的数据长度
             * @note 用于向缓冲区添加数据后，未更新缓冲区的实际数据长度的场景
            */
            void addSize(size_t len);

            /**
             * @brief 将指定长度的数据追加到缓冲区
             * @param p 指向数据的指针
             * @param len 数据长度（字节数）
             * @return Buffer& 当前缓冲区的引用
            */
            Buffer& append(const char* p, size_t len);

            /**
             * @brief 追加字符串中的数据到缓冲区(未加锁版本，用于在内部函数中调用，防止重复加锁导致的死锁)
             * @param str 包含数据的字符串
             * @return Buffer& 当前缓冲区的引用
            */
            Buffer& appendUnSafe(const char* p, size_t len);
            
            /**
             * @brief 将char*类型的数据追加到缓冲区中
             * @param p 待追加的char*数据
             * @return Buffer& 当前缓冲区的引用
            */
            Buffer& append(const char* p);

            /**
             * @brief 追加Slice中的数据到缓冲区
             * @param slice 包含数据的slice对象
             * @return Buffer& 当前缓冲区的引用
            */
            Buffer& append(Slice slice);

            /**
             * @brief 追加字符串中的数据到缓冲区
             * @param str 包含数据的字符串
             * @return Buffer& 当前缓冲区的引用
            */
            Buffer& append(const std::string& str);

            /**
             * @brief 追加POD类型的值到缓冲区
             * @tparam T POD(Plain Old Data)类型
             * @param v 要追加的值
             * @return Buffer& 当前缓冲区的引用
             * @note 仅支持POD类型的数据，非POD类型数据会触发编译错误
            */
            template <class T>
            Buffer& appendValue(const T& v)
            {
                static_assert(std::is_pod<T>::value, "Buffer::appendValue only support POD types");
                return append(reinterpret_cast<const char*>(&v), sizeof(T));
            }

            /**
             * @brief 从头部消耗指定长度的数据
             * @param len 要消耗的数据长度（字节数）
             * @return Buffer& 当前缓冲区的引用
             * @note 若len带当前数据长度，会消耗所有数据
            */
            Buffer& consume(size_t len);

            /**
             * @brief 合并另一个缓冲区的内容（将另一个缓冲区的所有数据追加到当前缓冲区，并清空另一个缓冲区）
             * @param other 要合并的缓冲区
             * @return Buffer& 当前缓冲区的引用
            */
            Buffer& absorb(Buffer& other);

            /**
             * @brief 设置缓冲区的期望增长大小
             * @param sz 期望增长大小（字节数）
             * @note 当需要扩展缓冲区时，会按照此大小进行扩展，减少内存分配的次数
            */
            void setExpectGrowSize(size_t sz);

            /**
             * @brief 转换为Slice对象
             * @return Slice 包含缓冲区数据的临时Slice对象
             * @note 返回的Slice对象仅在当前语句有效，请勿保存引用
            */
            operator Slice() const;
    };
} // namespace handy