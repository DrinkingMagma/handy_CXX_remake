# Handy 网络库重构项目

**原项目地址**: <https://github.com/yedf2/handy>

本项目是对原Handy网络库的现代化重构，采用C++17标准，提供更清晰的代码结构、更完善的注释和更丰富的示例。

***

## 项目概述

Handy是一个基于事件驱动的轻量级C++网络库，采用Reactor模式实现，底层使用epoll(Linux)或kqueue(BSD/macOS)实现I/O多路复用。本重构项目保留了原库的核心设计理念，同时优化了代码组织和可读性。

### 核心特性

- **事件驱动架构**: 基于Reactor模式，单线程或多线程事件循环
- **非阻塞I/O**: 所有socket操作均为非阻塞，提升并发性能
- **智能指针管理**: 使用`std::shared_ptr`和`std::unique_ptr`自动管理资源
- **线程安全**: 关键操作通过互斥锁保护，支持多线程环境
- **编解码支持**: 内置LineCodec、LengthCodec等常用编解码器
- **半同步半异步(HSHA)**: 支持I/O线程与业务线程分离的高并发模型
- **跨平台**: 支持Linux和类Unix系统

***

## 目录结构

```
/root/C++/handy_CXX_remake/
├── handy/                      # 核心库源码目录
│   ├── conn.h / conn.cpp       # TCP连接管理(TcpConn, TcpServer, HSHA)
│   ├── event_base.h / .cpp     # 事件循环核心(EventBase, Channel, Poller)
│   ├── codec.h / .cpp          # 消息编解码器(LineCodec, LengthCodec)
│   ├── thread_pool.h / .cpp    # 线程池实现
│   ├── net.h / .cpp            # 网络工具函数
│   ├── logger.h / .cpp         # 日志系统
│   ├── buffer.h                # 线程安全缓冲区
│   ├── status.h                # 系统状态工具
│   ├── conf.h / .cpp           # 配置文件解析
│   ├── udp.h / .cpp            # UDP支持
│   └── handy-imp.h             # 内部实现头文件
│
├── examples/                   # 示例程序目录
│   ├── tcp/                    # TCP基础示例
│   │   ├── client.cpp          # TCP客户端示例
│   │   └── server.cpp          # TCP服务端示例
│   │
│   ├── HSHA/                   # 半同步半异步服务器示例
│   │   ├── client.cpp          # HSHA客户端
│   │   └── server.cpp          # HSHA服务端(多线程)
│   │
│   ├── HTTP/                   # HTTP服务器示例
│   │   ├── client.cpp
│   │   └── server.cpp
│   │
│   ├── udp/                    # UDP通信示例
│   │   ├── client.cpp
│   │   └── server.cpp
│   │
│   ├── high_concurrency/       # 高并发测试示例
│   │   ├── client.cpp
│   │   └── server.cpp
│   │
│   ├── chat.cpp                # 聊天室示例
│   ├── timer.cpp               # 定时器示例
│   └── reconnect.cpp           # 自动重连示例
│
├── tests/                      # 单元测试目录
│   └── test.cpp
│
├── build/                      # 构建输出目录
│
├── CMakeLists.txt              # CMake构建配置
└── README.md                   # 本文件
```

***

## 核心模块详解

### 1. 事件驱动核心层 (event\_base.h/cpp)

**EventBase**: 事件循环引擎，协调I/O事件、定时器和异步任务

- `loop()`: 启动事件循环
- `exit()`: 安全退出事件循环
- `safeCall()`: 跨线程任务投递

**Channel**: I/O通道封装，绑定文件描述符与事件回调

- 管理socket的读写事件监听
- 触发用户注册的回调函数

**Poller**: I/O多路复用抽象(epoll/kqueue)

- `addChannel()`: 注册通道
- `removeChannel()`: 移除通道
- `loopOnce()`: 等待并分发事件

### 2. TCP连接管理层 (conn.h/cpp)

**TcpConn**: 单个TCP连接封装

- 状态管理: INVALID → HAND\_SHAKING → CONNECTED → CLOSED
- 数据缓冲: 输入/输出缓冲区自动管理
- 编解码集成: 支持绑定Codec自动处理消息边界

**TcpServer**: TCP服务器

- 端口监听与连接接受
- 多EventBase负载均衡(多线程)
- 回调注册: onConnCreate, onConnState, onConnRead, onConnMsg

**HSHA**: 半同步半异步服务器

- I/O线程: 异步处理网络事件
- 工作线程池: 同步处理业务逻辑
- 通过safeCall将结果回传到I/O线程发送

### 3. 编解码层 (codec.h/cpp)

**CodecBase**: 编解码器基类

- `tryDecode()`: 尝试从缓冲区解码消息

**LineCodec**: 行分隔编解码器(以\n分隔)
**LengthCodec**: 长度前缀编解码器(4字节长度头)

### 4. 线程池 (thread\_pool.h/cpp)

**ThreadPool**: 固定大小线程池

- `addTask()`: 提交任务
- `exit()`: 通知退出
- `join()`: 等待所有线程结束

**SafeQueue**: 线程安全任务队列

- 生产者-消费者模式
- 条件变量实现阻塞等待

### 5. 工具模块

**Buffer**: 线程安全动态缓冲区
**Logger**: 多级别日志系统(DEBUG/INFO/WARN/ERROR/FATAL)
**Net**: 网络工具函数(地址转换、socket选项设置等)
**Ipv4Addr**: IPv4地址封装

***

## 使用示例

### TCP Echo服务器

```cpp
#include "conn.h"
using namespace handy;

int main() {
    EventBase base;
    
    auto server = TcpServer::startServer(&base, "127.0.0.1", 12345);
    server->onConnMsg(std::make_unique<LineCodec>(), 
        [](const TcpConnPtr& conn, const Slice& msg) {
            conn->send(msg);  // Echo回显
        });
    
    base.loop();
    return 0;
}
```

### TCP客户端

```cpp
#include "conn.h"
using namespace handy;

int main() {
    EventBase base;
    
    auto conn = TcpConn::createConnection(&base, "127.0.0.1", 12345);
    conn->onMsg(std::make_unique<LineCodec>(),
        [](const TcpConnPtr& conn, const Slice& msg) {
            info("Received: %s", msg.c_str());
        });
    conn->send("hello\n");
    
    base.loop();
    return 0;
}
```

### HSHA服务器(支持耗时操作)

```cpp
#include "conn.h"
using namespace handy;

int main() {
    EventBase base;
    
    // 4个工作线程
    auto hsha = HSHA::startServer(&base, "127.0.0.1", 12345, 4);
    hsha->onMsg(std::make_unique<LineCodec>(),
        [](const TcpConnPtr& conn, const std::string& msg) {
            // 在工作线程中执行，可以阻塞
            std::this_thread::sleep_for(std::chrono::seconds(1));
            return "Processed: " + msg;
        });
    
    base.loop();
    return 0;
}
```

***

## 构建与运行

### 依赖

- C++17兼容编译器(GCC 7+/Clang 5+)
- CMake 3.10+
- Linux或类Unix系统

### 编译

```bash
cd /root/C++/handy_CXX_remake
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

### 运行示例

```bash
# 运行TCP服务端
cd build/examples/tcp
./server &

# 运行TCP客户端
./client

# 运行HSHA服务端(4工作线程)
cd ../HSHA
./server 4 &

# 运行HSHA客户端
./client
```

***

## 许可证

本项目遵循与原Handy库相同的开源许可证。

***

