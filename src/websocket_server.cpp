#include "websocket_server.hpp"

#include <unordered_set>
#include <unordered_map>
#include <spdlog/spdlog.h>
#include <boost/asio/ip/tcp.hpp>
#include <thread>

namespace ainovel {

WebSocketServer::WebSocketServer(const std::string& speech_api_key, 
                            const std::string& speech_api_url)
    : speech_client_(std::make_unique<SpeechClient>(speech_api_key, speech_api_url)),
    novel_assistant_(std::make_unique<NovelAssistant>(speech_api_key)),
    speech_api_key_(speech_api_key) {
    // 设置日志级别
    server_.set_access_channels(websocketpp::log::alevel::all);
    server_.clear_access_channels(websocketpp::log::alevel::frame_payload);

    // 初始化ASIO
    server_.init_asio();

    // 设置回调
    server_.set_open_handler([this](connection_hdl hdl) {
        this->on_open(hdl);
    });
    
    server_.set_close_handler([this](connection_hdl hdl) {
        this->on_close(hdl);
    });
    
    server_.set_message_handler([this](connection_hdl hdl, Server::message_ptr msg) {
        this->on_message(hdl, msg);
    });

    // 设置握手处理
    server_.set_validate_handler([this](connection_hdl hdl) -> bool {
        auto con = server_.get_con_from_hdl(hdl);
        
        // 验证Authorization
        auto auth = con->get_request_header("Authorization");
        if (auth.empty() || auth.substr(0, 7) != "Bearer ") {
            return false;
        }
        std::string token = auth.substr(7);
        if (!verify_token(token)) {
            return false;
        }

        // 验证Device-Id
        auto device_id = con->get_request_header("Device-Id");
        spdlog::info("Device-Id: {}", device_id);
        if (device_id.empty()) {
            return false;
        }

        // 验证Protocol-Version
        auto version = con->get_request_header("Protocol-Version");
        spdlog::info("Protocol-Version: {}", version);
        if (version != "1") {
            return false;
        }

        return true;
    });
}

void WebSocketServer::run(uint16_t port) {
    if (running_) return;

    try {
        server_.listen(port);
        server_.start_accept();
        running_ = true;
        spdlog::info("WebSocket server started on port {}", port);
        server_.run();
    } catch (const std::exception& e) {
        spdlog::error("Failed to start server: {}", e.what());
        throw;
    }
}

void WebSocketServer::stop() {
    if (!running_) return;

    server_.stop();
    running_ = false;
    spdlog::info("WebSocket server stopped");
}

bool WebSocketServer::verify_token(const std::string& token) {
    return token == "test-token";
}

void WebSocketServer::on_open(connection_hdl hdl) {
    auto con = server_.get_con_from_hdl(hdl);
    
    std::lock_guard<std::mutex> lock(mutex_);
    auto& client = clients_[hdl];
    client.device_id = con->get_request_header("Device-Id");
    client.authenticated = true;
    // 获取底层 socket 并设置选项
    boost::asio::ip::tcp::socket& sock = con->get_socket();
    sock.set_option(boost::asio::ip::tcp::no_delay(true)); // 正确调用 set_option
    
    spdlog::info("Client connected: {}", client.device_id);
}

void WebSocketServer::on_close(connection_hdl hdl) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = clients_.find(hdl);
    if (it != clients_.end()) {
        spdlog::info("Client disconnected: {}", it->second.device_id);
        clients_.erase(it);
    }
}

void WebSocketServer::on_message(connection_hdl hdl, Server::message_ptr msg) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = clients_.find(hdl);
    if (it == clients_.end()) return;

    try {
        if (msg->get_opcode() == websocketpp::frame::opcode::binary) {
            auto payload = msg->get_payload();
            std::vector<uint8_t> data(payload.begin(), payload.end());
            // if(rand() % 10 == 0){
            //     spdlog::info("Received binary message: {}", websocketpp::utility::to_hex(msg->get_payload()));
            // }
            
            handle_binary_message(hdl, data);
        } else {
            // 处理JSON消息
            auto json_msg = json::parse(msg->get_payload());
            spdlog::info("Received JSON message: {}", json_msg.dump());
            handle_json_message(hdl, json_msg);
        }
    } catch (const std::exception& e) {
        spdlog::error("Error processing message: {}", e.what());
    }
}

