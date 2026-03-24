# HSHAU examples

add_executable(hshau_server server.cpp)
add_executable(hshau_client client.cpp)

# 链接 handy 库（handy 已经包含了头文件路径和线程库依赖）
target_link_libraries(hshau_server PRIVATE handy)
target_link_libraries(hshau_client PRIVATE handy)