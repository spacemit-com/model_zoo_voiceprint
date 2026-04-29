/*
 * Copyright (C) 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * 说话人识别引擎实现
 */

#include <curl/curl.h>
#include <cmath>
#include <cstdlib>
#include <filesystem>  // NOLINT(build/c++17)
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <unordered_map>
#include "../include/vp_service.h"
#include "vp_backend.h"

namespace SpacemiT {

// =============================================================================
// Helper: BackendType mapping
// =============================================================================

static BackendType toInternalType(VpBackendType t) {
    switch (t) {
        case VpBackendType::CAMPPLUS:   return BackendType::CAMPPLUS;
        case VpBackendType::ERES2NET:   return BackendType::ERES2NET;
        case VpBackendType::ECAPA_TDNN: return BackendType::ECAPA_TDNN;
        case VpBackendType::HTTP:       return BackendType::HTTP;
        case VpBackendType::CUSTOM:     return BackendType::CUSTOM;
    }
    return BackendType::CAMPPLUS;
}

// =============================================================================
// Model Downloader (generic, driven by ModelInfo from backend)
// =============================================================================

namespace {

static std::string expandPath(const std::string& path) {
    if (path.empty() || path[0] != '~') return path;
    const char* home = std::getenv("HOME");
    if (!home) home = std::getenv("USERPROFILE");
    if (!home) return path;
    return std::string(home) + path.substr(1);
}

static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    auto* file = static_cast<std::ofstream*>(userp);
    size_t total = size * nmemb;
    file->write(static_cast<const char*>(contents), total);
    return total;
}

static bool ensureModelExists(const ModelInfo& info, const std::string& config_model_dir) {
    if (info.empty()) return true;  // No model to download (e.g. HTTP backend)

    std::string dir = expandPath(
        config_model_dir.empty()
            ? "~/.cache/models/" + info.cache_subdir
            : config_model_dir);
    std::string model_path = dir + "/" + info.filename;

    if (std::filesystem::exists(model_path)) return true;

    std::filesystem::create_directories(dir);

    std::string tmp_path = model_path + ".tmp";

    std::cout << "Model not found, downloading..." << std::endl;

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Failed to initialize curl" << std::endl; return false;
    }

    std::ofstream file(tmp_path, std::ios::binary);
    if (!file.is_open()) {
        curl_easy_cleanup(curl); return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, info.url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &file);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "voiceprint/1.0");

    CURLcode res = curl_easy_perform(curl);
    long response_code = 0;  // NOLINT(runtime/int): libcurl requires long*.
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    curl_easy_cleanup(curl);
    file.close();

    if (res != CURLE_OK || response_code != 200) {
        std::filesystem::remove(tmp_path);
        std::cerr << "Download failed" << std::endl;
        return false;
    }

    std::filesystem::rename(tmp_path, model_path);
    std::cout << "Model downloaded successfully" << std::endl;
    return true;
}

}  // namespace

}  // namespace SpacemiT

#include "wave_reader_internal.h"

namespace SpacemiT {

// =============================================================================
// SpeakerDatabase (internal, replaces SpeakerManager)
// =============================================================================

namespace {

class SpeakerDatabase {
public:
    explicit SpeakerDatabase(int dim) : dim_(dim) {}

    bool registerSpeaker(const std::string& name, const std::vector<float>& embedding) {
        if (static_cast<int>(embedding.size()) != dim_) return false;
        speakers_[name] = embedding;
        return true;
    }

    bool registerSpeaker(const std::string& name, const std::vector<std::vector<float>>& embeddings) {
        if (embeddings.empty()) return false;

        std::vector<float> avg(dim_, 0.0f);
        int count = 0;
        for (const auto& e : embeddings) {
            if (static_cast<int>(e.size()) == dim_) {
                for (int i = 0; i < dim_; i++) avg[i] += e[i];
                count++;
            }
        }
        if (count == 0) return false;

        for (float& v : avg) v /= count;
        // L2 normalize
        float norm = 0.0f;
        for (float v : avg) norm += v * v;
        norm = std::sqrt(norm);
        if (norm > 0) for (float& v : avg) v /= norm;

        speakers_[name] = avg;
        return true;
    }

