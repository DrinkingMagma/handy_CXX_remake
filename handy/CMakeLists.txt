# 添加 handy 静态库，会自动生成libhandy.a
# 也可选SHARED（动态库）
add_library(handy STATIC
    logger.cpp
)

# 包含头文件目录
# 设置头文件搜索路径
# PUBLIC：表示使用这个库的其他目标（如 logger_test）也会继承这个 include 路径
# CMAKE_CURRENT_SOURCE_DIR：当前 CMakeLists.txt 所在目录
target_include_directories(handy PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)