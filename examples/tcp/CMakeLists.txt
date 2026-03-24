# TCP examples

add_executable(tcp_server server.cpp)
add_executable(tcp_client client.cpp)

# 链接 handy 库（handy 已经包含了头文件路径和线程库依赖）
target_link_libraries(tcp_server PRIVATE handy)
target_link_libraries(tcp_client PRIVATE handy)