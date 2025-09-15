# 编译器
CXX = g++
# C++ 标准 + 线程库选项（-pthread 同时用于编译和链接）
CXXFLAGS = -std=c++11 -Wall -pthread
# 头文件搜索路径（指向 logger.h 所在目录）
INCLUDES = -I../handy
# 源文件（测试代码）
SRCS = logger_test.cpp
# 生成的可执行文件名
TARGET = logger_test
# 依赖的目标文件（logger 核心实现）
HANDY_OBJ = ../handy/logger.o

# 默认目标：编译所有
all: $(TARGET)

# 链接生成可执行文件（此处可额外加 -pthread，确保链接时生效）
$(TARGET): $(SRCS) $(HANDY_OBJ)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^ -pthread

# 编译 logger.cpp 生成目标文件（依赖 logger.h）
../handy/logger.o: ../handy/logger.cpp ../handy/logger.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

# 清理生成的文件（可执行文件 + 目标文件）
clean:
	rm -f $(TARGET) *.o ../handy/*.o