void WebSocketServer::handle_binary_message(connection_hdl hdl, 
                                          const std::vector<uint8_t>& data) {
    try {

        //spdlog::info("Received binary message: {}", data.size());
        auto& client = clients_[hdl];
        if (!client.decoder) {
            // 创建解码器
            int sample_rate = client.audio_params.value("sample_rate", 16000);
            int channels = client.audio_params.value("channels", 1);
            client.decoder = std::make_unique<OpusDecoder>(sample_rate, channels);
        }
        handle_audio_data(hdl,client, data);
    } catch (const std::exception& e) {
        spdlog::error("Error handling binary message: {}", e.what());
    }
}

void WebSocketServer::handle_audio_data(connection_hdl hdl,ClientContext& client, 
                                      const std::vector<uint8_t>& audio_data) {
    try {
        // 更新最后活动时间
        client.last_activity = std::chrono::steady_clock::now();

        // 解码Opus数据
        auto pcm_data = client.decoder->decode_float(audio_data);
        
        // 初始化VAD处理器（如果还没有）
        if (!client.vad) {
            client.vad = std::make_unique<VadProcessor>(16000, 0);
        }

        // 进行VAD检测
        bool has_voice = client.vad->process_frame(pcm_data);
        
        if (has_voice) {
            client.consecutive_speech++;
            client.consecutive_silence = 0;
            
            // 如果连续检测到足够多的语音帧，认为开始说话
            if (client.consecutive_speech >= ClientContext::SPEECH_THRESHOLD && !client.is_speaking) {
                client.is_speaking = true;
                //spdlog::info("Speech started");
                
                // 将预缓冲的帧添加到主缓冲区
                client.audio_buffer.commit_pre_buffer();
            }
        } else {
            client.consecutive_silence++;
            client.consecutive_speech = 0;
            
            // 如果连续检测到足够多的静音帧，认为说话结束
            if (client.consecutive_silence >= ClientContext::SILENCE_THRESHOLD && client.is_speaking) {
                client.is_speaking = false;
                client.should_process = true;
                //spdlog::info("Speech ended");
            }
        }

        // 根据不同模式处理音频
        switch (client.response_mode) {
            case ResponseMode::AUTO: {
                // 自动模式：使用VAD检测来控制录音
                if (client.is_speaking) {
                    // 添加音频数据到主缓冲区
                    if (!client.audio_buffer.append(pcm_data)) {
                        // 缓冲区满了，强制处理
                        client.should_process = true;
                    }
                } else {
                    // 非说话状态时，添加到预缓冲区
                    client.audio_buffer.add_to_pre_buffer(pcm_data);
                }
                break;
            }
            
            case ResponseMode::MANUAL: {
                // 手动模式：只要状态是LISTENING就累积音频
                if (client.state == ClientState::LISTENING) {
                    client.audio_buffer.append(pcm_data);
                }
                break;
            }
            
            case ResponseMode::REAL_TIME: {
                // 实时模式：直接处理每一帧音频
                client.audio_buffer.append(pcm_data);
                client.should_process = true;
                break;
            }
        }
        
        // 处理缓冲区
        process_audio_buffer(hdl,client);
        
    } catch (const std::exception& e) {
        spdlog::error("Error handling audio data: {}", e.what());
    }
}

void WebSocketServer::process_speech_recognition(connection_hdl hdl, ClientContext& client) {
    try {
        // 调用语音识别API
        std::string transcript = speech_client_->recognize(client.audio_buffer.pcm_data);
        if (!transcript.empty()) {
            spdlog::info("got transcript:{}",transcript);
            json stt = {
                {"type", "stt"},
                {"text", transcript},
            };
            send_json(hdl, stt);
            server_.get_io_service().poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(60));
            
            json start = {
                {"type", "tts"},
                {"state", "start"}
            };
            send_json(hdl, start);
            server_.get_io_service().poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(30));

            std::string response = novel_assistant_->ask(transcript);

            spdlog::info("got response:{}",response);
            send_tts_sequence(hdl, response);
            
            json stop = {
                {"type", "tts"},
                {"state", "stop"}
            };
            send_json(hdl, stop);
            
        }
        // 清空音频缓冲区
        client.audio_buffer.clear();
        client.should_process = false;
        
    } catch (const std::exception& e) {
        spdlog::error("Speech recognition failed: {}", e.what());
    }
}

void WebSocketServer::process_audio_buffer(connection_hdl hdl, ClientContext& client) {
    // 只有当should_process为true且缓冲区不为空时才处理
    if (client.should_process && !client.audio_buffer.pcm_data.empty()) {
        // 如果缓冲区数据太少，不处理
        if (client.audio_buffer.pcm_data.size() < 16000) {  // 至少1秒的音频
            return;
        }
        process_speech_recognition(hdl, client);
    }
}

