# FreeSWitch 音频格式转换流程

## 概述

本文档说明 mod_audio_stream 模块如何将从 WebSocket 接收的音频流转换为 FreeSWitch 可以正常播放的格式。

## 完整转换流程

### 1. WebSocket 接收格式

从 WebSocket 接收的原始音频数据：

```
格式: 32-bit Float (IEEE 754 浮点数)
采样率: 24000 Hz
声道: 单声道 (Mono)
位深: 32-bit (4 字节/样本)
字节序: 小端序 (Little Endian)
数值范围: [-1.0, 1.0]
传输编码: Base64
```

**示例数据大小计算**：
- 1 秒音频 = 24000 samples × 4 bytes = 96,000 bytes
- 100ms 音频 = 2400 samples × 4 bytes = 9,600 bytes

### 2. 转换步骤

#### 步骤 1: Base64 解码
```cpp
rawAudio = base64_decode(jsonAudio->valuestring);
```

#### 步骤 2: 32-bit Float → 16-bit Linear PCM
```cpp
// 输入: 32-bit Float [-1.0, 1.0] @ 24000 Hz
// 输出: 16-bit PCM [-32767, 32767] @ 24000 Hz

size_t input_samples = rawAudio.size() / sizeof(float);  // 除以 4
const float* float_data = reinterpret_cast<const float*>(rawAudio.data());

for (size_t i = 0; i < input_samples; i++) {
    float sample = float_data[i];
    
    // 限幅处理，防止溢出
    if (sample > 1.0f) sample = 1.0f;
    if (sample < -1.0f) sample = -1.0f;
    
    // 转换公式: Float → 16-bit PCM
    pcm16bit[i] = static_cast<int16_t>(sample * 32767.0f);
}
```

**转换说明**：
- **输入**: 32-bit Float (4 字节/样本)
- **输出**: 16-bit PCM (2 字节/样本)
- **数据量减半**: 96,000 bytes → 48,000 bytes (1秒音频)

**为什么使用 32767 而不是 32768？**
- 保持对称性：-1.0 → -32767, 0.0 → 0, 1.0 → 32767
- 避免溢出：32768 超出 int16_t 范围 [-32768, 32767]
- 符合音频处理标准实践

#### 步骤 3: 重采样到通话采样率
```cpp
// 使用 SpeexDSP 高质量重采样器
// 输入: 16-bit PCM @ 24000 Hz
// 输出: 16-bit PCM @ 8000 或 16000 Hz

SpeexResamplerState* resampler = speex_resampler_init(
    1,                          // 单声道
    24000,                      // 输入采样率 (固定 24000 Hz)
    target_rate,                // 输出采样率 (8000 或 16000 Hz)
    SWITCH_RESAMPLE_QUALITY,    // 高质量模式
    &err
);

speex_resampler_process_int(resampler, 0,
                            pcm16bit.data(),      // 输入: 24000 Hz
                            &in_len,
                            playbackSamples.data(), // 输出: 8000/16000 Hz
                            &out_len);
```

**采样率转换比例**：
- **24000 Hz → 8000 Hz**: 降采样 3:1
  - 输入: 2400 samples (100ms) → 输出: 800 samples (100ms)
  - 数据量: 4800 bytes → 1600 bytes
- **24000 Hz → 16000 Hz**: 降采样 3:2
  - 输入: 2400 samples (100ms) → 输出: 1600 samples (100ms)
  - 数据量: 4800 bytes → 3200 bytes

**为什么需要降采样？**
- VoIP 通话通常使用 8000 Hz (窄带) 或 16000 Hz (宽带)
- 24000 Hz 对于电话通话来说过高，会浪费带宽
- FreeSWitch 要求音频采样率与通话会话匹配

#### 步骤 4: 写入播放缓冲区
```cpp
// 格式: 16-bit Linear PCM, 通话采样率, 单声道, 小端序
switch_buffer_write(tech_pvt->play_buffer,
                   (uint8_t*)playbackSamples.data(),
                   data_size);
```

### 3. FreeSWitch 播放格式 (WRITE_REPLACE)

从播放缓冲区读取并注入到通话中的音频格式：

```
编码格式: 16-bit Linear PCM (有符号整数)
采样率: 与通话一致 (通常 8000 或 16000 Hz)
声道数: 单声道 (Mono)
位深度: 16-bit (2 字节)
字节序: 小端序 (Little Endian)
帧长度: 20ms (标准 RTP 帧)
  - 8000 Hz: 320 字节 (160 samples × 2 bytes)
  - 16000 Hz: 640 字节 (320 samples × 2 bytes)
```

### 4. 播放流程

```cpp
void stream_play_frame(switch_media_bug_t *bug, private_t *tech_pvt) {
    // 1. 获取输出帧
    switch_frame_t *out_frame = switch_core_media_bug_get_write_replace_frame(bug);
    
    // 2. 计算 20ms 帧长度
    size_t target_bytes = FRAME_SIZE_8000 * tech_pvt->sampling / 8000 * tech_pvt->channels;
    
    // 3. 从缓冲区读取 16-bit PCM 数据
    switch_buffer_read(tech_pvt->play_buffer, out_frame->data, read_size);
    
    // 4. 设置帧参数
    out_frame->datalen = target_bytes;
    out_frame->samples = target_bytes / (tech_pvt->channels * sizeof(int16_t));
    out_frame->rate = tech_pvt->sampling;
    out_frame->channels = tech_pvt->channels;
    
    // 5. 替换写方向音频（播放给线路）
    switch_core_media_bug_set_write_replace_frame(bug, out_frame);
}
```

