# # 编译器与编译选项
# CXX = g++ -g
# # 编译选项：C++17标准、警告、线程支持
# CXXFLAGS = -std=c++17 -Wall -pthread
# INCLUDES = -I../handy # 头文件路径

# # 自动查找所有以_test结尾的cpp文件
# TEST_SRCS = $(wildcard *_test.cpp)

# # 从源文件生成对应的可执行文件名（例如：logger_test.cpp → logger_test）
# TARGETS = $(TEST_SRCS:.cpp=)

# # 核心依赖目标文件
# HANDY_OBJS = ../handy/logger.o ../handy/utils.o ../handy/conf.o ../handy/port_posix.o ../handy/net.o ../handy/codec.o ../handy/thread_pool.o ../handy/daemon.o ../handy/udp.o ../handy/event_base.o ../handy/poller.o ../handy/conn.o ../handy/file.o ../handy/http.o ../handy/stat-svr.o

# # 默认目标：编译所有测试程序
# all: $(TARGETS)

# # 自动生成每个测试程序的编译规则
# # $@: 目标文件名（如logger_test）
# # $<: 源文件（如logger_test.cpp）
# $(TARGETS): %: %.cpp $(HANDY_OBJS)
# 	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^ -pthread

# # 编译handy模块的logger
# ../handy/logger.o: ../handy/logger.cpp ../handy/logger.h
# 	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

# # 编译handy模块的utils
# ../handy/utils.o: ../handy/utils.cpp ../handy/utils.h
# 	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

# # 编译handy模块的conf
# ../handy/conf.o: ../handy/conf.cpp ../handy/conf.h 
# 	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

# # 编译handy模块的port_posix
# ../handy/port_posix.o: ../handy/port_posix.cpp ../handy/port_posix.h 
# 	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

# # 编译handy模块的net
# ../handy/net.o: ../handy/net.cpp ../handy/net.h 
# 	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

# # 编译handy模块的net
# ../handy/codec.o: ../handy/codec.cpp ../handy/codec.h 
# 	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

# # 编译handy模块的thread_pool
# ../handy/thread_pool.o: ../handy/thread_pool.cpp ../handy/thread_pool.h 
# 	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

# # 编译handy模块的daemon
# ../handy/daemon.o: ../handy/daemon.cpp ../handy/daemon.h 
# 	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

# # 编译handy模块的event_base
# ../handy/event_base.o: ../handy/event_base.cpp ../handy/event_base.h 
# 	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

# # 编译handy模块的Poller
# ../handy/poller.o: ../handy/poller.cpp ../handy/poller.h 
# 	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

# # 编译handy模块的conn
# ../handy/conn.o: ../handy/conn.cpp ../handy/conn.h 
# 	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

# # 编译handy模块的udp
# ../handy/udp.o: ../handy/udp.cpp ../handy/udp.h 
# 	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

# # 编译handy模块的file
# ../handy/file.o: ../handy/file.cpp ../handy/file.h 
# 	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

# # 编译handy模块的http
# ../handy/http.o: ../handy/http.cpp ../handy/http.h 
# 	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

# # 编译handy模块的stat-svr
# ../handy/stat-svr.o: ../handy/stat-svr.cpp ../handy/stat-svr.h 
# 	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

# # 清理生成文件
# clean:
# 	rm -f $(TARGETS) ../handy/*.o *.log*
# # rm -f *.log*


# 编译器与编译选项
CXX = g++
# 调试选项：启用调试符号
DEBUG = -g

# 编译选项
# C++17标准、开启所有警告、启用线程支持
CXXFLAGS = -std=c++17 -Wall $(DEBUG) -pthread
# 头文件路径
INCLUDES = -I../../handy

# 模块目录
HANDY_DIR = ../../handy

# handy模块的源文件列表（只需要修改这个列表即可）
HANDY_SRCS_BASENAME = logger utils conf port_posix net codec thread_pool daemon udp event_base poller conn file http stat-svr

# 自动生成handy模块的.cpp源文件路径
HANDY_SRCS = $(addprefix $(HANDY_DIR)/, $(addsuffix .cpp, $(HANDY_SRCS_BASENAME)))
# 自动生成handy模块的.o目标文件路径
HANDY_OBJS = $(HANDY_SRCS:.cpp=.o)

# 自动查找当前目录下所有以_test结尾的cpp文件作为测试源文件
TEST_SRCS = $(wildcard *_test.cpp)
# 从源文件生成对应的可执行文件名（例如：logger_test.cpp → logger_test）
TARGETS = $(TEST_SRCS:.cpp=)

# ----------------- 规则定义 -----------------

# 默认目标：编译所有测试程序
all: $(TARGETS)

# 修复：使用纯隐式规则语法编译handy模块的.o文件
# 避免混用隐式和正常规则，同时确保路径正确
%.o: %.cpp %.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

# 自动生成每个测试程序的编译规则
# 目标：测试可执行文件 (e.g., logger_test)
# 依赖：对应的源文件 (e.g., logger_test.cpp) 和所有handy模块的.o文件
$(TARGETS): %: %.cpp $(HANDY_OBJS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^

# 清理生成文件
clean:
	rm -f $(TARGETS) $(HANDY_OBJS) *.log*