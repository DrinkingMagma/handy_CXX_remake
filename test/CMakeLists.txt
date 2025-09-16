# 添加 logger_test 可执行文件
add_executable(logger_test logger_test.cpp)

# 链接 handy 库
# 将logger_test 链接到两个目标
find_package(Threads REQUIRED)
target_link_libraries(logger_test PRIVATE handy Threads::Threads)

# 包含 handy 头文件目录
# 为 logger_test 添加一个头文件搜索路径
target_include_directories(logger_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../handy
)