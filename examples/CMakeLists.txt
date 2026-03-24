# Examples directory CMakeLists.txt

# 添加顶层示例程序
set(EXAMPLE_SOURCES
    chat.cpp
    timer.cpp
    reconnect.cpp
)

# 为每个示例源文件创建可执行文件
foreach(example_source ${EXAMPLE_SOURCES})
    # 获取文件名（不含扩展名）并添加_example后缀
    get_filename_component(example_name ${example_source} NAME_WE)
    add_executable(${example_name}_example ${example_source})
    
    # 链接 handy 库（handy 已经包含了头文件路径和线程库依赖）
    target_link_libraries(${example_name}_example PRIVATE handy)
endforeach()

# 添加子目录示例
add_subdirectory(tcp)
add_subdirectory(udp)
add_subdirectory(HTTP)
add_subdirectory(HSHA)
add_subdirectory(HSHAU)
add_subdirectory(high_concurrency)
