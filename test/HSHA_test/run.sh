#!/bin/bash
# 自动编译并启动 HSHA 测试，支持通过 "stop" 参数一键结束

SERVER_PID_FILE="/tmp/hsha_server.pid"
CLIENT_PID_FILE="/tmp/hsha_client.pid"

case "$1" in
    start)
        echo "开始编译..."
        make
        if [ $? -ne 0 ]; then
            echo "编译失败，退出"
            exit 1
        fi

        echo "启动 HSHA server..."
        ./server &
        echo $! > "$SERVER_PID_FILE"

        sleep 1

        echo "启动 client..."
        ./client &
        echo $! > "$CLIENT_PID_FILE"

        echo "HSHA server 与 client 已后台启动"
        echo "Server PID: $(cat $SERVER_PID_FILE)"
        echo "Client PID: $(cat $CLIENT_PID_FILE)"
        ;;
    stop)
        if [ -f "$SERVER_PID_FILE" ]; then
            kill -2 $(cat "$SERVER_PID_FILE") 2>/dev/null
            rm -f "$SERVER_PID_FILE"
            echo "已停止 HSHA server"
        fi
        if [ -f "$CLIENT_PID_FILE" ]; then
            kill -2 $(cat "$CLIENT_PID_FILE") 2>/dev/null
            rm -f "$CLIENT_PID_FILE"
            echo "已停止 client"
        fi
        ;;
    *)
        echo "用法: $0 {start|stop}"
        echo "或指定连接数和每连接消息数: $0 start [connCount] [msgPerConn]"
        exit 1
        ;;
esac
