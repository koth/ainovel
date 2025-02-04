#pragma once

#include <websocketpp/config/asio.hpp>
#include <websocketpp/server.hpp>
#include <nlohmann/json.hpp>
#include <deque>
#include "binary_protocol.hpp"
#include "audio_processor.hpp"
#include "vad_processor.hpp"
#include "speech_client.hpp"
#include "tts_client.hpp"
#include "novel_assistant.hpp"

namespace ainovel {

using json = nlohmann::json;
using websocketpp::connection_hdl;

using Server = websocketpp::server<websocketpp::config::asio>;

struct AudioBuffer {
    std::vector<float> pcm_data;        // PCM音频数据
    std::deque<std::vector<float>> pre_buffer;  // 预缓冲，用于保存可能的语音起始部分
    size_t max_buffer_size{16000 * 5};  // 默认最大缓冲5秒音频
    size_t pre_buffer_frames{3};        // 预缓冲帧数
    size_t silence_threshold{800};      // 静音阈值（样本数）
    size_t silence_duration{0};         // 当前静音持续时间
    
    bool append(const std::vector<float>& data) {
        // 检查是否有足够的空间
        if (pcm_data.size() + data.size() > max_buffer_size) {
            return false;
        }
        
        // 添加新数据
        pcm_data.insert(pcm_data.end(), data.begin(), data.end());
        return true;
    }
    
    void add_to_pre_buffer(const std::vector<float>& data) {
        pre_buffer.push_back(data);
        if (pre_buffer.size() > pre_buffer_frames) {
            pre_buffer.pop_front();
        }
    }
    
    void commit_pre_buffer() {
        // 将预缓冲中的数据添加到主缓冲区
        for (const auto& frame : pre_buffer) {
            pcm_data.insert(pcm_data.end(), frame.begin(), frame.end());
        }
        pre_buffer.clear();
    }
    
    void clear() {
        pcm_data.clear();
        pre_buffer.clear();
        silence_duration = 0;
    }
    
    bool is_empty() const {
        return pcm_data.empty() && pre_buffer.empty();
    }
    
    size_t size() const {
        size_t total_size = pcm_data.size();
        for (const auto& frame : pre_buffer) {
            total_size += frame.size();
        }
        return total_size;
    }
};

struct ClientContext {
    std::string device_id;
    ClientState state{ClientState::IDLE};
    ResponseMode response_mode{ResponseMode::AUTO};
    bool authenticated{false};
    json audio_params;
    std::unique_ptr<OpusDecoder> decoder;
    std::unique_ptr<VadProcessor> vad;
    AudioBuffer audio_buffer;
    
    // 音频处理状态
    bool is_speaking{false};         // 是否正在说话
    bool should_process{false};      // 是否应该处理音频
    std::chrono::steady_clock::time_point last_activity;  // 最后活动时间
    size_t consecutive_silence{0};   // 连续静音帧计数
    size_t consecutive_speech{0};    // 连续语音帧计数
    static constexpr size_t SPEECH_THRESHOLD = 5;    // 需要连续检测到的语音帧数
    static constexpr size_t SILENCE_THRESHOLD = 8;   // 需要连续检测到的静音帧数
};

class WebSocketServer {
public:
    WebSocketServer(const std::string& speech_api_key, 
                   const std::string& speech_api_url="https://api.siliconflow.cn/v1/audio/transcriptions");
    void run(uint16_t port);
    void stop();

private:
    bool verify_token(const std::string& token);
    void handle_binary_message(connection_hdl hdl, const std::vector<uint8_t>& payload);
    void handle_json_message(connection_hdl hdl, const json& msg);
    void handle_audio_data(connection_hdl hdl, ClientContext& client, const std::vector<uint8_t>& audio_data);
    void process_audio_buffer(connection_hdl hdl, ClientContext& client);
    void send_tts_sequence(connection_hdl hdl, const std::string& text);
    void send_binary(connection_hdl hdl, const std::vector<uint8_t>& payload);
    void send_json(connection_hdl hdl, const json& msg);
    void process_speech_recognition(connection_hdl hdl, ClientContext& client);

    // WebSocket callbacks
    void on_open(connection_hdl hdl);
    void on_close(connection_hdl hdl);
    void on_message(connection_hdl hdl, Server::message_ptr msg);

private:
    Server server_;
    std::map<connection_hdl, ClientContext, std::owner_less<connection_hdl>> clients_;
    std::mutex mutex_;
    bool running_{false};
    std::string speech_api_key_;
    std::unique_ptr<SpeechClient> speech_client_;
    std::unique_ptr<NovelAssistant> novel_assistant_;
};

} // namespace ainovel
