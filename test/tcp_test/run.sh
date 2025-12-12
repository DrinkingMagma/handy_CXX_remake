#!/bin/bash
# 自动编译并启动 tcp 测试，支持通过 "stop" 参数一键结束

SERVER_PID_FILE="/tmp/tcp_server.pid"
CLIENT_PID_FILE="/tmp/tcp_client.pid"

case "$1" in
    start)
        echo "开始编译..."
        # make clean && make
        make
        if [ $? -ne 0 ]; then
            echo "编译失败，退出"
            exit 1
        fi

        echo "启动 server..."
        ./server &
        echo $! > "$SERVER_PID_FILE"

        sleep 1   # 给 server 一点启动时间

        echo "启动 client..."
        ./client &
        echo $! > "$CLIENT_PID_FILE"

        echo "server 与 client 已后台启动，PID 分别记录在 $SERVER_PID_FILE 与 $CLIENT_PID_FILE"
        ;;
    stop)
        if [ -f "$SERVER_PID_FILE" ]; then
            kill $(cat "$SERVER_PID_FILE") 2>/dev/null
            rm -f "$SERVER_PID_FILE"
            echo "已停止 server"
        fi
        if [ -f "$CLIENT_PID_FILE" ]; then
            kill $(cat "$CLIENT_PID_FILE") 2>/dev/null
            rm -f "$CLIENT_PID_FILE"
            echo "已停止 client"
        fi
        ;;
    *)
        echo "用法: $0 {start|stop}"
        exit 1
        ;;
esac
