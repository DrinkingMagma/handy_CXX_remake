#include "event_base.h"
#include "conn.h"
#include "http.h"
#include "logger.h"
#include <csignal>
#include <unistd.h>
#include <vector>
#include <memory>

using namespace handy;

void sigIntHandler(int sig)
{
    (void)sig;
    INFO("exit client.cpp");
}

int main(int argc, char *argv[]) 
{
    Logger::getInstance().setLogLevel(Logger::LALL);
    Logger::getInstance().setLogFileName("./client.log");
    Logger::getInstance().clear();

    signal(SIGINT, sigIntHandler);
    std::unique_ptr<EventBase> base(new EventBase());
    
    TcpConnPtr tcpClient = TcpConn::createConnection(base.get(), "127.0.0.1", 8080);
    if(tcpClient == nullptr)
    {
        ERROR("http_client start failed");
        return -1;
    }
    INFO("http_client start success");

    std::string request1 = "GET / HTTP/1.1\r\n"
                           "Host: 127.0.0.1:8080\r\n"
                           "Connection: keep-alive\r\n"
                           "\r\n";
    
    std::string request2 = "GET /hello HTTP/1.1\r\n"
                           "Host: 127.0.0.1:8080\r\n"
                           "Connection: keep-alive\r\n"
                           "\r\n";
    
    std::string request3 = "GET /json HTTP/1.1\r\n"
                           "Host: 127.0.0.1:8080\r\n"
                           "Connection: keep-alive\r\n"
                           "\r\n";
    
    std::string request4 = "POST /echo HTTP/1.1\r\n"
                           "Host: 127.0.0.1:8080\r\n"
                           "Content-Type: text/plain\r\n"
                           "Content-Length: 13\r\n"
                           "Connection: keep-alive\r\n"
                           "\r\n"
                           "Hello, World!";
    
    std::string request5 = "GET /info HTTP/1.1\r\n"
                           "Host: 127.0.0.1:8080\r\n"
                           "Connection: keep-alive\r\n"
                           "\r\n";
    
    std::string request6 = "GET /notfound HTTP/1.1\r\n"
                           "Host: 127.0.0.1:8080\r\n"
                           "Connection: keep-alive\r\n"
                           "\r\n";

    std::vector<std::string> requests = {request1, request2, request3, request4, request5, request6};
    int requestIdx = 0;
    EventBase* basePtr = base.get();

    tcpClient->onReadable([&requestIdx, &requests, basePtr](const TcpConnPtr& conn) {
        Buffer& input = conn->getInputBuffer();
        if (input.size() > 0) {
            INFO("Received data from server:");
            std::string response(input.begin(), input.end());
            printf("%s\n", response.c_str());
            input.clear();
            
            requestIdx++;
            if (requestIdx < (int)requests.size()) {
                INFO("Sending next request (%d/%d)...", requestIdx + 1, (int)requests.size());
                usleep(100000);
                conn->send(requests[requestIdx]);
            } else {
                INFO("All requests completed, closing connection...");
                conn->close();
                basePtr->exit();
            }
        }
    });

    tcpClient->onState([basePtr](const TcpConnPtr& conn) {
        if (conn->getState() == TcpConn::State::CONNECTED) {
            INFO("Connected to HTTP server");
        } else if (conn->getState() == TcpConn::State::CLOSED) {
            INFO("Disconnected from server");
            basePtr->exit();
        }
    });

    INFO("Sending first request...");
    tcpClient->send(request1);

    base->loop();
    
    return 0;
}
