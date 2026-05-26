/*
 * Copyright (C) 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * WAV 读取内部数据结构
 */

#ifndef WAVE_READER_INTERNAL_H
#define WAVE_READER_INTERNAL_H

#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <cstdint>

namespace SpacemiT {

struct Wave {
    std::vector<float> samples;
    int32_t sample_rate;

    Wave() : sample_rate(0) {}
    Wave(std::vector<float> samples, int32_t rate)
        : samples(std::move(samples)), sample_rate(rate) {}
};

class WaveReader {
public:
    static std::unique_ptr<Wave> ReadFile(const std::string& filename,
        int speech_channel = 1);
    static std::vector<float> Resample(const std::vector<float>& samples,
        int input_rate, int output_rate);
};

}  // namespace SpacemiT

#endif  // WAVE_READER_INTERNAL_H
