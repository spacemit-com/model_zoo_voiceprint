/*
 * Copyright (C) 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * ONNX 模型接口
 */

#ifndef ONNX_MODEL_H
#define ONNX_MODEL_H

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

// Platform-specific FP16 compatibility
#if defined(__riscv) || defined(__riscv__)
#ifndef __fp16
#ifdef __riscv_f
#ifdef __FLT16_MAX__
    typedef _Float16 __fp16;
#else
    typedef struct { uint16_t __v; } __fp16;
#endif
#else
    typedef struct { uint16_t __v; } __fp16;
#endif
#endif
#elif defined(__arm__) || defined(__aarch64__)
#ifndef __fp16
#if !defined(__ARM_FP16_FORMAT_IEEE) && !defined(__ARM_FP16_FORMAT_ALTERNATIVE)
    typedef struct { uint16_t __v; } __fp16;
#endif
#endif
#else
#ifndef __fp16
#ifdef _Float16
    typedef _Float16 __fp16;
#else
    typedef struct { uint16_t __v; } __fp16;
#endif
#endif
#endif

#ifdef __riscv
  #define ONNXRUNTIME_FLOAT16_WORKAROUND
#endif

#include <onnxruntime_cxx_api.h>

namespace SpacemiT {
namespace campplus {

class OnnxModel {
public:
    OnnxModel(const std::string& model_path, int32_t num_threads, const std::string& provider);
    ~OnnxModel() = default;

    std::vector<float> RunInference(const std::vector<float>& input,
                                    const std::vector<int64_t>& input_shape) const;
    int32_t GetEmbeddingDim() const { return embedding_dim_; }

private:
    std::unique_ptr<Ort::Env> env_;
    std::unique_ptr<Ort::Session> session_;
    std::unique_ptr<Ort::MemoryInfo> memory_info_;
    std::vector<Ort::AllocatedStringPtr> input_names_allocated_;
    std::vector<Ort::AllocatedStringPtr> output_names_allocated_;
    std::vector<const char*> input_names_ptr_;
    std::vector<const char*> output_names_ptr_;
    int32_t embedding_dim_;
};

}  // namespace campplus
}  // namespace SpacemiT

#endif  // ONNX_MODEL_H
