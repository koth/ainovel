#pragma once

#include <common_audio/vad/include/webrtc_vad.h>
#include <vector>
#include <memory>
#include <stdexcept>

namespace ainovel {

class VadProcessor {
public:
    VadProcessor(int sample_rate = 16000, int mode = 0) : sample_rate_(sample_rate) {
        handle_ = WebRtcVad_Create();
        if (!handle_) {
            throw std::runtime_error("Failed to create VAD instance");
        }

        if (WebRtcVad_Init(handle_) != 0) {
            WebRtcVad_Free(handle_);
            throw std::runtime_error("Failed to initialize VAD");
        }

        // 设置VAD模式 (0-3)，数字越大越激进
        // 0: 最不激进，误报最少，但可能漏报
        // 3: 最激进，漏报最少，但可能误报
        if (WebRtcVad_set_mode(handle_, mode) != 0) {
            WebRtcVad_Free(handle_);
            throw std::runtime_error("Failed to set VAD mode");
        }
    }

    ~VadProcessor() {
        if (handle_) {
            WebRtcVad_Free(handle_);
        }
    }

    // 处理PCM数据，返回是否检测到语音活动
    bool process_frame(const std::vector<float>& frame) {
        // 将float转换为int16
        std::vector<int16_t> int16_data;
        int16_data.reserve(frame.size());
        for (float sample : frame) {
            int16_data.push_back(static_cast<int16_t>(sample * 32768.0f));
        }

        // 每帧20ms
        const int samples_per_frame = sample_rate_ / 50;  // 20ms = 1/50 秒
        size_t num_frames = int16_data.size() / samples_per_frame;

        // 计算有语音的帧数
        int voice_frames = 0;
        for (size_t i = 0; i < num_frames; ++i) {
            int result = WebRtcVad_Process(handle_,
                                         sample_rate_,
                                         int16_data.data() + i * samples_per_frame,
                                         samples_per_frame);
            if (result == 1) {
                voice_frames++;
            }
            else if (result < 0) {
                throw std::runtime_error("VAD processing failed");
            }
        }

        // 计算语音帧的比例，只有超过阈值才认为有语音
        constexpr float VOICE_THRESHOLD = 0.3f;  // 30%的帧检测到语音才算有声音
        return (num_frames > 0) &&
               (static_cast<float>(voice_frames) / num_frames >= VOICE_THRESHOLD);
    }

private:
    VadProcessor(const VadProcessor&) = delete;
    VadProcessor& operator=(const VadProcessor&) = delete;

    VadInst* handle_{nullptr};
    int sample_rate_;
};

} // namespace ainovel
