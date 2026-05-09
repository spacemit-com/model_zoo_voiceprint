# Voiceprint 组件

## 1. 项目简介

本组件为说话人识别（声纹识别）封装，提供统一的 C++ 接口，基于 ONNX Runtime 推理，搭载 CamP+（3D-Speaker）模型，输出 192 维说话人嵌入向量，通过余弦相似度进行比对。采用组件化架构（策略模式 + 工厂模式 + Pimpl），支持多后端扩展。功能特性如下：

| 类别     | 支持                                                                 |
| -------- | -------------------------------------------------------------------- |
| 部署方式 | **本地**（ONNX Runtime 推理）                                       |
| 识别方式 | 说话人识别 `Identify()`（1:N）、说话人验证 `Verify()`（1:1）、嵌入提取 `ExtractEmbedding()` |
| 后端     | 当前支持 CamP+（3D-Speaker）；接口可扩展其他后端                     |
| 注册方式 | 文件注册、多样本注册、原始 PCM 注册、实时录音注册（PortAudio）        |
| 接口     | C++（`include/vp_service.h`）                                        |

## 2. 验证模型

按以下顺序完成依赖安装、模型准备与示例运行。

### 2.1. 安装依赖

- **编译环境**：CMake >= 3.14，C++17 编译器（GCC 7+、Clang 5+、MSVC 2017+）。
- **必选**：libcurl（模型自动下载）、ALSA 开发库和 git（Linux 下拉取并编译 PortAudio
  v19.7.0，实时录音使用）。

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake \
  libcurl4-openssl-dev libasound2-dev git
```

Linux 下 `register_speaker` 会通过 CMake `FetchContent` 拉取 PortAudio v19.7.0，并只启用
ALSA backend 静态链接到可执行文件；构建产物不再依赖系统 `libportaudio.so.2`，但仍依赖系统
ALSA 运行库 `libasound.so.2`。默认源码地址为 Gitee fork
`https://gitee.com/spacemit-robotics/portaudio.git`，可用 CMake 参数覆盖：

```bash
cmake -DPORTAUDIO_GIT_REPOSITORY=<repo-url> -DPORTAUDIO_GIT_TAG=<tag-or-commit> ..
```

ONNX Runtime 若本地未找到，CMake 会自动从 GitHub 下载 v1.16.3 预编译包。

### 2.2. 下载模型

模型在首次运行时自动下载到 `~/.cache/models/vp/campplus/`，无需手动操作。

若需手动下载：

```bash
mkdir -p ~/.cache/models/vp/campplus
cd ~/.cache/models/vp/campplus
wget --no-check-certificate https://archive.spacemit.com/spacemit-ai/model_zoo/vp/campplus/3dspeaker_speech_campplus_sv_zh-cn_16k-common.onnx
```

### 2.3. 测试

本节提供示例程序的编译与运行方式，便于开发者快速验证效果。使用前需先按下列两种方式之一完成编译，再运行对应示例。

- **在 SDK 中验证**（2.3.1）：在已拉取的 SpacemiT Robot SDK 工程内用 `mm` 编译，产物部署到 `output/staging`。
- **独立构建下验证**（2.3.2）：在本组件目录下用 CMake 本地编译，不依赖完整 SDK。

#### 2.3.1. 在 SDK 中验证

**编译**：本组件已纳入 SpacemiT Robot SDK 时，在 SDK 根目录下执行：

```bash
source build/envsetup.sh
cd components/model_zoo/voiceprint
mm
```

构建产物会安装到 `output/staging`。

**运行**：运行前在 SDK 根目录执行 `source build/envsetup.sh`，然后可执行：

```bash
# 注册说话人（音频文件）
register_speaker -n 张三 sample.wav

# 注册说话人（实时录音，自动录制 3 次，每次 4 秒）
register_speaker -n 张三

# 识别说话人
identify_speaker test.wav

# 验证特定说话人
identify_speaker -v 张三 test.wav
```

#### 2.3.2. 独立构建下验证

在本组件目录下完成编译后，运行下列示例。

```bash
cd /path/to/voiceprint
mkdir -p build && cd build
cmake ..
make -j$(nproc)

# RISC-V SpaceMIT 构建
# cmake -DUSE_SPACEMIT_EP=ON ..

# 注册说话人
./bin/register_speaker -n 张三 sample.wav

# 多样本注册（推荐，取平均嵌入）
./bin/register_speaker -n 张三 s1.wav s2.wav s3.wav

# 实时录音注册
./bin/register_speaker -n 张三

# 指定录音设备和采样率
./bin/register_speaker -n 张三 -i 2 -r 48000 -c 2

# 列出可用录音设备
./bin/register_speaker -l

# 识别说话人
./bin/identify_speaker test.wav

# 显示前 5 个匹配结果，阈值 0.7
./bin/identify_speaker -n 5 -s 0.7 test.wav

# 验证特定说话人
./bin/identify_speaker -v 张三 test.wav

# 列出所有注册的说话人
./bin/identify_speaker -l
```

