### 针对handy网络库的重写
原GITHUB链接：https://github.com/yedf2/handy

#### 1. 日志模块 (Logger)

- [x] 对应 handy/logging.cpp 功能实现
- [x] 支持日志级别控制（与原项目 setloglevel 兼容）
- [ ] 日志文件滚动（参考 daemon.cpp 中的日志配置逻辑）
- [x] 多线程安全输出保障

#### 2. 基础工具模块 (难度: ★☆☆☆☆)

- [x] utils.h/utils.cpp
  - [x] 实现字符串格式化函数（参考 util::format 实现）
  - [x] 系统工具函数（时间转换、错误处理等）
  - [x] 非拷贝基类（noncopyable）
- [x] slice.h
  - [x] 轻量级字符串视图实现（支持 slice 相关操作）
- [x] conf.h/conf.cpp
  - [x] 配置文件解析功能（兼容 test/files 中的 ini 格式）
  - [x] 键值对读取接口（参考 daemon.cpp 中的配置读取逻辑）

#### 3. 事件循环核心 (难度: ★★★☆☆)

- [x] event_base.h/event_base.cpp
  - [x] 事件循环基类实现（EventBase 核心逻辑）
  - [x] 定时器管理（runAfter/runAt/cancel，参考 timer.cpp）
  - [x] 信号处理（Signal::signal，参考示例中的信号处理）
- [x] poller.h/poller.cpp
  - [x] 跨平台 I/O 多路复用封装
    - [x] Linux: epoll（参考 raw-examples/epoll.cpp）
    - [x] MacOS: kqueue（参考 raw-examples/kqueue.cpp）
- [x] port_posix.h/port_posix.cpp
  - [x] 平台相关系统调用封装（兼容 Linux/MacOS）

#### 4. 网络连接基础 (难度: ★★★☆☆)

- [x] conn.h/conn.cpp
  - [x] TCP 连接基类（TcpConn，参考 echo.cpp 连接逻辑）
  - [x] 连接状态管理（Connected/Closed 等状态处理）
  - [x] 数据发送/接收缓冲区（参考 Buffer 类实现）
- [x] net.h/net.cpp
  - [x] 地址解析、套接字操作封装
  - [ ] TCP 服务器基类（TcpServer，参考 echo.cpp 服务器实现）

#### 5. 协议编解码 (难度: ★★☆☆☆)

- [x] codec.h/codec.cpp
  - [x] 行协议编解码器（LineCodec，参考 hsha.cpp）
  - [x] 长度前缀协议编解码器（LengthCodec，参考 codec-svr.cpp）
- [ ] protobuf 支持（参考 protobuf 目录）
  - [ ] ProtoMsgCodec 实现（消息序列化/反序列化）
  - [ ] 与 protobuf 库的集成（.proto 文件处理）

#### 6. 高级网络功能 (难度: ★★★★☆)

- [x] udp.h/udp.cpp
  - [x] UDP 连接封装（参考 udp-cli.cpp/udp-svr.cpp）
  - [x] UDP 服务器实现（UdpServer）
- [ ] http.h/http.cpp
  - [ ] HTTP 协议解析与封装
  - [ ] HTTP 服务器基础功能（参考 http-hello.cpp）

#### 7. 扩展模块 (难度: ★★★☆☆)

- [x] threads.h/threads.h
  - [x] 线程池实现
  - [x] 半同步半异步模式（HSHA）支持（参考 hsha.cpp/udp-hsha.cpp）
- [ ] stat-svr.h/stat-svr.cpp
  - [ ] 状态监控服务器（参考 stat.cpp）
  - [ ] 页面展示接口（onPage 函数实现）
- [x] daemon.h/daemon.cpp
  - [x] 守护进程模式支持（参考 daemon.cpp）
  - [x] 进程管理（启动/停止/重启）

#### 8. 文件操作模块 (难度: ★★☆☆☆)

- [ ] file.h/file.cpp
  - [ ] 文件读写操作封装（参考 file::writeContent 实现）
  - [ ] 文件状态检查与处理

#### 9. 示例与测试 (难度: ★★☆☆☆)

- [ ] 核心示例移植
  - [ ] echo.cpp（基础回显服务）
  - [ ] timer.cpp（定时器示例）
  - [ ] chat.cpp（聊天服务器）
  - [ ] http-hello.cpp（HTTP 服务示例）
- [ ] 测试用例完善
  - [ ] 配置文件解析测试（基于 test/files）
  - [ ] 网络功能单元测试