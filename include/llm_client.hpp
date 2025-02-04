#pragma once

#include <string>
#include <vector>
#include <memory>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace ainovel {

using json = nlohmann::json;

class LLMClient {
public:
    struct Message {
        std::string role;
        std::string content;
        
        json to_json() const {
            return {
                {"role", role},
                {"content", content}
            };
        }
    };
    
    LLMClient(const std::string& api_key, 
              const std::string& api_url = "https://api.siliconflow.cn/v1/chat/completions",
              const std::string& model = "Qwen/Qwen2.5-7B-Instruct") 
        : api_key_(api_key), api_url_(api_url), model_(model) {
        curl_ = curl_easy_init();
        if (!curl_) {
            throw std::runtime_error("Failed to initialize CURL");
        }
    }
    
    ~LLMClient() {
        if (curl_) {
            curl_easy_cleanup(curl_);
        }
    }
    
    std::string chat(const std::vector<Message>& messages) {
        json request = {
            {"model", model_},
            {"messages", json::array()},
            {"temperature", 0.7},
            {"max_tokens", 100}
        };
        
        for (const auto& msg : messages) {
            request["messages"].push_back(msg.to_json());
        }
        
        std::string request_str = request.dump();
        std::string response_data;
        
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, ("Authorization: Bearer " + api_key_).c_str());
        headers = curl_slist_append(headers, "Content-Type: application/json");
        
        curl_easy_setopt(curl_, CURLOPT_URL, api_url_.c_str());
        curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, request_str.c_str());
        curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response_data);
        curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYHOST, 2L);
        
        CURLcode res = curl_easy_perform(curl_);
        curl_slist_free_all(headers);
        
        if (res != CURLE_OK) {
            throw std::runtime_error(std::string("CURL request failed: ") + 
                                   curl_easy_strerror(res));
        }
        
        try {
            json response = json::parse(response_data);
            if (response.contains("choices") && 
                !response["choices"].empty() && 
                response["choices"][0].contains("message") &&
                response["choices"][0]["message"].contains("content")) {
                return response["choices"][0]["message"]["content"];
            }
            spdlog::error("Unexpected response format: {}", response_data);
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
    
    CURL* curl_;
    std::string api_key_;
    std::string api_url_;
    std::string model_;
};

} // namespace ainovel
