/*
 * Copyright (C) 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * 说话人注册 CLI 工具
 */

#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <utility>
#include <vector>
#include "../include/vp_service.h"
#include "../src/audio_recorder.h"
#include "../src/wave_reader_internal.h"

static const float kRecordingDurationSeconds = 4.0f;
static const int kRecordingRepeats = 3;
static const int kTargetSampleRate = 16000;

static void WaitForEnter(const std::string& prompt) {
    std::cout << prompt << std::flush;
    std::string dummy;
    std::getline(std::cin, dummy);
}

static bool RecordSamples(AudioRecorder& recorder,
        std::vector<float>& samples,
        int index) {
    WaitForEnter("按 Enter 键开始录音...");
    std::cout << "开始录音 " << (index + 1) << " / " << kRecordingRepeats
        << " (" << kRecordingDurationSeconds << " 秒)..." << std::endl;

    if (!recorder.Record(kRecordingDurationSeconds, samples)) {
        std::cerr << "录音失败: " << recorder.GetLastError() << std::endl;
        return false;
    }

    std::cout << "录音完成，样本数: " << samples.size() << "\n";
    return true;
}

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [OPTIONS] -n NAME [audio1.wav ...]\n";
    std::cout << "\nRegister a speaker with audio files or live recording.\n\n";
    std::cout << "Options:\n";
    std::cout << "  -n, --name NAME       Speaker name (required)\n";
    std::cout << "  -d, --database FILE   Database file (default: speakers.db)\n";
    std::cout << "  -t, --threads NUM     Number of threads (default: 1)\n";
    std::cout << "  -f, --force           Force overwrite if speaker exists\n";
    std::cout << "  -l, --list-devices    List available input devices\n";
    std::cout << "  -i, --input-device N  Select input device by index\n";
    std::cout << "  -r, --sample-rate N   Recording sample rate (default: 16000)\n";
    std::cout << "  -c, --channels N      Number of recording channels (default: 1)\n";
    std::cout << "  -h, --help            Show this help message\n";
    std::cout << "\nExamples:\n";
    std::cout << "  " << prog << " -l                              # List devices\n";
    std::cout << "  " << prog << " -n john john_sample.wav          # From file\n";
    std::cout << "  " << prog << " -n john                          # Live recording\n";
    std::cout << "  " << prog << " -n john -i 2 -r 48000 -c 2       # Device 2, 48kHz, stereo\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string database_file = "speakers.db";
    std::string speaker_name;
    int num_threads = 1;
    bool force_overwrite = false;
    bool list_devices = false;
    int device_index = -1;
    int recording_sample_rate = kTargetSampleRate;
    int recording_channels = 1;
    std::vector<std::string> audio_files;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--name") == 0) {
            if (i + 1 < argc) {
                speaker_name = argv[++i];
            } else {
                std::cerr << "Error: -n requires a speaker name\n";
                return 1;
            }
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--database") == 0) {
            if (i + 1 < argc) {
                database_file = argv[++i];
            } else {
                std::cerr << "Error: -d requires a database file path\n";
                return 1;
            }
        } else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--threads") == 0) {
            if (i + 1 < argc) {
                num_threads = std::stoi(argv[++i]);
            } else {
                std::cerr << "Error: -t requires a number\n";
                return 1;
            }
        } else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--force") == 0) {
            force_overwrite = true;
        } else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--list-devices") == 0) {
            list_devices = true;
        } else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--input-device") == 0) {
            if (i + 1 < argc) {
                device_index = std::stoi(argv[++i]);
            } else {
                std::cerr << "Error: -i requires a device index\n";
                return 1;
            }
        } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--sample-rate") == 0) {
            if (i + 1 < argc) {
                recording_sample_rate = std::stoi(argv[++i]);
            } else {
                std::cerr << "Error: -r requires a sample rate\n";
                return 1;
            }
        } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--channels") == 0) {
            if (i + 1 < argc) {
                recording_channels = std::stoi(argv[++i]);
            } else {
                std::cerr << "Error: -c requires a channel count\n";
                return 1;
            }
        } else if (argv[i][0] != '-') {
            audio_files.push_back(argv[i]);
        } else {
            std::cerr << "Unknown option: " << argv[i] << "\n";
            return 1;
        }
    }

    // List devices mode
    if (list_devices) {
        std::string error;
        auto devices = AudioRecorder::ListInputDevices(&error);
        if (!error.empty()) {
            std::cerr << "Failed to list input devices: " << error << "\n";
            return 1;
        }
        if (devices.empty()) {
            std::cout << "No input devices found.\n";
            return 1;
        }
        std::cout << "Available input devices:\n";
        for (const auto& dev : devices) {
            std::cout << "  [" << dev.index << "] " << dev.name
                << " (channels: " << dev.max_input_channels
                << ", rates:";
            for (int rate : dev.supported_input_rates) {
                std::cout << " " << rate;
            }
            if (dev.supported_input_rates.empty()) {
                std::cout << " none";
            }
            std::cout << ")\n";
        }
        return 0;
    }

    if (speaker_name.empty()) {
        std::cerr << "Error: Speaker name is required (-n NAME)\n";
        print_usage(argv[0]);
        return 1;
    }

    bool use_recording = audio_files.empty();
    if (!use_recording && audio_files.size() < static_cast<size_t>(kRecordingRepeats)) {
        std::cout << "提示: 建议至少提供 " << kRecordingRepeats << " 个音频样本以获得稳定效果\n";
    }

    // Create engine
    auto config = SpacemiT::VpConfig::Preset("campplus")
        .withNumThreads(num_threads);

    SpacemiT::VpEngine engine(config);
    if (!engine.IsInitialized()) {
        std::cerr << "Failed to initialize engine\n";
        return 1;
    }

    // Load existing database
    engine.LoadDatabase(database_file);

    // Check if speaker already exists
    if (engine.ContainsSpeaker(speaker_name) && !force_overwrite) {
        std::cerr << "Error: Speaker '" << speaker_name << "' already exists in database.\n";
        std::cerr << "Use -f to force overwrite.\n";
        return 1;
    }

    if (force_overwrite && engine.ContainsSpeaker(speaker_name)) {
        engine.RemoveSpeaker(speaker_name);
    }

    bool success = false;

    if (use_recording) {
        // Live recording mode
        AudioRecorder recorder;
        if (!recorder.Initialize(recording_sample_rate, recording_channels, device_index)) {
            std::cerr << "录音初始化失败: " << recorder.GetLastError() << "\n";
            return 1;
        }

        std::cout << "进入录音模式，将录制 " << kRecordingRepeats
            << " 次，每次 " << kRecordingDurationSeconds << " 秒。\n";
        if (recording_sample_rate != kTargetSampleRate) {
            std::cout << "录音采样率: " << recording_sample_rate
                << " Hz → 自动重采样到 " << kTargetSampleRate << " Hz\n";
        }
        WaitForEnter("按 Enter 开始...");

        std::vector<std::vector<float>> all_embeddings;
        for (int i = 0; i < kRecordingRepeats; ++i) {
            std::vector<float> samples;
            if (!RecordSamples(recorder, samples, i)) return 1;

            // Extract first channel if multi-channel
            if (recording_channels > 1) {
                size_t frames = samples.size() / recording_channels;
                std::vector<float> mono(frames);
                for (size_t f = 0; f < frames; f++) {
                    mono[f] = samples[f * recording_channels];
                }
                samples = std::move(mono);
                std::cout << "提取左声道为单声道，样本数: " << samples.size() << "\n";
            }

            // Resample to 16kHz if needed
            if (recording_sample_rate != kTargetSampleRate) {
                std::cout << "重采样 " << recording_sample_rate << " → " << kTargetSampleRate << " Hz...";
                samples = SpacemiT::WaveReader::Resample(samples, recording_sample_rate, kTargetSampleRate);
                std::cout << " 样本数: " << samples.size() << "\n";
            }

            auto result = engine.ExtractEmbedding(samples, kTargetSampleRate);
            if (!result->IsSuccess()) {
                std::cerr << "生成嵌入失败: " << result->GetErrorMessage() << "\n";
                return 1;
            }
            std::cout << "Embedding RTF: "
                << std::fixed << std::setprecision(4)
                << result->GetRTF() << "\n";
            all_embeddings.push_back(result->GetEmbedding());
        }

        // Register with averaged embeddings via RegisterWithEmbedding
        // Average them manually
        if (all_embeddings.empty()) return 1;
        int dim = static_cast<int>(all_embeddings[0].size());
        std::vector<float> avg(dim, 0.0f);
        for (const auto& e : all_embeddings) {
            for (int j = 0; j < dim; j++) avg[j] += e[j];
        }
        for (float& v : avg) v /= all_embeddings.size();
        // L2 normalize
        float norm = 0.0f;
        for (float v : avg) norm += v * v;
        norm = std::sqrt(norm);
        if (norm > 0) for (float& v : avg) v /= norm;

        success = engine.RegisterWithEmbedding(speaker_name, avg);
    } else {
        // File mode
        std::cout << "Processing " << audio_files.size() << " audio file(s) for speaker '"
            << speaker_name << "'...\n";

        if (audio_files.size() == 1) {
            std::cout << "  Processing: " << audio_files[0] << "...\n";
            success = engine.Register(speaker_name, audio_files[0]);
        } else {
            success = engine.Register(speaker_name, audio_files);
        }
    }

    if (!success) {
        std::cerr << "Failed to register speaker\n";
        return 1;
    }

    if (!engine.SaveDatabase(database_file)) {
        std::cerr << "Failed to save database\n";
        return 1;
    }

    std::cout << "Successfully registered speaker '" << speaker_name << "'\n";
    std::cout << "Database saved to: " << database_file << "\n";
    std::cout << "Total speakers in database: " << engine.GetSpeakerCount() << "\n";

    return 0;
}
