/*
 * Copyright (C) 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * CamP+ 说话人识别后端
 */

#include <cmath>
#include <cstdlib>
#include <filesystem>  // NOLINT(build/c++17)
#include <memory>
#include <string>
#include <vector>
#include <iostream>
#include "../../common/mel_filterbank.h"
#include "../../vp_backend.h"
#include "onnx_model.h"

namespace SpacemiT {
namespace campplus {

static const char* kDefaultModelName = "3dspeaker_speech_campplus_sv_zh-cn_16k-common.onnx";

static std::string expandPath(const std::string& path) {
    if (path.empty() || path[0] != '~') return path;
    const char* home = std::getenv("HOME");
    if (!home) home = std::getenv("USERPROFILE");
    if (!home) return path;
    return std::string(home) + path.substr(1);
}

static std::string resolveModelPath(const std::string& model_dir) {
    if (!model_dir.empty()) {
        std::string dir = expandPath(model_dir);
        std::string path = dir + "/" + kDefaultModelName;
        if (std::filesystem::exists(path)) return path;
        // Maybe model_dir is the full model path
        if (std::filesystem::exists(dir)) return dir;
        return path;
    }
    // Default cache path
    std::string cache = expandPath("~/.cache/models/vp/campplus");
    return cache + "/" + kDefaultModelName;
}

class CamPPlusBackend : public IVpBackend {
public:
    CamPPlusBackend() = default;
    ~CamPPlusBackend() override { shutdown(); }

    ErrorInfo initialize(const VpBackendConfig& config) override {
        if (initialized_) return ErrorInfo::OK();

        std::string model_path = resolveModelPath(config.model_dir);

        if (!std::filesystem::exists(model_path)) {
            return ErrorInfo::error(1, "Model file not found: " + model_path);
        }

        try {
            model_ = std::make_unique<OnnxModel>(model_path, config.num_threads, config.provider);
            embedding_dim_ = model_->GetEmbeddingDim();
            initialized_ = true;
            return ErrorInfo::OK();
        } catch (const std::exception& e) {
            return ErrorInfo::error(2, std::string("Failed to load model: ") + e.what());
        }
    }

    void shutdown() override {
        model_.reset();
        initialized_ = false;
    }

    bool isInitialized() const override { return initialized_; }

    ErrorInfo computeEmbedding(const float* samples,
        int num_samples,
        int sample_rate,
        std::vector<float>& embedding) override {
        if (!initialized_) {
            return ErrorInfo::error(3, "Backend not initialized");
        }

        float min_samples = getMinAudioDuration() * sample_rate;
        if (num_samples < static_cast<int>(min_samples)) {
            return ErrorInfo::error(4, "Audio too short");
        }

        // Compute mel spectrogram
        std::vector<float> audio(samples, samples + num_samples);
        std::vector<int64_t> shape;
        auto fbank = MelFilterbank::ComputeFbank(audio, sample_rate, shape);

        // Run inference
        embedding = model_->RunInference(fbank, shape);
        if (embedding.empty()) {
            return ErrorInfo::error(5, "Inference failed");
        }

        // L2 normalize
        float norm = 0.0f;
        for (float val : embedding) norm += val * val;
        norm = std::sqrt(norm);
        if (norm > 0) {
            for (float& val : embedding) val /= norm;
        }

        return ErrorInfo::OK();
    }

    int getEmbeddingDimension() const override { return embedding_dim_; }

    std::string getName() const override { return "CamP+ (3D-Speaker)"; }

    std::vector<int> getSupportedSampleRates() const override {
        return {16000};
    }

    float getMinAudioDuration() const override { return 0.3f; }

    ModelInfo getModelInfo() const override {
        return {
            "https://archive.spacemit.com/spacemit-ai/model_zoo/vp/campplus/"
            "3dspeaker_speech_campplus_sv_zh-cn_16k-common.onnx",
            "3dspeaker_speech_campplus_sv_zh-cn_16k-common.onnx",
            "vp/campplus"
        };
    }

private:
    bool initialized_ = false;
    int embedding_dim_ = 192;
    std::unique_ptr<OnnxModel> model_;
};

}  // namespace campplus

REGISTER_VP_BACKEND(campplus, BackendType::CAMPPLUS, []() -> std::unique_ptr<IVpBackend> {
    return std::make_unique<campplus::CamPPlusBackend>();
});

}  // namespace SpacemiT
