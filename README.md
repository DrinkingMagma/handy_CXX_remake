### 针对handy网络库的重写
原GITHUB链接：https://github.com/yedf2/handy

#### 日志模块 (Logger)

- [x] 对应 handy/logging.cc 功能实现
- [x] 支持日志级别控制（与原项目 setloglevel 兼容）
- [ ] 日志文件滚动（参考 daemon.cc 中的日志配置逻辑）
- [x] 多线程安全输出保障

#### 1. 基础工具模块 (难度: ★☆☆☆☆)

- [ ] util.h/util.cc
  - [ ] 实现字符串格式化函数（参考 util::format 实现）
  - [ ] 系统工具函数（时间转换、错误处理等）
  - [ ] 非拷贝基类（noncopyable）
- [ ] slice.h
  - [ ] 轻量级字符串视图实现（支持 slice 相关操作）
- [ ] conf.h/conf.cc
  - [ ] 配置文件解析功能（兼容 test/files 中的 ini 格式）
  - [ ] 键值对读取接口（参考 daemon.cc 中的配置读取逻辑）

#### 2. 事件循环核心 (难度: ★★★☆☆)

- [ ] event_base.h/event_base.cc
  - [ ] 事件循环基类实现（EventBase 核心逻辑）
  - [ ] 定时器管理（runAfter/runAt/cancel，参考 timer.cc）
  - [ ] 信号处理（Signal::signal，参考示例中的信号处理）
- [ ] poller.h/poller.cc
  - [ ] 跨平台 I/O 多路复用封装
    - [ ] Linux: epoll（参考 raw-examples/epoll.cc）
    - [ ] MacOS: kqueue（参考 raw-examples/kqueue.cc）
- [ ] port_posix.h/port_posix.cc
  - [ ] 平台相关系统调用封装（兼容 Linux/MacOS）

#### 3. 网络连接基础 (难度: ★★★☆☆)

- [ ] conn.h/conn.cc
  - [ ] TCP 连接基类（TcpConn，参考 echo.cc 连接逻辑）
  - [ ] 连接状态管理（Connected/Closed 等状态处理）
  - [ ] 数据发送/接收缓冲区（参考 Buffer 类实现）
- [ ] net.h/net.cc
  - [ ] 地址解析、套接字操作封装
  - [ ] TCP 服务器基类（TcpServer，参考 echo.cc 服务器实现）

#### 4. 协议编解码 (难度: ★★☆☆☆)

- [ ] codec.h/codec.cc
  - [ ] 行协议编解码器（LineCodec，参考 hsha.cc）
  - [ ] 长度前缀协议编解码器（LengthCodec，参考 codec-svr.cc）
- [ ] protobuf 支持（参考 protobuf 目录）
  - [ ] ProtoMsgCodec 实现（消息序列化/反序列化）
  - [ ] 与 protobuf 库的集成（.proto 文件处理）

#### 5. 高级网络功能 (难度: ★★★★☆)

- [ ] udp.h/udp.cc
  - [ ] UDP 连接封装（参考 udp-cli.cc/udp-svr.cc）
  - [ ] UDP 服务器实现（UdpServer）
- [ ] http.h/http.cc
  - [ ] HTTP 协议解析与封装
  - [ ] HTTP 服务器基础功能（参考 http-hello.cc）

#### 6. 扩展模块 (难度: ★★★☆☆)

- [ ] threads.h/threads.h
  - [ ] 线程池实现
  - [ ] 半同步半异步模式（HSHA）支持（参考 hsha.cc/udp-hsha.cc）
- [ ] stat-svr.h/stat-svr.cc
  - [ ] 状态监控服务器（参考 stat.cc）
  - [ ] 页面展示接口（onPage 函数实现）
- [ ] daemon.h/daemon.cc
  - [ ] 守护进程模式支持（参考 daemon.cc）
  - [ ] 进程管理（启动/停止/重启）

#### 7. 文件操作模块 (难度: ★★☆☆☆)

- [ ] file.h/file.cc
  - [ ] 文件读写操作封装（参考 file::writeContent 实现）
  - [ ] 文件状态检查与处理

#### 8. 示例与测试 (难度: ★★☆☆☆)

- [ ] 核心示例移植
  - [ ] echo.cc（基础回显服务）
  - [ ] timer.cc（定时器示例）
  - [ ] chat.cc（聊天服务器）
  - [ ] http-hello.cc（HTTP 服务示例）
- [ ] 测试用例完善
  - [ ] 配置文件解析测试（基于 test/files）
  - [ ] 网络功能单元测试