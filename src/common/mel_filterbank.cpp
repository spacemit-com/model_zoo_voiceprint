/*
 * Copyright (C) 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Kaldi 对齐 Mel 频谱特征提取（公共模块，供多后端复用）
 */

#include <cmath>
#include <cstring>
#include <utility>
#include <vector>
#include <algorithm>
#include <complex>
#include <limits>
#include "mel_filterbank.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace SpacemiT {

void MelFilterbank::FFT(std::vector<std::complex<float>>& data) {
    int n = data.size();
    if (n <= 1) return;

    int bits = 0;
    while ((1 << bits) < n) bits++;

    for (int i = 1; i < n - 1; i++) {
        int rev = 0;
        int x = i;
        for (int j = 0; j < bits; j++) {
            rev = (rev << 1) | (x & 1);
            x >>= 1;
        }
        if (i < rev) {
            std::swap(data[i], data[rev]);
        }
    }

    for (int len = 2; len <= n; len <<= 1) {
        float ang = -2.0f * M_PI / len;
        float cos_ang = std::cos(ang);
        float sin_ang = std::sin(ang);
        int halflen = len >> 1;

        for (int i = 0; i < n; i += len) {
            float cos_w = 1.0f;
            float sin_w = 0.0f;

            for (int j = 0; j < halflen; j++) {
                std::complex<float> w(cos_w, sin_w);
                std::complex<float> u = data[i + j];
                std::complex<float> v = data[i + j + halflen] * w;
                data[i + j] = u + v;
                data[i + j + halflen] = u - v;

                float tmp = cos_w * cos_ang - sin_w * sin_ang;
                sin_w = sin_w * cos_ang + cos_w * sin_ang;
                cos_w = tmp;
            }
        }
    }
}

int MelFilterbank::NextPow2(int n) {
    int power = 1;
    while (power < n) power <<= 1;
    return power;
}

float MelFilterbank::HzToMel(float hz) {
    return 2595.0f * std::log10(1.0f + hz / 700.0f);
}

float MelFilterbank::MelToHz(float mel) {
    return 700.0f * (std::pow(10.0f, mel / 2595.0f) - 1.0f);
}

std::vector<std::vector<float>> MelFilterbank::CreateMelFilterbank(
    int num_filters, int fft_size, float sample_rate) {

    int num_fft_bins = fft_size / 2;  // Kaldi uses N/2 (excludes Nyquist)
    std::vector<std::vector<float>> filterbank(num_filters,
        std::vector<float>(num_fft_bins, 0.0f));

    float low_freq = 20.0f;
    float high_freq = sample_rate / 2.0f;
    float low_mel = HzToMel(low_freq);
    float high_mel = HzToMel(high_freq);
    float fft_bin_width = sample_rate / static_cast<float>(fft_size);
    float mel_freq_delta = (high_mel - low_mel) / (num_filters + 1);

    // Kaldi-style: iterate FFT bins and interpolate in mel domain
    for (int m = 0; m < num_filters; m++) {
        float left_mel   = low_mel + m * mel_freq_delta;
        float center_mel = low_mel + (m + 1) * mel_freq_delta;
        float right_mel  = low_mel + (m + 2) * mel_freq_delta;

        for (int i = 0; i < num_fft_bins; i++) {
            float mel = HzToMel(fft_bin_width * i);
            if (mel > left_mel && mel < right_mel) {
                filterbank[m][i] = (mel <= center_mel)
                    ? (mel - left_mel) / (center_mel - left_mel)
                    : (right_mel - mel) / (right_mel - center_mel);
            }
        }
    }

    return filterbank;
}

void MelFilterbank::PreEmphasis(std::vector<float>& signal, float coeff) {
    for (int i = signal.size() - 1; i > 0; i--) {
        signal[i] -= coeff * signal[i - 1];
    }
    signal[0] *= (1.0f - coeff);
}

void MelFilterbank::ApplyWindow(std::vector<float>& frame) {
    int frame_size = frame.size();
    for (int i = 0; i < frame_size; i++) {
        // Kaldi Povey window: pow(hann, 0.85)
        float hann = 0.5f - 0.5f * std::cos(2.0f * M_PI * i / (frame_size - 1));
        frame[i] *= std::pow(hann, 0.85f);
    }
}

