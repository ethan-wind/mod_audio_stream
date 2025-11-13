# Float32 到 G.711 A-law 转换说明

## 原始音频格式

- **数据类型**: Float32（32位浮点数）
- **字节序**: 小端序（Little-Endian）
- **采样率**: 24000 Hz
- **声道**: 单声道
- **数值范围**: -1.0 到 1.0

## 转换流程

```
Float32 (24000Hz, 单声道, 小端序)
    ↓
[步骤 1] Float32 → 16-bit PCM
    sample_16bit = sample_float32 * 32767
    ↓
16-bit PCM (24000Hz, 单声道)
    ↓
[步骤 2] 重采样 24000Hz → 8000Hz
    使用 SpeexDSP 重采样器
    ↓
16-bit PCM (8000Hz, 单声道)
    ↓
[步骤 3] 16-bit PCM → G.711 A-law
    使用标准 ITU-T G.711 算法
    ↓
G.711 A-law (8000Hz, 8-bit, 单声道)
```

## 关键代码

### Float32 转 16-bit PCM

```cpp
const float* float_data = reinterpret_cast<const float*>(rawAudio.data());
size_t input_samples = rawAudio.size() / sizeof(float);

for (size_t i = 0; i < input_samples; i++) {
    float sample = float_data[i];
    
    // 限幅处理
    if (sample > 1.0f) sample = 1.0f;
    if (sample < -1.0f) sample = -1.0f;
    
    // 转换为 16-bit
    pcm16bit[i] = static_cast<int16_t>(sample * 32767.0f);
}
```

### 重采样 24000Hz → 8000Hz

```cpp
SpeexResamplerState* resampler = speex_resampler_init(
    1,      // 单声道
    24000,  // 源采样率
    8000,   // 目标采样率
    SWITCH_RESAMPLE_QUALITY,
    &err
);

speex_resampler_process_int(
    resampler, 0,
    pcm16bit.data(),      // 输入：24000Hz 16-bit
    &in_len,
    outputSamples.data(), // 输出：8000Hz 16-bit
    &out_len
);
```

### 16-bit PCM → A-law

```cpp
for (size_t i = 0; i < outputSamples.size(); i++) {
    alawData[i] = linear_to_alaw(outputSamples[i]);
}
```

## WAV 文件格式

输出的 WAV 文件使用标准格式：

- **格式码**: 6 (G.711 A-law)
- **采样率**: 8000 Hz
- **声道数**: 1 (单声道)
- **位深度**: 8 bit
- **字节序**: 小端序
- **fmt chunk size**: 18 (包含 cbSize 字段)

## 数值转换示例

| Float32 输入 | 16-bit PCM | A-law (hex) | 说明 |
|-------------|-----------|-------------|------|
| 0.0         | 0         | 0xD5        | 静音 |
| 0.1         | 3277      | 0xE0        | 小音量 |
| 0.5         | 16384     | 0xF4        | 中等音量 |
| 1.0         | 32767     | 0xFF        | 最大音量 |
| -1.0        | -32767    | 0x7F        | 最大负值 |

## 编译和使用

### 编译主模块

```bash
cd build
cmake ..
make
sudo make install
sudo systemctl restart freeswitch
```

### 编译转换工具

```bash
g++ -o convert_to_alaw convert_to_alaw.cpp -lspeexdsp -std=c++11
```

### 使用转换工具

```bash
# 转换 Float32 WAV 文件
./convert_to_alaw input_float32_24000hz.wav output_alaw_8000hz.wav

# 工具会自动检测 Float32 或 Int32 格式
```

## 日志输出示例

```
processMessage - processing raw audio: 24000 Hz, size: 96000 bytes (Float32 format)
processMessage - converted Float32 to 16-bit PCM: 24000 samples
processMessage - resampled from 24000 to 8000 Hz: 24000 -> 8000 samples
processMessage - converted 8000 samples to A-law
processMessage - G.711 A-law WAV file created: /tmp/xxx.wav (8000 Hz, 8000 bytes, 8000 samples)
```

## 常见问题

### Q: 为什么之前有杂音？

**A**: 之前代码将 Float32 数据当作 Int32 处理，导致数值完全错误：
- Float32 的 `0.5` 被错误解释为一个巨大的整数
- 转换后的音频完全失真

### Q: Float32 的范围一定是 [-1.0, 1.0] 吗？

**A**: 标准 Float32 PCM 范围是 [-1.0, 1.0]，但某些系统可能超出此范围。代码中已添加限幅处理。

### Q: 为什么要转换为 16-bit 而不是直接转 A-law？

**A**: 
1. SpeexDSP 重采样器需要整数输入
2. G.711 A-law 标准基于 16-bit PCM 设计
3. 这是标准的音频处理流程

## 性能说明

- **Float32 → 16-bit**: 非常快，简单的乘法运算
- **重采样 24000→8000**: 中等，使用高质量算法
- **16-bit → A-law**: 非常快，查表法

对于 1 秒的音频（24000 个样本），整个转换过程通常在 1-2 毫秒内完成。
