# 编译器与编译选项
CXX = g++
# 编译选项：C++17标准、警告、线程支持
CXXFLAGS = -std=c++17 -Wall -pthread
INCLUDES = -I../handy # 头文件路径

# 自动查找所有以_test结尾的cpp文件
TEST_SRCS = $(wildcard *_test.cpp)

# 从源文件生成对应的可执行文件名（例如：logger_test.cpp → logger_test）
TARGETS = $(TEST_SRCS:.cpp=)

# 核心依赖目标文件（handy中的logger和utils）
HANDY_OBJS = ../handy/logger.o ../handy/utils.o ../handy/conf.o ../handy/port_posix.o ../handy/net.o ../handy/codec.o ../handy/thread_pool.o

# 默认目标：编译所有测试程序
all: $(TARGETS)

# 自动生成每个测试程序的编译规则
# $@: 目标文件名（如logger_test）
# $<: 源文件（如logger_test.cpp）
$(TARGETS): %: %.cpp $(HANDY_OBJS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^ -pthread

# 编译handy模块的logger
../handy/logger.o: ../handy/logger.cpp ../handy/logger.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

# 编译handy模块的utils
../handy/utils.o: ../handy/utils.cpp ../handy/utils.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

# 编译handy模块的conf
../handy/conf.o: ../handy/conf.cpp ../handy/conf.h 
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

# 编译handy模块的port_posix
../handy/port_posix.o: ../handy/port_posix.cpp ../handy/port_posix.h 
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

# 编译handy模块的net
../handy/net.o: ../handy/net.cpp ../handy/net.h 
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

# 编译handy模块的net
../handy/codec.o: ../handy/codec.cpp ../handy/codec.h 
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

# 编译handy模块的thread_pool
../handy/thread_pool.o: ../handy/thread_pool.cpp ../handy/thread_pool.h 
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

# 清理生成文件
clean:
# rm -f $(TARGETS) ../handy/*.o *.log*
	# rm -f *.log*
    