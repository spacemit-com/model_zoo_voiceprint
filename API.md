# Voiceprint API

说话人识别框架，提供 C++ 接口。

## 功能特性

- **说话人识别**: 从音频中识别未知说话人（1:N 匹配）
- **说话人验证**: 验证音频是否匹配特定说话人（1:1 比对）
- **多样本注册**: 多个音频样本取平均嵌入，提高注册准确性
- **嵌入提取**: 提取 192 维 L2 归一化说话人嵌入向量
- **数据库持久化**: 说话人数据库的保存与加载

---

## C++ API

```cpp
namespace SpacemiT {

// =============================================================================
// VpBackendType - 后端类型
// =============================================================================

enum class VpBackendType {
    CAMPPLUS,    // CamP+（3D-Speaker）
    ERES2NET,    // ERes2Net（预留）
    ECAPA_TDNN,  // ECAPA-TDNN（预留）
    HTTP,        // HTTP 远程推理（预留）
    CUSTOM,      // 自定义后端（预留）
};

// =============================================================================
// VpConfig - 配置
// =============================================================================

struct VpConfig {
    VpBackendType backend = VpBackendType::CAMPPLUS;  // 后端类型
    std::string model_dir;     // 模型目录
    int num_threads = 1;       // 推理线程数
    std::string provider = "cpu";  // 执行提供者 ("cpu", "spacemit")
    float threshold = 0.6f;    // 相似度阈值 [0.0, 1.0]
    int sample_rate = 16000;   // 采样率
    std::string db_path;       // 数据库文件路径

    static VpConfig Preset(const std::string& name);
    static std::vector<std::string> AvailablePresets();

    // 链式配置
    VpConfig withThreshold(float t) const;
    VpConfig withNumThreads(int n) const;
    VpConfig withProvider(const std::string& p) const;
    VpConfig withDbPath(const std::string& path) const;
    VpConfig withSampleRate(int rate) const;
};

// =============================================================================
// SpeakerMatch - 说话人匹配结果
// =============================================================================

struct SpeakerMatch {
    std::string name;       // 说话人名称
    float score = 0.0f;     // 余弦相似度分数 [0.0, 1.0]
};

// =============================================================================
// VpResult - 识别结果
// =============================================================================

class VpResult {
public:
    std::string GetName() const;                    // 最佳匹配说话人名称
    float GetScore() const;                         // 最佳匹配分数
    bool IsIdentified() const;                      // 是否超过阈值
    std::vector<SpeakerMatch> GetMatches() const;   // 所有匹配结果
    bool IsVerified() const;                        // 验证模式下是否匹配
    std::vector<float> GetEmbedding() const;        // 192 维嵌入向量
    float GetRTF() const;                           // 实时率
    int GetProcessingTimeMs() const;                // 处理时间 (毫秒)
    bool IsSuccess() const;                         // 是否成功
    std::string GetErrorMessage() const;            // 错误信息
};

// =============================================================================
// VpEngine - 识别引擎
// =============================================================================

class VpEngine {
public:
    explicit VpEngine(const VpConfig& config = VpConfig());
    explicit VpEngine(VpBackendType backend, const std::string& model_dir = "");

    // 说话人注册
    bool Register(const std::string& name, const std::string& audio_path);
    bool Register(const std::string& name, const std::vector<std::string>& audio_paths);
    bool Register(const std::string& name, const std::vector<float>& audio, int sample_rate = 16000);
    bool RegisterWithEmbedding(const std::string& name, const std::vector<float>& embedding);

    // 说话人识别 (1:N)
    std::shared_ptr<VpResult> Identify(const std::string& audio_path);
    std::shared_ptr<VpResult> Identify(const std::vector<float>& audio, int sample_rate = 16000);

    // 说话人验证 (1:1)
    std::shared_ptr<VpResult> Verify(const std::string& name, const std::string& audio_path);
    std::shared_ptr<VpResult> Verify(const std::string& name, const std::vector<float>& audio,
                                     int sample_rate = 16000);

    // 嵌入提取
    std::shared_ptr<VpResult> ExtractEmbedding(const std::string& audio_path);
    std::shared_ptr<VpResult> ExtractEmbedding(const std::vector<float>& audio, int sample_rate = 16000);

    // 说话人管理
    bool RemoveSpeaker(const std::string& name);
    bool ContainsSpeaker(const std::string& name) const;
    int GetSpeakerCount() const;
    std::vector<std::string> GetAllSpeakers() const;

    // 数据库持久化
    bool SaveDatabase(const std::string& path = "");
    bool LoadDatabase(const std::string& path = "");

    // 动态配置
    void SetThreshold(float threshold);
    float GetThreshold() const;

    // 状态查询
    bool IsInitialized() const;
    int GetEmbeddingDimension() const;
    std::string GetEngineName() const;
    VpBackendType GetBackendType() const;
    VpConfig GetConfig() const;
};

}  // namespace SpacemiT
```

### C++ 示例

```cpp
#include "vp_service.h"
using namespace SpacemiT;

int main() {
    // 配置
    auto config = VpConfig::Preset("campplus")
        .withThreshold(0.7f)
        .withNumThreads(4)
        .withDbPath("speakers.db");

    VpEngine engine(config);
    engine.LoadDatabase("speakers.db");

    // 注册说话人（单文件 / 多文件 / 原始 PCM）
    engine.Register("Alice", "sample.wav");
    engine.Register("Alice", {"s1.wav", "s2.wav", "s3.wav"});
    engine.Register("Alice", pcm_float_samples, 16000);

    // 识别 (1:N)
    auto result = engine.Identify("test.wav");
    if (result->IsIdentified()) {
        std::cout << result->GetName() << ": " << result->GetScore() << "\n";
        for (auto& m : result->GetMatches()) {
            std::cout << m.name << ": " << m.score << "\n";
        }
    }

    // 验证 (1:1)
    auto vr = engine.Verify("Alice", "test.wav");
    std::cout << "Verified: " << vr->IsVerified() << "\n";

    // 提取嵌入向量
    auto er = engine.ExtractEmbedding("test.wav");
    std::vector<float> embedding = er->GetEmbedding();  // 192 维，L2 归一化

    // 说话人管理
    engine.GetAllSpeakers();
    engine.RemoveSpeaker("Alice");
    engine.SaveDatabase();

    return 0;
}
```

---

## 数据格式

- **采样率**: 16000 Hz（任意采样率输入自动重采样）
- **声道**: 单声道（多声道自动提取左声道）
- **格式**:
  - C++: `std::vector<float>`（float32 归一化）
  - WAV 文件: PCM 16/32-bit 或 IEEE Float 32-bit
- **范围**: float32: [-1.0, 1.0]
- **最小时长**: 0.3 秒
- **推荐时长**: 1-3 秒
- **嵌入维度**: 192 维，L2 归一化
- **数据库格式**: 自定义二进制格式（magic: `0x53504B52`）