void WebSocketServer::handle_json_message(connection_hdl hdl, const json& msg) {
    auto& client = clients_[hdl];
    
    // 检查type字段是否存在
    if (!msg.contains("type")) {
        
        spdlog::error("Missing 'type' field in JSON message");
        return;
    }

    // 获取type字段，并转换为字符串
    std::string type;
    try {
        if (msg["type"].is_string()) {
            type = msg["type"].get<std::string>();
        } else if (msg["type"].is_number()) {
            type = std::to_string(msg["type"].get<int>());
        } else {
            spdlog::error("Invalid 'type' field type in JSON message");
            return;
        }
    } catch (const std::exception& e) {
        spdlog::error("Error processing message type: {}", e.what());
        return;
    }

    spdlog::info("Processing message type: {}", type);

    if (type == "hello") {
        std::string response_mode_str = msg.value("response_mode", "auto");
        client.response_mode = mode_from_string(response_mode_str);
        client.audio_params = msg.value("audio_params", json::object());
        spdlog::info("Client hello: {} (mode: {})", 
                    client.device_id, 
                    to_string(client.response_mode));

        // 发送hello响应
        json response = {
            {"type", "hello"},
            {"version", 3},
            {"transport", "websocket"},
            {"audio_params", {
                {"format", "opus"},
                {"sample_rate", 16000},
                {"channels", 1},
                {"frame_duration", 60}
            }}
        };
        send_json(hdl, response);
    }
    else if (type == "state") {
        if (!msg.contains("state")) {
            spdlog::error("Missing 'state' field in state message");
            return;
        }

        try {
            auto new_state = state_from_string(msg["state"].get<std::string>());
            if (client.state != new_state) {
                client.state = new_state;
                spdlog::info("Client state change: {} -> {}", 
                            client.device_id, 
                            to_string(client.state));

                // 在manual模式下，如果状态从listening变为idle，开始处理
                if (client.response_mode == ResponseMode::MANUAL &&
                    new_state == ClientState::IDLE) {
                    process_audio_buffer(hdl,client);
                }
            }
        } catch (const std::exception& e) {
            spdlog::error("Error processing state change: {}", e.what());
        }
    }
    else if (type == "abort") {
        spdlog::info("Client abort: {}", client.device_id);
        // TODO: 中断当前处理
    }
    else {
        spdlog::warn("Unknown message type: {}", type);
    }
}

