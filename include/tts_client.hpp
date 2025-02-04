#pragma once

#include <curl/curl.h>
#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <opus/opus.h>

namespace ainovel {

using json = nlohmann::json;

class TTSClient {
public:
    TTSClient(const std::string& api_key, const std::string& api_url="https://api.siliconflow.cn/v1/audio/speech",
              const std::string& model = "FunAudioLLM/CosyVoice2-0.5B")
        : api_key_(api_key), api_url_(api_url), model_(model) {
        curl_ = curl_easy_init();
        if (!curl_) {
            throw std::runtime_error("Failed to initialize CURL");
        }

        // 初始化Opus编码器
        int error;
        encoder_ = opus_encoder_create(16000, 1, OPUS_APPLICATION_VOIP, &error);
        if (error != OPUS_OK || !encoder_) {
            if (curl_) curl_easy_cleanup(curl_);
            throw std::runtime_error("Failed to create Opus encoder: " + std::to_string(error));
        }

        // 设置比特率为32kbps (32000 bps)，这对于语音来说已经足够了
        opus_encoder_ctl(encoder_, OPUS_SET_BITRATE(32000));
    }

    ~TTSClient() {
        if (curl_) {
            curl_easy_cleanup(curl_);
        }
        if (encoder_) {
            opus_encoder_destroy(encoder_);
        }
    }

    // 将文本转换为音频数据，返回帧数组
    std::vector<std::vector<uint8_t>> synthesize(const std::string& text) {
        // 准备请求数据
        json request_data = {
            {"input", text},
            {"model", model_},
            {"voice", "FunAudioLLM/CosyVoice2-0.5B:diana"},
            {"sample_rate", 16000},
            {"stream", false},
            {"speed", 1},
            {"gain", 0},
            {"response_format", "wav"},  // 请求WAV格式
        };
        std::string json_str = request_data.dump();

        // 设置HTTP请求头
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, ("Authorization: Bearer " + api_key_).c_str());
        headers = curl_slist_append(headers, "Content-Type: application/json");

        std::vector<uint8_t> wav_data;

        // 设置CURL选项
        curl_easy_setopt(curl_, CURLOPT_URL, api_url_.c_str());
        curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, json_str.c_str());
        curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &wav_data);
        curl_easy_setopt(curl_, CURLOPT_VERBOSE, 1L);
        curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, 0L);

        // 执行请求
        CURLcode res = curl_easy_perform(curl_);
        curl_slist_free_all(headers);
        if (res != CURLE_OK) {
            throw std::runtime_error("Failed to perform TTS request: " + std::string(curl_easy_strerror(res)));
        }

        // get response code
        long response_code = 0;
        curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &response_code);
        spdlog::info("TTS response code: {}, WAV data size: {}", response_code, wav_data.size());

        // 解析WAV头部
        if (wav_data.size() < 44) {  // 标准WAV头部是44字节
            throw std::runtime_error("Invalid WAV data: too small");
        }

        // 检查WAV格式
        if (std::string(wav_data.begin(), wav_data.begin() + 4) != "RIFF" ||
            std::string(wav_data.begin() + 8, wav_data.begin() + 12) != "WAVE") {
            throw std::runtime_error("Invalid WAV format");
        }

        // 解析采样率和格式
        uint32_t sample_rate;
        uint16_t num_channels, bits_per_sample;
        std::memcpy(&sample_rate, wav_data.data() + 24, 4);
        std::memcpy(&num_channels, wav_data.data() + 22, 2);
        std::memcpy(&bits_per_sample, wav_data.data() + 34, 2);

        spdlog::info("WAV format: {} Hz, {} channels, {} bits", sample_rate, num_channels, bits_per_sample);

        // 提取PCM数据
        std::vector<int16_t> pcm_data;
        size_t pcm_start = 44;  // WAV头部后的数据
        size_t num_samples = (wav_data.size() - pcm_start) / sizeof(int16_t);
        pcm_data.resize(num_samples);
        std::memcpy(pcm_data.data(), wav_data.data() + pcm_start, wav_data.size() - pcm_start);

        // 将PCM数据转换为opus格式，每帧单独存储
        std::vector<std::vector<uint8_t>> opus_frames;
        const int frame_size = 960; // 60ms at 16kHz
        const int max_packet_size = 1500; 
        std::vector<uint8_t> opus_packet(max_packet_size);

        size_t offset = 0;
        while (offset < pcm_data.size()) {
            // 准备一帧PCM数据
            size_t remaining = std::min(frame_size, static_cast<int>(pcm_data.size() - offset));
            std::vector<opus_int16> pcm_frame(frame_size, 0);  // 初始化为0，处理最后一帧的填充
            
            // 复制PCM数据到帧
            std::memcpy(pcm_frame.data(), pcm_data.data() + offset, remaining * sizeof(int16_t));

            // 编码为Opus
            int encoded_size = opus_encode(encoder_, pcm_frame.data(), frame_size,
                                        opus_packet.data(), max_packet_size);

            if (encoded_size < 0) {
                throw std::runtime_error("Failed to encode PCM to Opus: " + std::to_string(encoded_size));
            }

            // 创建当前帧的数据包
            std::vector<uint8_t> frame_data;
            frame_data.reserve(encoded_size);
            // frame_data.push_back(static_cast<uint8_t>((encoded_size >> 8) & 0xFF));
            // frame_data.push_back(static_cast<uint8_t>(encoded_size & 0xFF));
            frame_data.insert(frame_data.end(), opus_packet.begin(), opus_packet.begin() + encoded_size);

            // 添加到帧数组
            opus_frames.push_back(std::move(frame_data));

            offset += frame_size;
        }

        spdlog::info("Encoded {} PCM samples into {} Opus frames", pcm_data.size(), opus_frames.size());
        return opus_frames;
    }

private:
    static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
        size_t realsize = size * nmemb;
        auto* data = static_cast<std::vector<uint8_t>*>(userp);
        const uint8_t* bytes = static_cast<const uint8_t*>(contents);
        data->insert(data->end(), bytes, bytes + realsize);
        return realsize;
    }

    CURL* curl_;
    OpusEncoder* encoder_;
    std::string api_key_;
    std::string api_url_;
    std::string model_;
};

} // namespace ainovel
