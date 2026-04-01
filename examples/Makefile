# 编译器与编译选项
CXX = g++
# 调试选项：启用调试符号
DEBUG = -g

# 编译选项
# C++17标准、开启所有警告、启用线程支持
CXXFLAGS = -std=c++17 -Wall $(DEBUG) -pthread
# 头文件路径
INCLUDES = -I../handy

# 模块目录
HANDY_DIR = ../handy

# handy模块的源文件列表
HANDY_SRCS_BASENAME = logger utils conf port_posix net codec thread_pool daemon udp event_base poller conn file http stat-svr

# 自动生成handy模块的.cpp源文件路径
HANDY_SRCS = $(addprefix $(HANDY_DIR)/, $(addsuffix .cpp, $(HANDY_SRCS_BASENAME)))
# 自动生成handy模块的.o目标文件路径
HANDY_OBJS = $(HANDY_SRCS:.cpp=.o)

TEST_SRCS = $(wildcard *.cpp)
# 从源文件生成对应的可执行文件名
TARGETS = $(TEST_SRCS:.cpp=)

# ----------------- 规则定义 -----------------

# 默认目标：编译所有测试程序
all: $(TARGETS)

# 避免混用隐式和正常规则，同时确保路径正确
%.o: %.cpp %.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

# 自动生成每个测试程序的编译规则
# 目标：测试可执行文件
# 依赖：对应的源文件和所有handy模块的.o文件
$(TARGETS): %: %.cpp $(HANDY_OBJS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^

# 清理生成文件
clean:
	rm -f $(TARGETS) $(HANDY_OBJS) *.log*