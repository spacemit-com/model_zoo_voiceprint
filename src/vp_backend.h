/*
 * Copyright (C) 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * 说话人识别后端抽象接口
 */

#ifndef VP_BACKEND_H
#define VP_BACKEND_H

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace SpacemiT {

// =============================================================================
// ErrorInfo
// =============================================================================

struct ErrorInfo {
    int code = 0;
    std::string message;

    bool ok() const { return code == 0; }
    static ErrorInfo OK() { return {0, ""}; }
    static ErrorInfo error(int code, const std::string& msg) { return {code, msg}; }
};

// =============================================================================
// VpBackendConfig
// =============================================================================

struct VpBackendConfig {
    std::string model_dir;
    int num_threads = 1;
    std::string provider = "cpu";
    int sample_rate = 16000;
};

// =============================================================================
// ModelInfo — describes model download info for a backend
// =============================================================================

struct ModelInfo {
    std::string url;
    std::string filename;
    std::string cache_subdir;  // relative to ~/.cache/models/, e.g. "vp/campplus"

    bool empty() const { return url.empty(); }
};

// =============================================================================
// IVpBackend
// =============================================================================

class IVpBackend {
public:
    virtual ~IVpBackend() = default;

    virtual ErrorInfo initialize(const VpBackendConfig& config) = 0;
    virtual void shutdown() = 0;
    virtual bool isInitialized() const = 0;

    virtual ErrorInfo computeEmbedding(const float* samples,
        int num_samples,
        int sample_rate,
        std::vector<float>& embedding) = 0;

    virtual int getEmbeddingDimension() const = 0;
    virtual std::string getName() const = 0;
    virtual std::vector<int> getSupportedSampleRates() const = 0;
    virtual float getMinAudioDuration() const { return 0.3f; }

    virtual ModelInfo getModelInfo() const { return {}; }
};

// =============================================================================
// BackendType
// =============================================================================

enum class BackendType {
    CAMPPLUS,
    ERES2NET,
    ECAPA_TDNN,
    HTTP,
    CUSTOM,
};

// =============================================================================
// VpBackendFactory — registry-based factory
// =============================================================================

class VpBackendFactory {
public:
    using FactoryFn = std::function<std::unique_ptr<IVpBackend>()>;

    static std::unique_ptr<IVpBackend> create(BackendType type);
    static bool isAvailable(BackendType type);
    static std::vector<BackendType> getAvailableBackends();
    static void registerBackend(BackendType type, FactoryFn fn);
};

// =============================================================================
// REGISTER_VP_BACKEND — auto-registration macro
// =============================================================================

namespace detail {

struct VpBackendRegistrar {
    VpBackendRegistrar(BackendType type, VpBackendFactory::FactoryFn fn) {
        VpBackendFactory::registerBackend(type, std::move(fn));
    }
};

}  // namespace detail

#define REGISTER_VP_BACKEND(name, type, factory_fn) \
    static ::SpacemiT::detail::VpBackendRegistrar _vp_reg_##name(type, factory_fn)

}  // namespace SpacemiT

#endif  // VP_BACKEND_H
