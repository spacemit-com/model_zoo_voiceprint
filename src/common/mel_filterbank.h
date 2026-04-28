/*
 * Copyright (C) 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Mel 滤波器组接口（公共模块，供多后端复用）
 */

#ifndef MEL_FILTERBANK_H
#define MEL_FILTERBANK_H

#include <vector>
#include <cstdint>
#include <complex>

namespace SpacemiT {

class MelFilterbank {
public:
    static std::vector<float> ComputeFbank(
        const std::vector<float>& samples,
        int32_t sample_rate,
        std::vector<int64_t>& shape);

private:
    static std::vector<float> ComputeMelSpectrogram(
        const std::vector<float>& samples,
        int32_t sample_rate,
        int32_t& num_frames,
        int32_t& num_mel_bins);

    static void FFT(std::vector<std::complex<float>>& data);
    static int NextPow2(int n);
    static float HzToMel(float hz);
    static float MelToHz(float mel);
    static std::vector<std::vector<float>> CreateMelFilterbank(
        int num_filters, int fft_size, float sample_rate);
    static void PreEmphasis(std::vector<float>& signal, float coeff);
    static void ApplyWindow(std::vector<float>& frame);
    static std::vector<float> ComputePowerSpectrum(
        const std::vector<std::complex<float>>& fft_result);
    static std::vector<float> ApplyMelFilterbank(
        const std::vector<float>& power_spec,
        const std::vector<std::vector<float>>& filterbank);
};

}  // namespace SpacemiT

#endif  // MEL_FILTERBANK_H