void WebSocketServer::send_tts_sequence(connection_hdl hdl, const std::string& text) {
    
    
    // 使用更智能的分句方式
    std::vector<std::string> sentences;
    std::string current_sentence;
    
    auto is_punctuation = [](const std::string& s) -> bool {
        static const std::unordered_set<std::string> end_marks = {
            "。", "！", "？", ".", "!", "?"
        };
        static const std::unordered_set<std::string> quote_marks = {
            """, """, "」", ")", "）"
        };
        static const std::unordered_set<std::string> pause_marks = {
            "，", "；", ",", ";"
        };
        return end_marks.count(s) > 0 || quote_marks.count(s) > 0 || pause_marks.count(s) > 0;
    };

    auto is_end_mark = [](const std::string& s) -> bool {
        static const std::unordered_set<std::string> end_marks = {
            "。", "！", "？", ".", "!", "?"
        };
        return end_marks.count(s) > 0;
    };

    auto is_quote_mark = [](const std::string& s) -> bool {
        static const std::unordered_set<std::string> quote_marks = {
            """, """, "」", ")", "）"
        };
        return quote_marks.count(s) > 0;
    };

    auto is_pause_mark = [](const std::string& s) -> bool {
        static const std::unordered_set<std::string> pause_marks = {
            "，", "；", ",", ";"
        };
        return pause_marks.count(s) > 0;
    };

    size_t i = 0;
    while (i < text.length()) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        std::string utf8_char;
        
        if (c < 0x80) {  // ASCII字符
            utf8_char = text[i];
            i += 1;
        } else if ((c & 0xE0) == 0xC0) {  // 2字节UTF-8
            if (i + 1 < text.length())
                utf8_char = text.substr(i, 2);
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {  // 3字节UTF-8
            if (i + 2 < text.length())
                utf8_char = text.substr(i, 3);
            i += 3;
        } else if ((c & 0xF8) == 0xF0) {  // 4字节UTF-8
            if (i + 3 < text.length())
                utf8_char = text.substr(i, 4);
            i += 4;
        } else {
            // 无效的UTF-8序列，跳过
            i += 1;
            continue;
        }

        current_sentence += utf8_char;
        
        // 检查是否是句子结束
        bool is_end = false;
        if (i < text.length()) {
            // 获取下一个UTF-8字符
            unsigned char next_c = static_cast<unsigned char>(text[i]);
            std::string next_char;
            size_t next_size = 1;
            
            if (next_c < 0x80) {
                next_char = text[i];
            } else if ((next_c & 0xE0) == 0xC0 && i + 1 < text.length()) {
                next_char = text.substr(i, 2);
                next_size = 2;
            } else if ((next_c & 0xF0) == 0xE0 && i + 2 < text.length()) {
                next_char = text.substr(i, 3);
                next_size = 3;
            } else if ((next_c & 0xF8) == 0xF0 && i + 3 < text.length()) {
                next_char = text.substr(i, 4);
                next_size = 4;
            }

            if (is_end_mark(utf8_char) && !is_quote_mark(next_char)) {
                is_end = true;
            } else if (is_pause_mark(utf8_char) && current_sentence.length() >= 45) {  // 考虑到UTF-8编码，增加长度阈值
                is_end = true;
            }
        } else {
            // 文本的最后一个字符
            is_end = true;
        }
        
        if (is_end && !current_sentence.empty()) {
            // 移除句子前后的空白字符
            while (!current_sentence.empty() && std::isspace(static_cast<unsigned char>(current_sentence.front()))) {
                current_sentence.erase(0, 1);
            }
            while (!current_sentence.empty() && std::isspace(static_cast<unsigned char>(current_sentence.back()))) {
                current_sentence.pop_back();
            }
            
            if (!current_sentence.empty()) {
                sentences.push_back(current_sentence);
                current_sentence.clear();
            }
        }
    }
    
    // 如果最后还有未处理的句子
    if (!current_sentence.empty()) {
        sentences.push_back(current_sentence);
    }

    TTSClient tts_client(speech_api_key_);
    int n_total_packet =0;

    // 处理每个句子
    for (const auto& sentence : sentences) {
        spdlog::info("Processing sentence: {}", sentence);
        
        json sentence_start = {
            {"type", "tts"},
            {"state", "sentence_start"},
            {"text", sentence}
        };
        send_json(hdl, sentence_start);
        server_.get_io_service().poll();
        // std::this_thread::sleep_for(std::chrono::milliseconds(60));
        try {
            // 合成语音
            std::vector<std::vector<uint8_t>> audio_datas = tts_client.synthesize(sentence);
            // 发送音频数据
            for (const auto& audio_data : audio_datas){
                send_binary(hdl, audio_data);
            }
            std::vector<uint8_t> empty;
            send_binary(hdl, empty);
            n_total_packet += audio_datas.size();
            server_.get_io_service().poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(60*audio_datas.size()));
        } catch (const std::exception& e) {
            spdlog::error("TTS synthesis failed for sentence: {}, error: {}", sentence, e.what());
            // 发送错误消息给客户端
            json error = {
                {"type", "tts"},
                {"state", "error"},
                {"error", e.what()}
            };
            send_json(hdl, error);
        }

        json sentence_end = {
            {"type", "tts"},
            {"state", "sentence_end"}
        };
        send_json(hdl, sentence_end);
        server_.get_io_service().poll();
    }
    // std::this_thread::sleep_for(std::chrono::milliseconds(70*2 * n_total_packet));
    
}

void WebSocketServer::send_binary(connection_hdl hdl, const std::vector<uint8_t>& payload) {
    try {
        // 减小分片大小到64KB，避免客户端缓冲区溢出
        const size_t MAX_CHUNK_SIZE = 64 * 1024-1;
        
        // 如果数据大小小于最大分片大小，直接发送
        if (payload.size() <= MAX_CHUNK_SIZE) {
            server_.send(hdl, payload.data(), payload.size(), websocketpp::frame::opcode::binary);
            return;
        }

        // 分片发送
        size_t offset = 0;
        while (offset < payload.size()) {
            size_t chunk_size = std::min(MAX_CHUNK_SIZE, payload.size() - offset);
            websocketpp::frame::opcode::value op = (offset == 0) ? 
                websocketpp::frame::opcode::binary : 
                websocketpp::frame::opcode::continuation;
            
            server_.send(hdl, payload.data() + offset, chunk_size, op);
            offset += chunk_size;
        }
    } catch (const std::exception& e) {
        spdlog::error("Error sending binary message: {}", e.what());
    }
}

void WebSocketServer::send_json(connection_hdl hdl, const json& msg) {
    try {
        server_.send(hdl, msg.dump(), websocketpp::frame::opcode::text);
    } catch (const std::exception& e) {
        spdlog::error("Error sending JSON message: {}", e.what());
    }
}

} // namespace ainovel
