#pragma once

#include <opus/opus.h>
#include <vector>
#include <memory>
#include <stdexcept>

namespace ainovel {

class OpusDecoder {
public:
    OpusDecoder(int sample_rate = 16000, int channels = 1) {
        int error;
        decoder_ = opus_decoder_create(sample_rate, channels, &error);
        if (error != OPUS_OK || !decoder_) {
            throw std::runtime_error("Failed to create Opus decoder: " + std::to_string(error));
        }
    }

    ~OpusDecoder() {
        if (decoder_) {
            opus_decoder_destroy(decoder_);
        }
    }

    // 解码一帧Opus数据
    std::vector<float> decode_float(const std::vector<uint8_t>& opus_data) {
        // Opus帧大小通常是2.5ms, 5ms, 10ms, 20ms, 40ms或60ms
        // 对于16kHz采样率，每毫秒有16个样本
        // 我们使用60ms的帧大小作为最大值
        const int max_frame_size = 60 * 16; // 60ms * 16 samples/ms
        std::vector<float> pcm(max_frame_size);
        
        int samples = opus_decode_float(decoder_, 
                                      opus_data.data(), 
                                      opus_data.size(),
                                      pcm.data(),
                                      max_frame_size,
                                      0);
        
        if (samples < 0) {
            throw std::runtime_error("Failed to decode Opus data: " + std::to_string(samples));
        }
        
        pcm.resize(samples);
        return pcm;
    }

private:
    OpusDecoder(const OpusDecoder&) = delete;
    OpusDecoder& operator=(const OpusDecoder&) = delete;

    ::OpusDecoder* decoder_{nullptr};
};

} // namespace ainovel