`register_speaker` 完整参数：

| 参数 | 说明 |
|------|------|
| `-n, --name NAME` | 说话人名称（必需） |
| `-d, --database FILE` | 数据库文件路径（默认 `speakers.db`） |
| `-t, --threads NUM` | 线程数（默认 1） |
| `-f, --force` | 强制覆盖已存在的说话人 |
| `-l, --list-devices` | 列出可用录音设备 |
| `-i, --input-device N` | 指定录音设备索引 |
| `-r, --sample-rate N` | 录音采样率（默认 16000） |
| `-c, --channels N` | 录音通道数（默认 1） |

`identify_speaker` 完整参数：

| 参数 | 说明 |
|------|------|
| `-d, --database FILE` | 数据库文件路径（默认 `speakers.db`） |
| `-t, --threads NUM` | 线程数（默认 1） |
| `-s, --threshold VAL` | 相似度阈值 0-1（默认 0.6） |
| `-n, --top NUM` | 显示前 N 个匹配（默认 3） |
| `-v, --verify NAME` | 验证是否匹配特定说话人 |
| `-l, --list` | 列出所有注册的说话人 |
| `-V, --verbose` | 显示所有相似度分数 |

## 3. 应用开发

本章说明如何在自有工程中**集成 Voiceprint 并调用 API**。环境与依赖见 [2.1](#21-安装依赖)，模型准备见 [2.2](#22-下载模型)，编译与运行示例见 [2.3](#23-测试)。

### 3.1. 构建与集成产物

无论通过 [2.3.1](#231-在-sdk-中验证)（SDK）或 [2.3.2](#232-独立构建下验证)（独立构建）哪种方式编译，完成后**应用开发所需**的库与头文件如下，集成时只需**包含头文件并链接对应库**：

| 产物 | 说明 |
| ---- | ---- |
| `include/vp_service.h` | **C++ API 头文件**，应用侧只需包含此头文件并链接下方库即可调用 |
| `build/lib/libvoiceprint.a` | C++ 核心库，链接时使用 |

示例可执行文件（非集成必需）：`build/bin/register_speaker`、`build/bin/identify_speaker`。

### 3.2. API 使用

**C++**：头文件 `include/vp_service.h` 为唯一 API 入口，实现为 PIMPL，无额外依赖。在业务代码中 `#include "vp_service.h"`，链接 `libvoiceprint.a`（及 ONNX Runtime、libcurl 等），即可使用。实时录音示例 `register_speaker` 在 Linux 下会静态链接自编 PortAudio。

```cpp
#include "vp_service.h"
using namespace SpacemiT;

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
}

// 验证 (1:1)
auto vr = engine.Verify("Alice", "test.wav");
std::cout << "Verified: " << vr->IsVerified() << "\n";

// 提取嵌入向量
auto er = engine.ExtractEmbedding("test.wav");
std::vector<float> embedding = er->GetEmbedding();  // 192 维，L2 归一化
```

完整 API 文档详见 [API.md](API.md)。

**CMake 集成**：将本组件作为子目录引入，并链接 `voiceprint`、包含头文件路径即可。

```cmake
add_subdirectory(voiceprint)
target_link_libraries(your_target PRIVATE voiceprint)
target_include_directories(your_target PRIVATE ${VOICEPRINT_SOURCE_DIR}/include)
```

## 4. 常见问题

| 现象 | 可能原因 | 处理 |
| --- | --- | --- |
| `UNKNOWN: No match above threshold` | 分数低于阈值、注册样本不足或环境不同 | 增加注册样本，降低噪声，必要时调整 `--threshold`。 |
| 同一人验证偶发失败 | 语音太短、音量过低或麦克风位置变化大 | 使用 2-5 秒清晰语音，并保持注册和识别设备一致。 |
| 不同人分数偏高 | 注册样本质量差或阈值过低 | 提高阈值，重新采集注册样本。 |
| 找不到录音设备 | PortAudio 默认设备不正确 | `register_speaker -l` 列出设备，再用 `-i` 指定。 |

## 5. 版本与发布

版本以本组件文档或仓库 tag 为准。

| 版本   | 说明 |
| ------ | ---- |
| 0.1.0  | 提供 C++ 接口，支持 CamP+ 后端，文件/PCM/实时录音注册，1:N 识别与 1:1 验证。 |

## 6. 贡献方式

欢迎参与贡献：提交 Issue 反馈问题，或通过 Pull Request 提交代码。

- **编码规范**：C++ 代码遵循 [Google C++ 风格指南](https://google.github.io/styleguide/cppguide.html)。
- **提交前检查**：若仓库提供 lint 脚本，请在提交前运行并通过检查。

## 7. License

本组件源码文件头声明为 Apache-2.0，最终以本目录 `LICENSE` 文件为准。
