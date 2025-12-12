#include "event_base.h"
#include "conn.h"
#include "logger.h"
#include "csignal"

using namespace handy;

static EventBase* g_base = nullptr;

void sigIntHandler(int sig)
{
    (void)sig;
    INFO("exit client.cpp");
    if (g_base) g_base->exit();
}

int main(int argc, char *argv[]) 
{
    Logger::getInstance().setLogLevel(Logger::LALL);
    Logger::getInstance().setLogFileName("./client.log");
    Logger::getInstance().clear();

    signal(SIGINT, sigIntHandler);
    std::unique_ptr<EventBase> base(new EventBase());
    g_base = base.get();
    TcpConnPtr tcpClient = TcpConn::createConnection(base.get(), "127.0.0.1", 12345);
    if(tcpClient == nullptr)
    {
        ERROR("tcp_client start failed");
        return -1;
    }
    INFO("tcp_client start success");
    // 处理可读事件：当服务器发送数据时触发
    tcpClient->onReadable([](const TcpConnPtr& conn) {
        Buffer& inputBuffer = conn->getInputBuffer();
        if (inputBuffer.size() > 0) {
            // 读取并打印接收到的数据
            std::string data(inputBuffer.begin(), inputBuffer.end());
            INFO("Received from server: %s", data.c_str());
            // 清空输入缓冲区
            inputBuffer.clear();
        }
    });

    // 处理可写事件：当连接可以发送数据时触发
    tcpClient->onWritable([](const TcpConnPtr& conn) {
        // 注意：一旦连接变为可写，这个回调会持续触发
        // 所以通常只在有数据需要发送时才使用，或者发送后禁用可写事件
        static bool hasSent = false;
        if (!hasSent) {
            // 发送额外的数据（仅发送一次）
            conn->send("This is an additional message\n");
            hasSent = true;
            // 可以选择禁用可写事件监听，避免频繁触发
            conn->getChannel()->enableWrite(false);
        }
    });

    tcpClient->onState([](const TcpConnPtr& conn) {
        // 处理连接状态变化事件
        if (conn->getState() == TcpConn::State::CONNECTED) {
            INFO("Connected to server");
            conn->getChannel()->enableWrite(true);
        } else if (conn->getState() == TcpConn::State::CLOSED) {
            INFO("Disconnected from server");
        }
    });

    
    tcpClient->send("hello\nworld\n");

    base->loop();

    g_base = nullptr;
    
    return 0;
}