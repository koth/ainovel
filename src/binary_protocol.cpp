#include "binary_protocol.hpp"
#include <stdexcept>

namespace ainovel {

BinaryMessage::BinaryMessage(MessageType type, const std::vector<uint8_t>& payload) {
    packet_.type = static_cast<uint8_t>(type);
    packet_.reserved = 0;
    packet_.payload_size = payload.size();
    packet_.payload.assign(payload.begin(), payload.end());
}

BinaryMessage::BinaryMessage(const std::vector<uint8_t>& data) {
    packet_.type = data[0];
    packet_.reserved = data[1];
    packet_.payload_size = data[2] << 8 | data[3];
    packet_.payload.insert(packet_.payload.begin(), data.data()+4, data.data()+4+packet_.payload_size);
    if (packet_.payload.size() != packet_.payload_size) {
        throw std::runtime_error("Invalid binary message: payload size mismatch");
    }
}

std::vector<uint8_t> BinaryMessage::serialize() const {
    std::vector<uint8_t> result;
    result.reserve(4+ packet_.payload_size);
    result.push_back(packet_.type);
    result.push_back(packet_.reserved);
    result.push_back(packet_.payload_size >> 8);
    result.push_back(packet_.payload_size & 0xFF);
    // Copy payload
    result.insert(result.end(), packet_.payload.begin(), packet_.payload.end());

    return result;
}

std::string to_string(ClientState state) {
    switch (state) {
        case ClientState::IDLE: return "idle";
        case ClientState::WAKE_WORD_DETECTED: return "wake_word_detected";
        case ClientState::LISTENING: return "listening";
        case ClientState::SPEAKING: return "speaking";
        default: return "unknown";
    }
}

std::string to_string(MessageType type){
    switch (type) {
        case MessageType::AUDIO: return "audio";
        case MessageType::JSON: return "json";
        default: return "unknown";
    }
}

std::string to_string(ResponseMode mode) {
    switch (mode) {
        case ResponseMode::AUTO: return "auto";
        case ResponseMode::MANUAL: return "manual";
        case ResponseMode::REAL_TIME: return "real_time";
        default: return "unknown";
    }
}

ClientState state_from_string(const std::string& state) {
    if (state == "idle") return ClientState::IDLE;
    if (state == "wake_word_detected") return ClientState::WAKE_WORD_DETECTED;
    if (state == "listening") return ClientState::LISTENING;
    if (state == "speaking") return ClientState::SPEAKING;
    throw std::runtime_error("Invalid client state: " + state);
}

ResponseMode mode_from_string(const std::string& mode) {
    if (mode == "auto") return ResponseMode::AUTO;
    if (mode == "manual") return ResponseMode::MANUAL;
    if (mode == "real_time") return ResponseMode::REAL_TIME;
    throw std::runtime_error("Invalid response mode: " + mode);
}

} // namespace ainovel
