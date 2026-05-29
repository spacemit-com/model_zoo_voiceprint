/*
 * Copyright (C) 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <unistd.h>

#include "vp_backend.h"
#include "vp_service.h"

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "ASSERTION FAILED: " << message << std::endl;
        std::exit(1);
    }
}

bool near(float actual, float expected, float tolerance = 1e-5f) {
    return std::fabs(actual - expected) <= tolerance;
}

bool contains(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

std::string artifact_path(const std::string& filename) {
    const char* artifact_dir = std::getenv("SROBOTIS_TEST_ARTIFACT_DIR");
    if (artifact_dir && artifact_dir[0] != '\0') {
        return std::string(artifact_dir) + "/" + filename;
    }

    char temp_path[] = "/tmp/voiceprint-pr-contract-db-XXXXXX";
    int fd = mkstemp(temp_path);
    require(fd >= 0, "mkstemp must create an isolated voiceprint test database path");
    close(fd);
    std::remove(temp_path);
    return temp_path;
}

class FakeVpBackend : public SpacemiT::IVpBackend {
public:
    SpacemiT::ErrorInfo initialize(const SpacemiT::VpBackendConfig& config) override {
        config_ = config;
        initialized_ = true;
        return SpacemiT::ErrorInfo::OK();
    }

    void shutdown() override { initialized_ = false; }
    bool isInitialized() const override { return initialized_; }

    SpacemiT::ErrorInfo computeEmbedding(
            const float* samples,
            int num_samples,
            int sample_rate,
            std::vector<float>& embedding) override {
        if (!initialized_) {
            return SpacemiT::ErrorInfo::error(1, "backend not initialized");
        }
        if (samples == nullptr || num_samples <= 0) {
            return SpacemiT::ErrorInfo::error(2, "empty audio");
        }
        if (sample_rate != config_.sample_rate) {
            return SpacemiT::ErrorInfo::error(3, "unsupported sample rate");
        }

        embedding.assign(4, 0.0f);
        if (samples[0] >= 0.0f) {
            embedding[0] = 1.0f;
        } else {
            embedding[1] = 1.0f;
        }
        return SpacemiT::ErrorInfo::OK();
    }

    int getEmbeddingDimension() const override { return 4; }
    std::string getName() const override { return "fake-vp"; }
    std::vector<int> getSupportedSampleRates() const override { return {16000}; }

private:
    bool initialized_ = false;
    SpacemiT::VpBackendConfig config_;
};

void register_fake_backend() {
    SpacemiT::VpBackendFactory::registerBackend(
        SpacemiT::BackendType::CUSTOM,
        []() { return std::make_unique<FakeVpBackend>(); });
}

void verify_preset_and_builder_contract() {
    const auto presets = SpacemiT::VpConfig::AvailablePresets();
    require(contains(presets, "campplus"), "campplus preset must be advertised");

    const auto campplus = SpacemiT::VpConfig::Preset("campplus");
    require(campplus.backend == SpacemiT::VpBackendType::CAMPPLUS,
            "campplus preset must select CAMPPLUS backend");
    require(!campplus.model_dir.empty(), "campplus preset must provide a model directory");
    require(campplus.sample_rate == 16000, "campplus preset must keep 16 kHz sample rate");

    const auto tuned = campplus.withThreshold(0.7f)
        .withNumThreads(3)
        .withProvider("spacemit")
        .withDbPath("/tmp/vp.db")
        .withSampleRate(8000);
    require(near(campplus.threshold, 0.6f), "withThreshold must not mutate source config");
    require(near(tuned.threshold, 0.7f), "withThreshold must set threshold on returned config");
    require(tuned.num_threads == 3, "withNumThreads must set thread count");
    require(tuned.provider == "spacemit", "withProvider must set provider");
    require(tuned.db_path == "/tmp/vp.db", "withDbPath must set database path");
    require(tuned.sample_rate == 8000, "withSampleRate must set sample rate");
}

void verify_fake_backend_engine_contract() {
    register_fake_backend();

    SpacemiT::VpConfig config;
    config.backend = SpacemiT::VpBackendType::CUSTOM;
    config.threshold = 0.6f;
    config.sample_rate = 16000;
    SpacemiT::VpEngine engine(config);

    require(engine.IsInitialized(), "fake backend engine must initialize");
    require(engine.GetEmbeddingDimension() == 4, "fake backend must expose embedding dimension");
    require(engine.GetEngineName() == "fake-vp", "engine must expose fake backend name");

    require(engine.RegisterWithEmbedding("alice", {1.0f, 0.0f, 0.0f, 0.0f}),
            "RegisterWithEmbedding must accept correct dimension");
    require(engine.ContainsSpeaker("alice"), "registered speaker must be queryable");
    require(engine.GetSpeakerCount() == 1, "speaker count must update after registration");

    const std::vector<float> positive_audio = {0.25f, 0.1f, 0.0f};
    auto identified = engine.Identify(positive_audio, 16000);
    require(identified && identified->IsSuccess(), "Identify must succeed with fake backend");
    require(identified->IsIdentified(), "Identify must mark matching speaker identified");
    require(identified->GetName() == "alice", "Identify must return the registered speaker");
    require(near(identified->GetScore(), 1.0f), "Identify must report cosine score 1");

    auto verified = engine.Verify("alice", positive_audio, 16000);
    require(verified && verified->IsSuccess(), "Verify must succeed with fake backend");
    require(verified->IsVerified(), "Verify must accept matching speaker");

    const std::vector<float> negative_audio = {-0.25f, 0.1f, 0.0f};
    auto rejected = engine.Verify("alice", negative_audio, 16000);
    require(rejected && rejected->IsSuccess(), "Verify must run for non-matching speaker");
    require(!rejected->IsVerified(), "Verify must reject non-matching embedding");

    const std::string db_path = artifact_path("voiceprint-pr-contract.db");
    std::remove(db_path.c_str());
    require(engine.SaveDatabase(db_path), "SaveDatabase must persist registered speakers");

    SpacemiT::VpEngine restored(config);
    require(restored.IsInitialized(), "second fake backend engine must initialize");
    require(restored.LoadDatabase(db_path), "LoadDatabase must load saved speakers");
    require(restored.ContainsSpeaker("alice"), "loaded database must contain saved speaker");
    std::remove(db_path.c_str());
}

void verify_invalid_input_error_path() {
    bool threw = false;
    try {
        (void)SpacemiT::VpConfig::Preset("does-not-exist");
    } catch (const std::invalid_argument& exc) {
        threw = std::string(exc.what()).find("Unknown voiceprint preset") != std::string::npos;
    }
    require(threw, "unknown voiceprint preset must throw a useful invalid_argument");

    SpacemiT::VpConfig unavailable;
    unavailable.backend = SpacemiT::VpBackendType::ERES2NET;
    SpacemiT::VpEngine unavailable_engine(unavailable);
    require(!unavailable_engine.IsInitialized(),
            "engine with unavailable backend must remain uninitialized");
    require(!unavailable_engine.RegisterWithEmbedding("bad", {1.0f, 0.0f, 0.0f, 0.0f}),
            "uninitialized engine must reject direct embedding registration");
    auto uninitialized_result = unavailable_engine.Identify(std::vector<float>{1.0f}, 16000);
    require(uninitialized_result && !uninitialized_result->IsSuccess(),
            "uninitialized engine identify must return failed result");
    require(uninitialized_result->GetErrorMessage().find("not initialized") != std::string::npos,
            "uninitialized identify must explain the failure");

    register_fake_backend();
    SpacemiT::VpConfig config;
    config.backend = SpacemiT::VpBackendType::CUSTOM;
    SpacemiT::VpEngine engine(config);
    require(engine.IsInitialized(), "fake backend engine must initialize for error-path checks");
    require(!engine.RegisterWithEmbedding("bad-dimension", {1.0f, 0.0f}),
            "wrong embedding dimension must be rejected");

    auto bad_audio = engine.ExtractEmbedding(std::vector<float>{}, 16000);
    require(bad_audio && !bad_audio->IsSuccess(),
            "empty audio must produce a failed extraction result");
    require(bad_audio->GetErrorMessage().find("empty audio") != std::string::npos,
            "empty audio failure must include backend error message");
}

}  // namespace

int main(int argc, char** argv) {
    require(argc == 2, "expected one test mode argument");
    const std::string mode = argv[1];

    if (mode == "--fake-backend-contract") {
        verify_preset_and_builder_contract();
        verify_fake_backend_engine_contract();
    } else if (mode == "--invalid-input-error-path") {
        verify_invalid_input_error_path();
    } else {
        std::cerr << "Unknown mode: " << mode << std::endl;
        return 2;
    }

    std::cout << "PASS " << mode << std::endl;
    return 0;
}
