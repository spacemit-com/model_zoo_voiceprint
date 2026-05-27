/*
 * Copyright (C) 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * WAV 文件读取与 FIR 重采样
 */

#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <algorithm>
#include <fstream>
#include <iostream>
#include "wave_reader_internal.h"

namespace SpacemiT {

constexpr int kTargetSampleRate = 16000;
constexpr int kDefaultLowpassTaps = 63;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static std::vector<float> DesignLowpassKernel(int taps, double cutoff_ratio) {
    if (taps < 3) {
        taps = 3;
    }
    if (taps % 2 == 0) {
        taps += 1;  // 保持对称
    }

    std::vector<float> kernel(static_cast<size_t>(taps));
    int mid = taps / 2;

    for (int n = 0; n < taps; ++n) {
        double rel = static_cast<double>(n - mid);
        double value;
        if (std::abs(rel) < 1e-8) {
            value = 2.0 * cutoff_ratio;
        } else {
            value = std::sin(2.0 * M_PI * cutoff_ratio * rel) / (M_PI * rel);
        }
        double window = 0.54 - 0.46 * std::cos(2.0 * M_PI * n / (taps - 1));
        kernel[static_cast<size_t>(n)] = static_cast<float>(value * window);
    }

    double sum = 0.0;
    for (float v : kernel) {
        sum += v;
    }
    if (std::abs(sum) > 1e-12) {
        for (float& v : kernel) {
            v = static_cast<float>(v / sum);
        }
    }

    return kernel;
}

static std::vector<float> ApplyFIRFilter(const std::vector<float>& input,
        const std::vector<float>& kernel) {
    if (input.empty() || kernel.empty()) {
        return input;
    }

    int taps = static_cast<int>(kernel.size());
    int half = taps / 2;
    std::vector<float> output(input.size(), 0.0f);

    for (size_t n = 0; n < input.size(); ++n) {
        double acc = 0.0;
        for (int k = 0; k < taps; ++k) {
            int idx = static_cast<int>(n) + k - half;
            if (idx >= 0 && idx < static_cast<int>(input.size())) {
                acc += input[static_cast<size_t>(idx)] * kernel[static_cast<size_t>(k)];
            }
        }
        output[n] = static_cast<float>(acc);
    }

    return output;
}

static std::vector<float> ResampleMono(const std::vector<float>& input,
                                int input_rate,
                                int output_rate) {
    if (input_rate == output_rate || input.empty()) {
        return input;
    }

    std::vector<float> processed = input;

    if (input_rate > output_rate && input.size() > 1) {
        double cutoff_ratio = 0.5 * (static_cast<double>(output_rate) / static_cast<double>(input_rate));
        cutoff_ratio = std::min(cutoff_ratio * 0.9, 0.5);
        if (cutoff_ratio > 0.0) {
            auto kernel = DesignLowpassKernel(kDefaultLowpassTaps, cutoff_ratio);
            processed = ApplyFIRFilter(processed, kernel);
        }
    }

    double ratio = static_cast<double>(input_rate) / static_cast<double>(output_rate);
    size_t output_length = static_cast<size_t>(std::floor(static_cast<double>(processed.size()) / ratio));
    if (output_length == 0) {
        return processed;
    }

    std::vector<float> output(output_length);

    for (size_t i = 0; i < output_length; ++i) {
        double src_pos = static_cast<double>(i) * ratio;
        size_t idx = static_cast<size_t>(src_pos);
        double frac = src_pos - static_cast<double>(idx);
        if (idx + 1 < processed.size()) {
            float a = processed[idx];
            float b = processed[idx + 1];
            output[i] = static_cast<float>((1.0 - frac) * a + frac * b);
        } else {
            output[i] = processed[idx];
        }
    }

    return output;
}

struct WaveHeader {
    uint32_t chunk_id;
    uint32_t chunk_size;
    uint32_t format;
    uint32_t subchunk1_id;
    uint32_t subchunk1_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    uint32_t subchunk2_id;
    uint32_t subchunk2_size;
};

std::vector<float> WaveReader::Resample(const std::vector<float>& samples,
                                        int input_rate, int output_rate) {
    return ResampleMono(samples, input_rate, output_rate);
}

std::unique_ptr<Wave> WaveReader::ReadFile(const std::string& filename,
        int speech_channel) {
    if (speech_channel < 1) {
        std::cerr << "Invalid speech channel: " << speech_channel << std::endl;
        return nullptr;
    }

    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return nullptr;
    }

    WaveHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(WaveHeader));

    if (!file || file.gcount() != sizeof(WaveHeader)) {
        std::cerr << "Failed to read WAV header" << std::endl;
        return nullptr;
    }

    // Check RIFF header
    if (header.chunk_id != 0x46464952) {  // "RIFF"
        std::cerr << "Invalid RIFF header" << std::endl;
        return nullptr;
    }

    // Check WAVE format
    if (header.format != 0x45564157) {  // "WAVE"
        std::cerr << "Invalid WAVE format" << std::endl;
        return nullptr;
    }

    // Handle JUNK chunk if present
    if (header.subchunk1_id == 0x4B4E554A) {  // "JUNK"
        file.seekg(header.subchunk1_size, std::ios::cur);
        file.read(reinterpret_cast<char*>(&header.subchunk1_id), 4);
        file.read(reinterpret_cast<char*>(&header.subchunk1_size), 4);
        file.read(reinterpret_cast<char*>(&header.audio_format), 2);
        file.read(reinterpret_cast<char*>(&header.num_channels), 2);
        file.read(reinterpret_cast<char*>(&header.sample_rate), 4);
        file.read(reinterpret_cast<char*>(&header.byte_rate), 4);
        file.read(reinterpret_cast<char*>(&header.block_align), 2);
        file.read(reinterpret_cast<char*>(&header.bits_per_sample), 2);
        file.read(reinterpret_cast<char*>(&header.subchunk2_id), 4);
        file.read(reinterpret_cast<char*>(&header.subchunk2_size), 4);

        if (!file) {
            std::cerr << "Failed to read extended WAV header" << std::endl;
            return nullptr;
        }
    }

    // Check fmt chunk
    if (header.subchunk1_id != 0x20746d66) {  // "fmt "
        std::cerr << "Invalid fmt chunk" << std::endl;
        return nullptr;
    }

    // Handle WAVE_FORMAT_EXTENSIBLE (format code 0xFFFE)
    if (header.audio_format == 0xFFFE) {
        // Read SubFormat GUID (16 bytes) — file pointer is at byte 44
        uint8_t sub_format[16];
        file.read(reinterpret_cast<char*>(sub_format), 16);
        if (!file) {
            std::cerr << "Failed to read WAVE_FORMAT_EXTENSIBLE SubFormat" << std::endl;
            return nullptr;
        }

        // Extract actual format code from SubFormat (first 2 bytes, little-endian)
        header.audio_format = static_cast<uint16_t>(sub_format[0] | (sub_format[1] << 8));

        // Skip any remaining fmt extension bytes (subchunk1_size > 40)
        if (header.subchunk1_size > 40) {
            file.seekg(header.subchunk1_size - 40, std::ios::cur);
        }

        // Re-read the next chunk header (subchunk2_id was filled with extension data)
        file.read(reinterpret_cast<char*>(&header.subchunk2_id), 4);
        file.read(reinterpret_cast<char*>(&header.subchunk2_size), 4);
        if (!file) {
            std::cerr << "Failed to read chunk after WAVE_FORMAT_EXTENSIBLE" << std::endl;
            return nullptr;
        }
    }

    // Check audio format (1 = PCM, 3 = IEEE float)
    if (header.audio_format != 1 && header.audio_format != 3) {
        std::cerr << "Unsupported audio format: " << header.audio_format << std::endl;
        return nullptr;
    }

    if (header.num_channels == 0) {
        std::cerr << "Invalid channel count: 0" << std::endl;
        return nullptr;
    }
    if (speech_channel > header.num_channels) {
        std::cerr << "Speech channel " << speech_channel
            << " out of range for " << header.num_channels
            << " channel(s): " << filename << std::endl;
        return nullptr;
    }

    // Check bits per sample
    if (header.bits_per_sample != 16 && header.bits_per_sample != 8 && header.bits_per_sample != 32) {
        std::cerr << "Unsupported bits per sample: " << header.bits_per_sample << std::endl;
        return nullptr;
    }

    // Handle extended format
    if (header.subchunk1_size == 18) {
        int16_t extra_size;
        file.read(reinterpret_cast<char*>(&extra_size), 2);
        if (!file || extra_size != 0) {
            std::cerr << "Extra size should be 0" << std::endl;
            return nullptr;
        }
    }

    // Find data chunk
    while (header.subchunk2_id != 0x61746164) {  // "data"
        file.seekg(header.subchunk2_size, std::ios::cur);
        file.read(reinterpret_cast<char*>(&header.subchunk2_id), 4);
        if (!file) break;
        file.read(reinterpret_cast<char*>(&header.subchunk2_size), 4);
        if (!file) break;
    }

    if (header.subchunk2_id != 0x61746164) {  // "data"
        std::cerr << "data chunk not found" << std::endl;
        return nullptr;
    }

    // Read samples
    if (header.bits_per_sample == 0) {
        std::cerr << "Invalid bits per sample: 0" << std::endl;
        return nullptr;
    }
    int32_t num_samples = header.subchunk2_size / (header.bits_per_sample / 8);
    std::vector<float> samples(num_samples);

    if (header.bits_per_sample == 16) {
        // Read and convert directly without extra allocation
        std::vector<int16_t> raw_samples(num_samples);
        file.read(reinterpret_cast<char*>(raw_samples.data()), num_samples * sizeof(int16_t));
        if (!file) {
            std::cerr << "Failed to read audio samples" << std::endl;
            return nullptr;
        }
        constexpr float scale = 1.0f / 32768.0f;
        for (int32_t i = 0; i < num_samples; i++) {
            samples[i] = raw_samples[i] * scale;
        }
    } else if (header.bits_per_sample == 8) {
        std::vector<uint8_t> raw_samples(num_samples);
        file.read(reinterpret_cast<char*>(raw_samples.data()), num_samples);
        if (!file) {
            std::cerr << "Failed to read audio samples" << std::endl;
            return nullptr;
        }
        constexpr float scale = 1.0f / 128.0f;
        for (int32_t i = 0; i < num_samples; i++) {
            samples[i] = (raw_samples[i] - 128) * scale;
        }
    } else if (header.bits_per_sample == 32) {
        if (header.audio_format == 3) {  // IEEE float
            file.read(reinterpret_cast<char*>(samples.data()), num_samples * sizeof(float));
            if (!file) {
                std::cerr << "Failed to read float audio samples" << std::endl;
                return nullptr;
            }
        } else {  // 32-bit PCM
            std::vector<int32_t> raw_samples(num_samples);
            file.read(reinterpret_cast<char*>(raw_samples.data()), num_samples * sizeof(int32_t));
            if (!file) {
                std::cerr << "Failed to read audio samples" << std::endl;
                return nullptr;
            }
            constexpr float scale = 1.0f / 2147483648.0f;
            for (int32_t i = 0; i < num_samples; i++) {
                samples[i] = raw_samples[i] * scale;
            }
        }
    }

    if (header.num_channels != 1) {
        int channels = header.num_channels;
        if (channels > 0) {
            size_t frames = samples.size() / static_cast<size_t>(channels);
            std::vector<float> mono;
            mono.reserve(frames);
            const size_t selected_channel = static_cast<size_t>(speech_channel - 1);
            for (size_t i = 0; i < frames; ++i) {
                mono.push_back(samples[i * channels + selected_channel]);
            }
            samples = std::move(mono);
        }
    }

    int final_sample_rate = header.sample_rate;
    if (final_sample_rate != kTargetSampleRate) {
        auto resampled = ResampleMono(samples, final_sample_rate, kTargetSampleRate);
        if (!resampled.empty()) {
            samples = std::move(resampled);
            final_sample_rate = kTargetSampleRate;
        }
    }

    return std::make_unique<Wave>(std::move(samples), final_sample_rate);
}

}  // namespace SpacemiT
