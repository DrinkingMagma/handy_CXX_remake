# 添加 handy 静态库
add_library(handy STATIC
    logger.cpp
    codec.cpp
    conn.cpp
    daemon.cpp
    event_base.cpp
    file.cpp
    http.cpp
    net.cpp
    poller.cpp
    port_posix.cpp
    stat-svr.cpp
    thread_pool.cpp
    udp.cpp
    utils.cpp
    conf.cpp
)

# 设置头文件搜索路径
# PUBLIC：使用这个库的其他目标会继承这个 include 路径
target_include_directories(handy PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 链接系统线程库（在根目录已查找）
target_link_libraries(handy PRIVATE Threads::Threads)