std::vector<float> MelFilterbank::ComputePowerSpectrum(
    const std::vector<std::complex<float>>& fft_result) {

    int fft_size = fft_result.size();
    std::vector<float> power_spec(fft_size / 2 + 1);

    for (int i = 0; i <= fft_size / 2; i++) {
        float real = fft_result[i].real();
        float imag = fft_result[i].imag();
        power_spec[i] = real * real + imag * imag;
    }

    return power_spec;
}

std::vector<float> MelFilterbank::ApplyMelFilterbank(
    const std::vector<float>& power_spec,
    const std::vector<std::vector<float>>& filterbank) {

    int num_filters = filterbank.size();
    int spec_size = power_spec.size();
    std::vector<float> mel_energies(num_filters);

    for (int m = 0; m < num_filters; m++) {
        float energy = 0.0f;
        const float* filter_ptr = filterbank[m].data();
        const float* spec_ptr = power_spec.data();
        int filter_size = static_cast<int>(filterbank[m].size());
        int len = std::min(spec_size, filter_size);

        for (int k = 0; k < len; k++) {
            energy += spec_ptr[k] * filter_ptr[k];
        }

        mel_energies[m] = std::log(std::max(energy, std::numeric_limits<float>::epsilon()));
    }

    return mel_energies;
}

std::vector<float> MelFilterbank::ComputeMelSpectrogram(
    const std::vector<float>& samples,
    int32_t sample_rate,
    int32_t& num_frames,
    int32_t& num_mel_bins) {

    const int frame_size = 400;
    const int hop_size = 160;
    num_mel_bins = 80;
    const float pre_emphasis_coeff = 0.97f;

    int num_samples = samples.size();
    num_frames = (num_samples - frame_size) / hop_size + 1;
    if (num_frames <= 0) {
        num_frames = 1;
    }

    int fft_size = NextPow2(frame_size);
    auto filterbank = CreateMelFilterbank(num_mel_bins, fft_size, static_cast<float>(sample_rate));

    std::vector<float> mel_spec(num_frames * num_mel_bins);

    for (int frame = 0; frame < num_frames; frame++) {
        int start_idx = frame * hop_size;
        int end_idx = std::min(start_idx + frame_size, num_samples);
        int copy_size = std::min(end_idx - start_idx, frame_size);

        // Pre-emphasis and windowing on frame_size (400) samples
        std::vector<float> frame_data(frame_size, 0.0f);
        std::copy_n(samples.data() + start_idx, copy_size, frame_data.data());

        // Remove DC offset (Kaldi default: remove_dc_offset=true)
        float mean = 0.0f;
        for (int i = 0; i < copy_size; i++) mean += frame_data[i];
        mean /= frame_size;
        for (int i = 0; i < copy_size; i++) frame_data[i] -= mean;

        PreEmphasis(frame_data, pre_emphasis_coeff);
        ApplyWindow(frame_data);

        // Zero-pad to fft_size (512) for FFT
        std::vector<std::complex<float>> fft_data(fft_size, {0.0f, 0.0f});
        for (int i = 0; i < frame_size; i++) {
            fft_data[i] = {frame_data[i], 0.0f};
        }

        FFT(fft_data);
        auto power_spec = ComputePowerSpectrum(fft_data);
        auto mel_energies = ApplyMelFilterbank(power_spec, filterbank);

        std::copy(mel_energies.begin(), mel_energies.end(),
            mel_spec.begin() + frame * num_mel_bins);
    }

    return mel_spec;
}

std::vector<float> MelFilterbank::ComputeFbank(
    const std::vector<float>& samples,
    int32_t sample_rate,
    std::vector<int64_t>& shape) {

    int32_t num_frames, num_mel_bins;
    auto mel_spec = ComputeMelSpectrogram(samples, sample_rate, num_frames, num_mel_bins);

    // Utterance-level CMVN: subtract per-bin mean across all frames
    std::vector<float> mean(num_mel_bins, 0.0f);
    for (int f = 0; f < num_frames; f++) {
        for (int b = 0; b < num_mel_bins; b++) {
            mean[b] += mel_spec[f * num_mel_bins + b];
        }
    }
    for (int b = 0; b < num_mel_bins; b++) {
        mean[b] /= num_frames;
    }
    for (int f = 0; f < num_frames; f++) {
        for (int b = 0; b < num_mel_bins; b++) {
            mel_spec[f * num_mel_bins + b] -= mean[b];
        }
    }

    shape.resize(3);
    shape[0] = 1;
    shape[1] = num_frames;
    shape[2] = num_mel_bins;

    return mel_spec;
}

}  // namespace SpacemiT
