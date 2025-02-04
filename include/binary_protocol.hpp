#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <memory>

#ifdef _WIN32
    #include <WinSock2.h>
#else
    #include <arpa/inet.h>
#endif

namespace ainovel {

struct BinaryPacket {
    uint8_t type;           // 消息类型（0：音频流数据，1：JSON）
    uint8_t reserved;       // 保留字段
    uint16_t payload_size;  // 负载大小（Big Endian）
    std::vector<uint8_t> payload;      // 负载数据
};

enum class MessageType : uint8_t {
    AUDIO = 0,
    JSON = 1
};

enum class ClientState {
    IDLE,
    WAKE_WORD_DETECTED,
    LISTENING,
    SPEAKING
};

enum class ResponseMode {
    AUTO,
    MANUAL,
    REAL_TIME
};

class BinaryMessage {
public:
    BinaryMessage(MessageType type, const std::vector<uint8_t>& payload);
    BinaryMessage(const std::vector<uint8_t>& data); // 从完整的二进制数据构造

    std::vector<uint8_t> serialize() const;
    const std::vector<uint8_t>& payload() const { return packet_.payload; }
    MessageType type() const { return static_cast<MessageType>(packet_.type); }

private:
    BinaryPacket packet_;
};

// 辅助函数
std::string to_string(ClientState state);
std::string to_string(MessageType type);
std::string to_string(ResponseMode mode);
ClientState state_from_string(const std::string& state);
ResponseMode mode_from_string(const std::string& mode);

} // namespace ainovel