## 格式对比表

| 阶段 | 格式 | 采样率 | 位深度 | 字节/样本 | 字节序 | 1秒数据量 |
|------|------|--------|--------|----------|--------|----------|
| WebSocket 接收 | 32-bit Float | 24000 Hz | 32-bit | 4 bytes | 小端序 | 96,000 bytes |
| Float→PCM 转换 | 16-bit PCM | 24000 Hz | 16-bit | 2 bytes | 小端序 | 48,000 bytes |
| 重采样 (8kHz) | 16-bit PCM | 8000 Hz | 16-bit | 2 bytes | 小端序 | 16,000 bytes |
| 重采样 (16kHz) | 16-bit PCM | 16000 Hz | 16-bit | 2 bytes | 小端序 | 32,000 bytes |
| 播放缓冲区 | 16-bit PCM | 8000/16000 Hz | 16-bit | 2 bytes | 小端序 | 16k/32k bytes |
| WRITE_REPLACE | 16-bit PCM | 8000/16000 Hz | 16-bit | 2 bytes | 小端序 | 16k/32k bytes |
| 文件保存 | G.711 A-law | 8000 Hz | 8-bit | 1 byte | 小端序 | 8,000 bytes |

**数据量变化**：
- 原始 WebSocket: 96 KB/秒 (32-bit Float @ 24kHz)
- 转换后: 48 KB/秒 (16-bit PCM @ 24kHz) - **减少 50%**
- 重采样到 8kHz: 16 KB/秒 - **减少 83%**
- 重采样到 16kHz: 32 KB/秒 - **减少 67%**

## 关键技术点

### 1. 为什么使用 16-bit Linear PCM？
- FreeSWitch 内部音频处理标准格式
- 与所有编解码器兼容
- 无损质量，适合实时处理
- 后续会根据通话编解码器自动编码 (G.711/Opus/etc.)

### 2. 为什么需要重采样？
- WebSocket 音频采样率可能与通话不一致
- FreeSWitch 要求音频采样率与通话会话匹配
- SpeexDSP 提供高质量重采样算法

### 3. 为什么使用 20ms 帧？
- VoIP 标准帧长度
- 平衡延迟和效率
- 符合 RTP 协议规范

### 4. 缓冲区大小
```cpp
// 10秒缓冲区，防止突发音频丢失
const size_t play_buflen = desiredSampling * channels * sizeof(int16_t) * 10;

// 8000 Hz: 160,000 字节
// 16000 Hz: 320,000 字节
```

## 数据流向图

```
WebSocket 接收
│ 格式: 32-bit Float, 24000 Hz, Mono, LE
│ 数据量: 96 KB/秒
│ Base64 编码
    ↓ Base64 解码
Raw 32-bit Float 数据
│ 24000 samples/秒 × 4 bytes = 96 KB/秒
    ↓ Float32 → 16-bit PCM 转换
16-bit PCM @ 24000 Hz
│ 24000 samples/秒 × 2 bytes = 48 KB/秒
│ 数据量减少 50%
    ↓ SpeexDSP 重采样 (3:1 或 3:2)
16-bit PCM @ 8000 或 16000 Hz
│ 8000 Hz: 16 KB/秒 (降采样 3:1)
│ 16000 Hz: 32 KB/秒 (降采样 3:2)
    ↓ 写入播放缓冲区
播放缓冲区 (10秒容量)
│ 格式: 16-bit Linear PCM
│ 采样率: 与通话一致
│ 缓冲区大小: 160 KB (8kHz) 或 320 KB (16kHz)
    ↓ 每 20ms 读取一帧
WRITE_REPLACE 回调
│ 8000 Hz: 320 bytes/帧 (160 samples)
│ 16000 Hz: 640 bytes/帧 (320 samples)
    ↓ FreeSWitch 自动编码
通话编解码器
│ G.711 A-law/μ-law (8 kbps)
│ Opus (6-510 kbps)
│ G.722 (64 kbps)
│ 等等
    ↓ RTP 传输
对端接收并播放
```

## 代码位置

- **接收和转换**: `audio_streamer_glue.cpp::processMessage()`
- **播放注入**: `audio_streamer_glue.cpp::stream_play_frame()`
- **缓冲区初始化**: `audio_streamer_glue.cpp::stream_data_init()`
- **Media Bug 回调**: `mod_audio_stream.c::capture_callback()`

## 测试验证

### 验证音频格式正确性
1. 检查日志中的采样率和声道数
2. 确认缓冲区使用情况 (buffer_ms)
3. 监听通话质量，确保无失真

### 常见问题排查
- **音频失真**: 检查 Float32 转换是否正确限幅
- **播放速度异常**: 检查采样率是否匹配
- **音频断续**: 检查缓冲区是否频繁为空
- **无声音**: 检查 stream_play_enabled 标志

## 总结

整个转换流程确保：
1. ✅ 从 WebSocket 接收的 Float32 音频正确转换为 16-bit PCM
2. ✅ 采样率重采样到与通话一致
3. ✅ 格式符合 FreeSWitch 播放要求 (16-bit Linear PCM)
4. ✅ 使用标准 20ms 帧长度
5. ✅ 小端序字节序保持一致
6. ✅ 缓冲区管理防止音频丢失

**最终播放格式: 16-bit Linear PCM, 8000/16000 Hz, Mono, Little Endian, 20ms 帧**
