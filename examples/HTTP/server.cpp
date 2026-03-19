#include "http.h"
#include "logger.h"
#include <csignal>
#include <unistd.h>

using namespace handy;
static EventBase* g_base = nullptr;

void sigIntHandler(int sig)
{
    (void)sig;
    INFO("exit server.cpp");
    if (g_base) g_base->exit();
}

int main(int argc, char *argv[]) 
{
    Logger::getInstance().setLogLevel(Logger::LALL);
    Logger::getInstance().setLogFileName("./server.log");
    Logger::getInstance().clear();

    signal(SIGINT, sigIntHandler);

    std::unique_ptr<EventBase> base(new EventBase());
    g_base = base.get();
    
    HttpServer httpServer(base.get());
    
    int ret = httpServer.bind("127.0.0.1", 8080);
    if (ret != 0) {
        ERROR("http_server start failed, ret=%d", ret);
        return -1;
    }
    INFO("http_server start success on port 8080");

    httpServer.onGet("/", [](const HttpConnPtr& conn) {
        HttpResponse& res = conn.getResponse();
        res.setStatus(200, "OK");
        res.setHeader("Content-Type", "text/html; charset=utf-8");
        res.setBody("<html><body><h1>Welcome to HTTP Server</h1><p>This is the home page</p></body></html>");
        conn.sendResponse();
    });

    httpServer.onGet("/hello", [](const HttpConnPtr& conn) {
        HttpResponse& res = conn.getResponse();
        res.setStatus(200, "OK");
        res.setHeader("Content-Type", "text/plain; charset=utf-8");
        res.setBody("Hello, World!");
        conn.sendResponse();
    });

    httpServer.onGet("/json", [](const HttpConnPtr& conn) {
        HttpResponse& res = conn.getResponse();
        res.setStatus(200, "OK");
        res.setHeader("Content-Type", "application/json; charset=utf-8");
        res.setBody("{\"message\": \"Hello JSON\", \"status\": \"success\"}");
        conn.sendResponse();
    });

    httpServer.onRequest("POST", "/echo", [](const HttpConnPtr& conn) {
        HttpRequest& req = conn.getRequest();
        HttpResponse& res = conn.getResponse();
        
        res.setStatus(200, "OK");
        res.setHeader("Content-Type", "text/plain; charset=utf-8");
        
        std::string body = req.getBody().toString();
        res.setBody("Received POST data: " + body);
        conn.sendResponse();
    });

    httpServer.onGet("/info", [](const HttpConnPtr& conn) {
        HttpRequest& req = conn.getRequest();
        HttpResponse& res = conn.getResponse();
        
        res.setStatus(200, "OK");
        res.setHeader("Content-Type", "text/plain; charset=utf-8");
        
        std::string info = "Method: GET\n";
        info += "Version: " + req.getVersion() + "\n";
        
        auto headers = req.getHeaders();
        info += "Headers:\n";
        for (auto& h : headers) {
            info += "  " + h.first + ": " + h.second + "\n";
        }
        
        res.setBody(info);
        conn.sendResponse();
    });

    httpServer.onDefault([](const HttpConnPtr& conn) {
        HttpResponse& res = conn.getResponse();
        res.setStatus(404, "Not Found");
        res.setHeader("Content-Type", "text/html; charset=utf-8");
        res.setBody("<html><body><h1>404 Not Found</h1><p>The requested URL was not found on this server.</p></body></html>");
        conn.sendResponse();
    });

    base->loop();

    g_base = nullptr;
    return 0;
}
