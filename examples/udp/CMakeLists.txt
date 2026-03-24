# UDP examples

add_executable(udp_server server.cpp)
add_executable(udp_client client.cpp)

# 链接 handy 库（handy 已经包含了头文件路径和线程库依赖）
target_link_libraries(udp_server PRIVATE handy)
target_link_libraries(udp_client PRIVATE handy)