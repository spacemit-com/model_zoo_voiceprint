/*
 * Copyright (C) 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * 音频录制接口
 */

#ifndef AUDIO_RECORDER_H
#define AUDIO_RECORDER_H

#include <vector>
#include <cstdint>
#include <string>

struct DeviceInfo {
    int index;
    std::string name;
    int max_input_channels;
    std::vector<int> supported_input_rates;
};

class AudioRecorder {
public:
    AudioRecorder();
    ~AudioRecorder();

    static std::vector<DeviceInfo> ListInputDevices(std::string* error = nullptr);

    bool Initialize(int sample_rate, int channels = 1, int device_index = -1);
    bool IsInitialized() const { return initialized_; }

    bool Record(float duration_seconds, std::vector<float>& out_samples);
    std::string GetLastError() const { return last_error_; }

private:
    bool initialized_ = false;
    int sample_rate_ = 16000;
    int channels_ = 1;
    int device_index_ = -1;
    bool pa_initialized_ = false;
    std::string last_error_;
};

#endif  // AUDIO_RECORDER_H