    bool removeSpeaker(const std::string& name) { return speakers_.erase(name) > 0; }
    bool containsSpeaker(const std::string& name) const { return speakers_.count(name) > 0; }
    int getSpeakerCount() const { return static_cast<int>(speakers_.size()); }

    std::vector<std::string> getAllSpeakers() const {
        std::vector<std::string> names;
        names.reserve(speakers_.size());
        for (const auto& [n, _] : speakers_) names.push_back(n);
        return names;
    }

    struct MatchResult {
        std::string name;
        float score = 0.0f;
    };

    std::vector<MatchResult> search(const std::vector<float>& embedding, float threshold, int max_results) const {
        if (static_cast<int>(embedding.size()) != dim_) return {};

        std::vector<MatchResult> matches;
        for (const auto& [name, spk_emb] : speakers_) {
            float score = cosineSimilarity(embedding, spk_emb);
            if (score >= threshold) {
                matches.push_back({name, score});
            }
        }

        if (static_cast<int>(matches.size()) > max_results) {
            std::partial_sort(matches.begin(), matches.begin() + max_results, matches.end(),
                [](const MatchResult& a, const MatchResult& b) { return a.score > b.score; });
            matches.resize(max_results);
        } else {
            std::sort(matches.begin(), matches.end(),
                [](const MatchResult& a, const MatchResult& b) { return a.score > b.score; });
        }
        return matches;
    }

    float verifySpeaker(const std::string& name, const std::vector<float>& embedding) const {
        if (static_cast<int>(embedding.size()) != dim_) return 0.0f;
        auto it = speakers_.find(name);
        if (it == speakers_.end()) return 0.0f;
        return cosineSimilarity(embedding, it->second);
    }

    bool save(const std::string& filename) const {
        std::ofstream file(filename, std::ios::binary);
        if (!file) return false;

        uint32_t magic = 0x53504B52;
        uint32_t version = 1;
        int32_t dim = dim_;
        int32_t num = static_cast<int32_t>(speakers_.size());

        file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
        file.write(reinterpret_cast<const char*>(&version), sizeof(version));
        file.write(reinterpret_cast<const char*>(&dim), sizeof(dim));
        file.write(reinterpret_cast<const char*>(&num), sizeof(num));

        for (const auto& [name, emb] : speakers_) {
            int32_t name_len = static_cast<int32_t>(name.length());
            file.write(reinterpret_cast<const char*>(&name_len), sizeof(name_len));
            file.write(name.c_str(), name_len);
            file.write(reinterpret_cast<const char*>(emb.data()), emb.size() * sizeof(float));
        }
        return file.good();
    }

    bool load(const std::string& filename) {
        std::ifstream file(filename, std::ios::binary);
        if (!file) return false;

        uint32_t magic, version;
        int32_t file_dim, num;

        file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
        if (magic != 0x53504B52) return false;

        file.read(reinterpret_cast<char*>(&version), sizeof(version));
        if (version != 1) return false;

        file.read(reinterpret_cast<char*>(&file_dim), sizeof(file_dim));
        if (file_dim != dim_) {
            std::cerr << "Embedding dimension mismatch. Expected: " << dim_
                << ", Got: " << file_dim << std::endl;
            return false;
        }

        file.read(reinterpret_cast<char*>(&num), sizeof(num));

        std::unordered_map<std::string, std::vector<float>> temp;
        for (int32_t i = 0; i < num; i++) {
            int32_t name_len;
            file.read(reinterpret_cast<char*>(&name_len), sizeof(name_len));
            std::string name(name_len, '\0');
            file.read(&name[0], name_len);
            std::vector<float> emb(dim_);
            file.read(reinterpret_cast<char*>(emb.data()), dim_ * sizeof(float));
            if (!file) return false;
            temp[name] = std::move(emb);
        }

        speakers_ = std::move(temp);
        std::cout << "Loaded " << speakers_.size() << " speakers from database" << std::endl;
        return true;
    }

private:
    float cosineSimilarity(const std::vector<float>& a, const std::vector<float>& b) const {
        float dot = 0.0f, na = 0.0f, nb = 0.0f;
        for (int i = 0; i < dim_; i++) {
            dot += a[i] * b[i];
            na += a[i] * a[i];
            nb += b[i] * b[i];
        }
        na = std::sqrt(na);
        nb = std::sqrt(nb);
        return (na > 0 && nb > 0) ? dot / (na * nb) : 0.0f;
    }

