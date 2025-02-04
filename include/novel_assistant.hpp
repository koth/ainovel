#pragma once

#include "llm_client.hpp"
#include <string>
#include <vector>
#include <memory>

namespace ainovel {

class NovelAssistant {
public:
    NovelAssistant(const std::string& api_key) 
        : llm_client_(std::make_unique<LLMClient>(api_key)) {
        // 初始化系统提示词
        messages_.push_back({
            "system",
            "你是一个专业的网络小说助理，熟悉各大网站的网络小说信息。你可以：\n"
            "1. 推荐热门或符合特定要求的网络小说\n"
            "2. 解答关于网络小说的问题\n"
            "3. 分析网络小说的情节和写作特点\n"
            "请用简洁专业的语气回答问题，每次回答内容不超过100字。"
        });
    }
    
    std::string ask(const std::string& question) {
        // 添加用户问题
        messages_.push_back({"user", question});
        
        // 获取AI回答
        std::string response = llm_client_->chat(messages_);
        
        // 保存AI回答到历史记录
        messages_.push_back({"assistant", response});
        
        // 如果历史记录过长，删除最早的对话（保留system message）
        if (messages_.size() > 10) {  // 删除最老的4轮对话
            messages_.erase(messages_.begin() + 1, messages_.begin() + 3);
        }
        
        return response;
    }
    
    void reset_conversation() {
        // 重置对话，只保留system message
        messages_.erase(messages_.begin() + 1, messages_.end());
    }
    
private:
    std::unique_ptr<LLMClient> llm_client_;
    std::vector<LLMClient::Message> messages_;
};

} // namespace ainovel
