#pragma once

#include <curl/curl.h>
#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace ainovel {

using json = nlohmann::json;

// WAV文件头结构
#pragma pack(push, 1)
struct WavHeader {
    // RIFF Header
    char riff_header[4] = {'R', 'I', 'F', 'F'};  // RIFF标识
    uint32_t wav_size;                           // 文件大小 - 8
    char wave_header[4] = {'W', 'A', 'V', 'E'};  // WAVE标识
    
    // Format Header
    char fmt_header[4] = {'f', 'm', 't', ' '};   // fmt标识
    uint32_t fmt_chunk_size = 16;                // fmt块大小
    uint16_t audio_format = 1;                   // 音频格式 (1 = PCM)
    uint16_t num_channels;                       // 通道数
    uint32_t sample_rate;                        // 采样率
    uint32_t byte_rate;                          // 字节率
    uint16_t sample_alignment;                   // 数据块对齐
    uint16_t bit_depth;                          // 位深度
    
    // Data Header
    char data_header[4] = {'d', 'a', 't', 'a'};  // data标识
    uint32_t data_bytes;                         // 音频数据大小
};
#pragma pack(pop)

class SpeechClient {
public:
    SpeechClient(const std::string& api_key, const std::string& api_url, 
                 const std::string& model = "FunAudioLLM/SenseVoiceSmall")
        : api_key_(api_key), api_url_(api_url), model_(model) {
        curl_ = curl_easy_init();
        if (!curl_) {
            throw std::runtime_error("Failed to initialize CURL");
        }
        
        // 初始化mime
        mime_ = curl_mime_init(curl_);
        if (!mime_) {
            curl_easy_cleanup(curl_);
            throw std::runtime_error("Failed to initialize CURL mime");
        }
    }

    ~SpeechClient() {
        if (mime_) {
            curl_mime_free(mime_);
        }
        if (curl_) {
            curl_easy_cleanup(curl_);
        }
    }

    std::string recognize(const std::vector<float>& pcm_data, int sample_rate = 16000) {
        // 重置mime
        if (mime_) {
            curl_mime_free(mime_);
        }
        mime_ = curl_mime_init(curl_);
        
        // 创建WAV数据
        std::vector<char> wav_data = create_wav(pcm_data, sample_rate);
        // spdlog::debug("Created WAV data with size: {} bytes", wav_data.size());
        
        // 添加WAV文件部分
        curl_mimepart* file_part = curl_mime_addpart(mime_);
        curl_mime_data(file_part, wav_data.data(), wav_data.size());
        curl_mime_name(file_part, "file");
        curl_mime_filename(file_part, "audio.wav");
        curl_mime_type(file_part, "audio/wav");

        // 添加model字段
        curl_mimepart* model_part = curl_mime_addpart(mime_);
        curl_mime_data(model_part, model_.c_str(), CURL_ZERO_TERMINATED);
        curl_mime_name(model_part, "model");

        // 设置HTTP请求头
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, ("Authorization: Bearer " + api_key_).c_str());
        headers = curl_slist_append(headers, "Content-Type: multipart/form-data");

        std::string response_data;

        // 设置CURL选项
        curl_easy_setopt(curl_, CURLOPT_URL, api_url_.c_str());
        curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl_, CURLOPT_MIMEPOST, mime_);
        curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response_data);
        curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(curl_, CURLOPT_VERBOSE, 0L);  // Enable verbose debug output

        // spdlog::debug("Sending request to: {}", api_url_);

        // 发送请求
        CURLcode res = curl_easy_perform(curl_);
        curl_slist_free_all(headers);

        if (res != CURLE_OK) {
            throw std::runtime_error(std::string("CURL request failed: ") + 
                                   curl_easy_strerror(res));
        }

        // 解析响应
        try {
            // spdlog::debug("Server response: {}", response_data);
            json response = json::parse(response_data);
            if (response.contains("text")){
                return response["text"];
            }else{
                spdlog::error("Failed to parse response: {}", response_data);
            }
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to parse response: ") + e.what());
        }

        return "";
    }

private:
    static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
        std::string* response = static_cast<std::string*>(userdata);
        response->append(ptr, size * nmemb);
        return size * nmemb;
    }

    std::vector<char> create_wav(const std::vector<float>& pcm_data, int sample_rate) {
        // 创建WAV头
        WavHeader header;
        
        // 设置WAV参数
        header.num_channels = 1;  // 单声道
        header.sample_rate = sample_rate;
        header.bit_depth = 16;    // 16位深度
        
        // 计算相关参数
        header.byte_rate = sample_rate * header.num_channels * (header.bit_depth / 8);
        header.sample_alignment = header.num_channels * (header.bit_depth / 8);
        
        // 将float PCM转换为int16
        std::vector<int16_t> int16_data;
        int16_data.reserve(pcm_data.size());
        for (float sample : pcm_data) {
            int16_data.push_back(static_cast<int16_t>(sample * 32768.0f));
        }
        
        // 计算数据大小
        header.data_bytes = static_cast<uint32_t>(int16_data.size() * sizeof(int16_t));
        header.wav_size = header.data_bytes + sizeof(WavHeader) - 8;
        
        // 创建输出缓冲区
        std::vector<char> wav_data;
        wav_data.reserve(sizeof(WavHeader) + header.data_bytes);
        
        // 写入WAV头
        wav_data.insert(wav_data.end(), 
                       reinterpret_cast<char*>(&header),
                       reinterpret_cast<char*>(&header) + sizeof(WavHeader));
        
        // 写入音频数据
        wav_data.insert(wav_data.end(),
                       reinterpret_cast<char*>(int16_data.data()),
                       reinterpret_cast<char*>(int16_data.data()) + header.data_bytes);
        
        return wav_data;
    }

    CURL* curl_;
    curl_mime* mime_;
    std::string api_key_;
    std::string api_url_;
    std::string model_;
};

} // namespace ainovel
