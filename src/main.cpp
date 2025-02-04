#include "websocket_server.hpp"
#include <spdlog/spdlog.h>
#include <csignal>

#ifdef _WIN32
    #include <WinSock2.h>
#endif

std::unique_ptr<ainovel::WebSocketServer> server;

void signal_handler(int signal) {
    spdlog::info("Received signal {}, stopping server...", signal);
    if (server) {
        server->stop();
    }
}

#ifdef _WIN32
class WSAInitializer {
public:
    WSAInitializer() {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            throw std::runtime_error("Failed to initialize WinSock");
        }
    }

    ~WSAInitializer() {
        WSACleanup();
    }
};
#endif

int main() {
    try {
        #ifdef _WIN32
            // 初始化Windows Socket
            WSAInitializer wsa;
            SetConsoleOutputCP(CP_UTF8);
        #endif

        // 设置信号处理
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        // 设置日志
        spdlog::set_level(spdlog::level::debug);
        spdlog::info("Starting WebSocket server...");

        // 创建并运行服务器
        server = std::make_unique<ainovel::WebSocketServer>("sk-tipgvyzsowtwrbrjdkmwzaeqhtdrdhkgclotewkrachyjblj","https://api.siliconflow.cn/v1/audio/transcriptions");
        server->run(8080);

        return 0;
    } catch (const std::exception& e) {
        spdlog::error("Fatal error: {}", e.what());
        return 1;
    }
}
