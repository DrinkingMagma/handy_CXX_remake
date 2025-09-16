cmake_minimum_required(VERSION 3.10)
# 定义项目的名称
# project(handy)
# 可选，明确只使用C++
project((handy LANGUAGES CXX))

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
# 若不满足 C++17 标准则报错（构建失败）
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加 handy 子目录，执行其中的 CMakeLists.txt 文件
add_subdirectory(handy)
add_subdirectory(test)