    int dim_;
    std::unordered_map<std::string, std::vector<float>> speakers_;
};

}  // namespace

// =============================================================================
// VpResult::Impl
// =============================================================================

struct VpResult::Impl {
    std::vector<SpeakerMatch> matches;
    std::vector<float> embedding;
    float rtf = 0.0f;
    int processing_time_ms = 0;
    bool success = false;
    bool verified = false;
    float threshold = 0.6f;
    std::string error_message;
};

VpResult::VpResult() : impl_(std::make_unique<Impl>()) {}
VpResult::~VpResult() = default;
VpResult::VpResult(VpResult&&) noexcept = default;
VpResult& VpResult::operator=(VpResult&&) noexcept = default;

std::string VpResult::GetName() const {
    if (impl_->matches.empty()) return "";
    return impl_->matches[0].name;
}

float VpResult::GetScore() const {
    if (impl_->matches.empty()) return 0.0f;
    return impl_->matches[0].score;
}

bool VpResult::IsIdentified() const {
    return !impl_->matches.empty() && impl_->matches[0].score >= impl_->threshold;
}

std::vector<SpeakerMatch> VpResult::GetMatches() const { return impl_->matches; }
bool VpResult::IsVerified() const { return impl_->verified; }
std::vector<float> VpResult::GetEmbedding() const { return impl_->embedding; }
float VpResult::GetRTF() const { return impl_->rtf; }
int VpResult::GetProcessingTimeMs() const { return impl_->processing_time_ms; }
bool VpResult::IsSuccess() const { return impl_->success; }
std::string VpResult::GetErrorMessage() const { return impl_->error_message; }

// =============================================================================
// VpEngine::Impl
// =============================================================================

struct VpEngine::Impl {
    VpConfig config;
    std::unique_ptr<IVpBackend> backend;
    std::unique_ptr<SpeakerDatabase> db;
    bool initialized = false;

    // Read WAV file and return PCM samples
    struct WavResult {
        std::vector<float> samples;
        int sample_rate = 0;
        bool ok = false;
    };

    WavResult readWav(const std::string& path);

