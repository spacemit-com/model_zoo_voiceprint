/*
 * Copyright (C) 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * PortAudio 音频录制实现
 */

#include <portaudio.h>
#include <cstdio>
#include <utility>
#include <vector>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>
#include "audio_recorder.h"

#if defined(_WIN32)
#include <io.h>
#include <fcntl.h>
#else
#include <unistd.h>
#endif

namespace {

class ScopedStderrSilencer {
public:
    ScopedStderrSilencer() {
#if defined(_WIN32)
        original_fd_ = _dup(_fileno(stderr));
        if (original_fd_ == -1) return;
        FILE* null_file = _fdopen(_open("nul", _O_WRONLY), "w");
        if (!null_file) {
            _close(original_fd_); original_fd_ = -1; return;
        }
        null_stream_ = null_file;
        fflush(stderr);
        _dup2(_fileno(null_stream_), _fileno(stderr));
#else
        original_fd_ = dup(fileno(stderr));
        if (original_fd_ == -1) return;
        null_stream_ = fopen("/dev/null", "w");
        if (!null_stream_) {
            close(original_fd_); original_fd_ = -1; return;
        }
        fflush(stderr);
        dup2(fileno(null_stream_), fileno(stderr));
#endif
    }

    ~ScopedStderrSilencer() {
#if defined(_WIN32)
        if (original_fd_ != -1) {
            fflush(stderr); _dup2(original_fd_, _fileno(stderr)); _close(original_fd_);
        }
        if (null_stream_) fclose(null_stream_);
#else
        if (original_fd_ != -1) {
            fflush(stderr); dup2(original_fd_, fileno(stderr)); close(original_fd_);
        }
        if (null_stream_) fclose(null_stream_);
#endif
    }

private:
    int original_fd_ = -1;
    FILE* null_stream_ = nullptr;
};

struct RecorderState {
    std::vector<float>* buffer;
    int channels;
};

int RecordCallback(const void* input_buffer, void*, uint64_t frames_per_buffer,
        const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags,
        void* user_data) {
    auto* state = static_cast<RecorderState*>(user_data);
    const float* in = static_cast<const float*>(input_buffer);

    if (!in) {
        state->buffer->insert(state->buffer->end(), frames_per_buffer * state->channels, 0.0f);
    } else {
        state->buffer->insert(state->buffer->end(), in, in + frames_per_buffer * state->channels);
    }

    return paContinue;
}

}  // namespace

AudioRecorder::AudioRecorder() = default;
AudioRecorder::~AudioRecorder() {
    if (pa_initialized_) {
        Pa_Terminate();
        pa_initialized_ = false;
    }
}

std::vector<DeviceInfo> AudioRecorder::ListInputDevices(std::string* error) {
    std::vector<DeviceInfo> devices;
    if (error) {
        error->clear();
    }

    ScopedStderrSilencer silencer;

    PaError err = Pa_Initialize();
    if (err != paNoError) {
        if (error) {
            *error = Pa_GetErrorText(err);
        }
        return devices;
    }

    static const int kProbeRates[] = {8000, 16000, 22050, 44100, 48000, 96000};

    int count = Pa_GetDeviceCount();
    if (count < 0) {
        if (error) {
            *error = Pa_GetErrorText(count);
        }
        Pa_Terminate();
        return devices;
    }

    for (int i = 0; i < count; i++) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (info && info->maxInputChannels > 0) {
            PaStreamParameters params;
            params.device = i;
            params.channelCount = std::min(info->maxInputChannels, 2);
            params.sampleFormat = paFloat32;
            params.suggestedLatency = info->defaultLowInputLatency;
            params.hostApiSpecificStreamInfo = nullptr;

            std::vector<int> rates;
            for (int rate : kProbeRates) {
                if (Pa_IsFormatSupported(&params, nullptr, rate) == paFormatIsSupported) {
                    rates.push_back(rate);
                }
            }

            devices.push_back({
                i,
                info->name ? info->name : "",
                info->maxInputChannels,
                std::move(rates)
            });
        }
    }

    Pa_Terminate();
    return devices;
}

bool AudioRecorder::Initialize(int sample_rate, int channels, int device_index) {
    sample_rate_ = sample_rate;
    channels_ = channels;
    device_index_ = device_index;

    {
        ScopedStderrSilencer silencer;
        PaError err = Pa_Initialize();
        if (err != paNoError) {
            last_error_ = Pa_GetErrorText(err);
            return false;
        }
    }
    pa_initialized_ = true;

    initialized_ = true;
    return true;
}

bool AudioRecorder::Record(float duration_seconds, std::vector<float>& out_samples) {
    if (!initialized_) {
        last_error_ = "Recorder not initialized";
        return false;
    }

    PaStreamParameters input_params;
    if (device_index_ >= 0) {
        input_params.device = device_index_;
    } else {
        input_params.device = Pa_GetDefaultInputDevice();
    }
    if (input_params.device == paNoDevice) {
        last_error_ = "No input device available";
        return false;
    }
    input_params.channelCount = channels_;
    input_params.sampleFormat = paFloat32;
    input_params.suggestedLatency = Pa_GetDeviceInfo(input_params.device)->defaultLowInputLatency;
    input_params.hostApiSpecificStreamInfo = nullptr;

    RecorderState state{&out_samples, channels_};
    out_samples.clear();
    out_samples.reserve(static_cast<size_t>(duration_seconds * sample_rate_) * channels_);

    PaStream* stream = nullptr;
    PaError err = Pa_OpenStream(&stream, &input_params, nullptr, sample_rate_, paFramesPerBufferUnspecified,
                                paClipOff, RecordCallback, &state);
    if (err != paNoError) {
        last_error_ = Pa_GetErrorText(err);
        return false;
    }

    err = Pa_StartStream(stream);
    if (err != paNoError) {
        last_error_ = Pa_GetErrorText(err);
        Pa_CloseStream(stream);
        return false;
    }

    auto deadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(static_cast<int>(duration_seconds * 1000));

    while (Pa_IsStreamActive(stream) == 1) {
        if (std::chrono::steady_clock::now() >= deadline) {
            err = Pa_StopStream(stream);
            break;
        }
        Pa_Sleep(10);
    }

    if (err != paNoError) {
        last_error_ = Pa_GetErrorText(err);
    }

    Pa_CloseStream(stream);

    return err == paNoError;
}
