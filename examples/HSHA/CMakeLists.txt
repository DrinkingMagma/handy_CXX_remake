# HSHA examples

add_executable(hsha_server server.cpp)
add_executable(hsha_client client.cpp)

# 链接 handy 库（handy 已经包含了头文件路径和线程库依赖）
target_link_libraries(hsha_server PRIVATE handy)
target_link_libraries(hsha_client PRIVATE handy)