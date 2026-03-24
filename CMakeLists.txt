cmake_minimum_required(VERSION 3.10)

# 定义项目的名称
project(handy LANGUAGES CXX)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置构建类型
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# 设置编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

# 查找系统线程库（只需在根目录执行一次）
find_package(Threads REQUIRED)

# 添加 handy 库子目录
add_subdirectory(handy)

# 添加测试子目录
add_subdirectory(test)

# 添加示例子目录
add_subdirectory(examples)

# 添加自定义目标：一键构建所有
add_custom_target(all_targets
    DEPENDS handy logger_test codec_test udp_test daemon_test utils_test
            conn_test stat-svr_test http_test conf_test file_test
            status_test thread_pool_test slice_test port_posix_test net_test
            chat_example timer_example reconnect_example
            tcp_server tcp_client udp_server udp_client
            http_server http_client hsha_server hsha_client
            hshau_server hshau_client
            high_concurrency_server high_concurrency_client
    COMMENT "Building all targets"
)