# High Concurrency Test CMakeLists.txt

add_executable(high_concurrency_server server.cpp)
add_executable(high_concurrency_client client.cpp)

# 链接 handy 库（handy 已经包含了头文件路径和线程库依赖）
target_link_libraries(high_concurrency_server PRIVATE handy)
target_link_libraries(high_concurrency_client PRIVATE handy)