    // Compute embedding from PCM
    std::shared_ptr<VpResult> computeEmbedding(const float* samples, int num_samples, int sample_rate);
};

VpEngine::Impl::WavResult VpEngine::Impl::readWav(const std::string& path) {
    WavResult result;
    auto wave = WaveReader::ReadFile(path);
    if (!wave) return result;
    result.samples = std::move(wave->samples);
    result.sample_rate = wave->sample_rate;
    result.ok = true;
    return result;
}

std::shared_ptr<VpResult> VpEngine::Impl::computeEmbedding(const float* samples, int num_samples, int sample_rate) {
    auto result = std::make_shared<VpResult>();

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<float> embedding;
    auto err = backend->computeEmbedding(samples, num_samples, sample_rate, embedding);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    if (!err.ok()) {
        result->impl_->success = false;
        result->impl_->error_message = err.message;
        return result;
    }

    float audio_duration = static_cast<float>(num_samples) / sample_rate;
    float processing_seconds = duration_ms / 1000.0f;

    result->impl_->embedding = std::move(embedding);
    result->impl_->rtf = (audio_duration > 0) ? processing_seconds / audio_duration : 0.0f;
    result->impl_->processing_time_ms = static_cast<int>(duration_ms);
    result->impl_->success = true;
    result->impl_->threshold = config.threshold;

    return result;
}

// =============================================================================
// VpEngine
// =============================================================================

VpEngine::VpEngine(const VpConfig& config) : impl_(std::make_unique<Impl>()) {
    init(config);
}

VpEngine::VpEngine(VpBackendType backend, const std::string& model_dir)
    : impl_(std::make_unique<Impl>()) {
    VpConfig config;
    config.backend = backend;
    config.model_dir = model_dir;
    init(config);
}

void VpEngine::init(const VpConfig& config) {
    impl_->config = config;

    // Create backend first (lightweight, no model loaded yet)
    impl_->backend = VpBackendFactory::create(toInternalType(config.backend));
    if (!impl_->backend) {
        std::cerr << "Failed to create backend" << std::endl;
        return;
    }

    // Get model info from backend and ensure model exists
    auto model_info = impl_->backend->getModelInfo();
    if (!model_info.empty()) {
        ensureModelExists(model_info, config.model_dir);
    }

    // Initialize backend
    VpBackendConfig bc;
    bc.model_dir = config.model_dir;
    if (bc.model_dir.empty() && !model_info.empty()) {
        bc.model_dir = "~/.cache/models/" + model_info.cache_subdir;
    }
    bc.num_threads = config.num_threads;
    bc.provider = config.provider;
    bc.sample_rate = config.sample_rate;

    auto err = impl_->backend->initialize(bc);
    if (!err.ok()) {
        std::cerr << "Backend initialization failed: " << err.message << std::endl;
        return;
    }

    // Create speaker database
    impl_->db = std::make_unique<SpeakerDatabase>(impl_->backend->getEmbeddingDimension());
    impl_->initialized = true;

    // Auto-load database if configured
    if (!config.db_path.empty()) {
        impl_->db->load(config.db_path);
    }
}

VpEngine::~VpEngine() = default;

// --- Registration ---

bool VpEngine::Register(const std::string& name, const std::string& audio_path) {
    if (!impl_->initialized) return false;

    auto wav = impl_->readWav(audio_path);
    if (!wav.ok) return false;

    return Register(name, wav.samples, wav.sample_rate);
}

bool VpEngine::Register(const std::string& name, const std::vector<std::string>& audio_paths) {
    if (!impl_->initialized) return false;

    std::vector<std::vector<float>> embeddings;
    for (const auto& path : audio_paths) {
        auto wav = impl_->readWav(path);
        if (!wav.ok) continue;

        std::vector<float> emb;
        auto err = impl_->backend->computeEmbedding(wav.samples.data(),
            static_cast<int>(wav.samples.size()), wav.sample_rate, emb);
        if (err.ok() && !emb.empty()) {
            embeddings.push_back(std::move(emb));
        }
    }

    if (embeddings.empty()) return false;
    return impl_->db->registerSpeaker(name, embeddings);
}

bool VpEngine::Register(const std::string& name, const std::vector<float>& audio, int sample_rate) {
    if (!impl_->initialized) return false;

    std::vector<float> emb;
    auto err = impl_->backend->computeEmbedding(audio.data(), static_cast<int>(audio.size()),
        sample_rate, emb);
    if (!err.ok() || emb.empty()) return false;
    return impl_->db->registerSpeaker(name, emb);
}

bool VpEngine::RegisterWithEmbedding(const std::string& name, const std::vector<float>& embedding) {
    if (!impl_->initialized) return false;
    return impl_->db->registerSpeaker(name, embedding);
}

// --- Identification ---

std::shared_ptr<VpResult> VpEngine::Identify(const std::string& audio_path) {
    if (!impl_->initialized) {
        auto r = std::make_shared<VpResult>();
        r->impl_->error_message = "Engine not initialized";
        return r;
    }

    auto wav = impl_->readWav(audio_path);
    if (!wav.ok) {
        auto r = std::make_shared<VpResult>();
        r->impl_->error_message = "Failed to read audio file: " + audio_path;
        return r;
    }

    return Identify(wav.samples, wav.sample_rate);
}

std::shared_ptr<VpResult> VpEngine::Identify(const std::vector<float>& audio, int sample_rate) {
    if (!impl_->initialized) {
        auto r = std::make_shared<VpResult>();
        r->impl_->error_message = "Engine not initialized";
        return r;
    }

    auto result = impl_->computeEmbedding(audio.data(), static_cast<int>(audio.size()), sample_rate);
    if (!result->IsSuccess()) return result;

    auto matches = impl_->db->search(result->impl_->embedding, 0.0f, 100);

    result->impl_->matches.clear();
    for (const auto& m : matches) {
        result->impl_->matches.push_back({m.name, m.score});
    }

    return result;
}

// --- Verification ---

std::shared_ptr<VpResult> VpEngine::Verify(const std::string& name, const std::string& audio_path) {
    if (!impl_->initialized) {
        auto r = std::make_shared<VpResult>();
        r->impl_->error_message = "Engine not initialized";
        return r;
    }

    auto wav = impl_->readWav(audio_path);
    if (!wav.ok) {
        auto r = std::make_shared<VpResult>();
        r->impl_->error_message = "Failed to read audio file";
        return r;
    }

    return Verify(name, wav.samples, wav.sample_rate);
}

std::shared_ptr<VpResult> VpEngine::Verify(const std::string& name,
                                            const std::vector<float>& audio,
                                            int sample_rate) {
    if (!impl_->initialized) {
        auto r = std::make_shared<VpResult>();
        r->impl_->error_message = "Engine not initialized";
        return r;
    }

    auto result = impl_->computeEmbedding(audio.data(), static_cast<int>(audio.size()), sample_rate);
    if (!result->IsSuccess()) return result;

    float score = impl_->db->verifySpeaker(name, result->impl_->embedding);
    result->impl_->verified = (score >= impl_->config.threshold);
    result->impl_->matches.push_back({name, score});

    return result;
}

// --- Embedding extraction ---

std::shared_ptr<VpResult> VpEngine::ExtractEmbedding(const std::string& audio_path) {
    if (!impl_->initialized) {
        auto r = std::make_shared<VpResult>();
        r->impl_->error_message = "Engine not initialized";
        return r;
    }

    auto wav = impl_->readWav(audio_path);
    if (!wav.ok) {
        auto r = std::make_shared<VpResult>();
        r->impl_->error_message = "Failed to read audio file";
        return r;
    }

    return ExtractEmbedding(wav.samples, wav.sample_rate);
}

std::shared_ptr<VpResult> VpEngine::ExtractEmbedding(const std::vector<float>& audio, int sample_rate) {
    if (!impl_->initialized) {
        auto r = std::make_shared<VpResult>();
        r->impl_->error_message = "Engine not initialized";
        return r;
    }

    return impl_->computeEmbedding(audio.data(), static_cast<int>(audio.size()), sample_rate);
}

// --- Speaker management ---

bool VpEngine::RemoveSpeaker(const std::string& name) {
    if (!impl_->initialized) return false;
    return impl_->db->removeSpeaker(name);
}

bool VpEngine::ContainsSpeaker(const std::string& name) const {
    if (!impl_->initialized) return false;
    return impl_->db->containsSpeaker(name);
}

int VpEngine::GetSpeakerCount() const {
    if (!impl_->initialized) return 0;
    return impl_->db->getSpeakerCount();
}

std::vector<std::string> VpEngine::GetAllSpeakers() const {
    if (!impl_->initialized) return {};
    return impl_->db->getAllSpeakers();
}

// --- Database persistence ---

bool VpEngine::SaveDatabase(const std::string& path) {
    if (!impl_->initialized) return false;
    std::string p = path.empty() ? impl_->config.db_path : path;
    if (p.empty()) return false;
    return impl_->db->save(p);
}

bool VpEngine::LoadDatabase(const std::string& path) {
    if (!impl_->initialized) return false;
    std::string p = path.empty() ? impl_->config.db_path : path;
    if (p.empty()) return false;
    return impl_->db->load(p);
}

// --- Dynamic configuration ---

void VpEngine::SetThreshold(float threshold) { impl_->config.threshold = threshold; }
float VpEngine::GetThreshold() const { return impl_->config.threshold; }

// --- Status ---

bool VpEngine::IsInitialized() const { return impl_->initialized; }

int VpEngine::GetEmbeddingDimension() const {
    if (!impl_->initialized) return 0;
    return impl_->backend->getEmbeddingDimension();
}

std::string VpEngine::GetEngineName() const {
    if (!impl_->initialized) return "";
    return impl_->backend->getName();
}

VpBackendType VpEngine::GetBackendType() const { return impl_->config.backend; }
VpConfig VpEngine::GetConfig() const { return impl_->config; }

}  // namespace SpacemiT
