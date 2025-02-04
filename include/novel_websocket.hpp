#pragma once

#include "novel_assistant.hpp"
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <string>
#include <memory>
#include <map>
#include <mutex>

namespace ainovel {

using NovelServer = websocketpp::server<websocketpp::config::asio>;
using connection_hdl = websocketpp::connection_hdl;

class NovelWebSocketServer {
public:
    NovelWebSocketServer(const std::string& host, uint16_t port,
                        const std::string& llm_api_key)
        : host_(host), port_(port) {
        
        novel_assistant_ = std::make_unique<NovelAssistant>(llm_api_key);
        
        // 设置WebSocket服务器
        server_.set_access_channels(websocketpp::log::alevel::none);
        server_.clear_access_channels(websocketpp::log::alevel::all);
        
        server_.init_asio();
        
        server_.set_open_handler(
            [this](connection_hdl hdl) {
                spdlog::info("Novel client connected");
            }
        );
        
        server_.set_close_handler(
            [this](connection_hdl hdl) {
                spdlog::info("Novel client disconnected");
            }
        );
        
        server_.set_message_handler(
            [this](connection_hdl hdl, NovelServer::message_ptr msg) {
                handle_message(hdl, msg);
            }
        );
    }
    
    void run() {
        try {
            server_.listen(host_, port_);
            server_.start_accept();
            spdlog::info("Novel WebSocket server started on {}:{}", host_, port_);
            server_.run();
        } catch (const std::exception& e) {
            spdlog::error("Failed to start novel server: {}", e.what());
            throw;
        }
    }
    
    void stop() {
        server_.stop();
        spdlog::info("Novel WebSocket server stopped");
    }
    
private:
    void handle_message(connection_hdl hdl, NovelServer::message_ptr msg) {
        try {
            json data = json::parse(msg->get_payload());
            
            if (!data.contains("type")) {
                return;
            }
            
            std::string type = data["type"];
            
            if (type == "query") {
                if (!data.contains("content")) {
                    return;
                }
                
                std::string question = data["content"];
                std::string response = novel_assistant_->ask(question);
                
                json reply = {
                    {"type", "response"},
                    {"content", response}
                };
                
                server_.send(hdl, reply.dump(), msg->get_opcode());
            }
            else if (type == "reset") {
                novel_assistant_->reset_conversation();
                json reply = {
                    {"type", "response"},
                    {"content", "对话已重置"}
                };
                server_.send(hdl, reply.dump(), msg->get_opcode());
            }
            
        } catch (const std::exception& e) {
            spdlog::error("Error processing novel message: {}", e.what());
            
            json error_reply = {
                {"type", "error"},
                {"message", e.what()}
            };
            
            server_.send(hdl, error_reply.dump(), msg->get_opcode());
        }
    }
    
    NovelServer server_;
    std::string host_;
    uint16_t port_;
    std::unique_ptr<NovelAssistant> novel_assistant_;
};

} // namespace ainovel
