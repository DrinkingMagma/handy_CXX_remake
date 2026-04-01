# High Concurrency Test Makefile

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O3 -pthread
INCLUDES = -I../../handy
LDFLAGS = -pthread

# Source files
SERVER_SRC = server.cpp
CLIENT_SRC = client.cpp

# Object files
SERVER_OBJ = $(SERVER_SRC:.cpp=.o)
CLIENT_OBJ = $(CLIENT_SRC:.cpp=.o)

# Targets
SERVER_BIN = high_concurrency_server
CLIENT_BIN = high_concurrency_client

# Handy library
HANDY_LIB = ../../handy/libhandy.a
HANDY_SRCS = $(wildcard ../../handy/*.cpp)
HANDY_OBJS = $(HANDY_SRCS:.cpp=.o)

.PHONY: all clean run_server run_client run_test

all: $(SERVER_BIN) $(CLIENT_BIN)

# Build handy library
$(HANDY_LIB): $(HANDY_OBJS)
	ar rcs $@ $^

../../handy/%.o: ../../handy/%.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Build server
$(SERVER_BIN): $(SERVER_OBJ) $(HANDY_LIB)
	$(CXX) $(CXXFLAGS) -o $@ $< $(HANDY_LIB) $(LDFLAGS)

# Build client
$(CLIENT_BIN): $(CLIENT_OBJ) $(HANDY_LIB)
	$(CXX) $(CXXFLAGS) -o $@ $< $(HANDY_LIB) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(SERVER_BIN) $(CLIENT_BIN) $(SERVER_OBJ) $(CLIENT_OBJ)
	rm -f logs/*.log

# Create logs directory if not exists
logs:
	mkdir -p logs

# Run targets
run_server: $(SERVER_BIN) logs
	./$(SERVER_BIN)

run_client: $(CLIENT_BIN) logs
	./$(CLIENT_BIN)

# Run complete test
run_test: $(SERVER_BIN) $(CLIENT_BIN) logs
	@echo "Starting server..."
	./$(SERVER_BIN) &
	@sleep 2
	@echo "Starting client..."
	./$(CLIENT_BIN)
	@pkill -f $(SERVER_BIN) || true

help:
	@echo "High Concurrency Test Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all         - Build server and client"
	@echo "  clean       - Remove built files and logs"
	@echo "  run_server  - Run server only"
	@echo "  run_client  - Run client only (server must be running)"
	@echo "  run_test    - Run complete test (server + client)"
	@echo "  help        - Show this help message"
