# FreeSWITCH 流式播放实现文档

## 概述

本文档描述了 `mod_audio_stream` 模块中流式播放功能的实现原理，该功能允许将从 WebSocket 接收的 TTS 音频实时播放给 SIP 线路。

## 呼叫架构

```
线路（SIP话机/网关）<---SIP--->  FreeSWITCH  <---WebSocket--->  AI服务
```

## 音频流向

### 1. 上行（采集）- 已实现
```
线路 → FreeSWITCH → WebSocket
```
- 线路的语音通过 SIP 传输到 FreeSWITCH
- FreeSWITCH 通过 READ 回调采集音频
- 音频通过 WebSocket 发送给 AI 服务

### 2. 下行（播放）- 本文档重点
```
WebSocket → FreeSWITCH → 线路
```
- AI 服务生成 TTS 音频，通过 WebSocket 发送
- FreeSWITCH 接收并缓存音频数据
- 通过 WRITE_REPLACE 机制播放给线路

## FreeSWITCH Media Bug 方向说明

在 FreeSWITCH 中，音频方向的定义：

- **READ 方向**：从线路采集音频（线路 → FS）
  - 用于采集线路说话的声音
  - 对应上行音频流

- **WRITE 方向**：向线路播放音频（FS → 线路）
  - 用于播放声音给线路
  - 对应下行音频流

## 实现原理

### 1. 音频接收与缓存

当从 WebSocket 接收到 TTS 音频数据时：

```cpp
// 在 AudioStreamer::processMessage() 中
// 1. 接收 Float32 格式的音频（24000 Hz）
// 2. 转换为 16-bit PCM
// 3. 重采样到通话采样率（如 8000 Hz）
// 4. 写入播放缓冲区 (play_buffer)

switch_buffer_write(tech_pvt->play_buffer,
                   (uint8_t*)playbackSamples.data(),
                   data_size);
```

**关键点**：
- 原始格式：Float32, 24000 Hz, 单声道, 小端序
- 转换为：16-bit PCM, 通话采样率, 单声道
- 缓冲区大小：10 秒（防止突发音频丢失）

### 2. 音频播放机制

通过 FreeSWITCH 的 WRITE_REPLACE 回调实现：

```cpp
// 在 capture_callback() 中
case SWITCH_ABC_TYPE_WRITE:
case SWITCH_ABC_TYPE_WRITE_REPLACE:
    if (tech_pvt->stream_play_enabled) {
        stream_play_frame(bug, tech_pvt);
    }
    return SWITCH_TRUE;
```

**工作流程**：

1. **触发时机**：当有音频要播放给线路时，WRITE 回调被触发
2. **读取缓冲区**：从 `play_buffer` 读取一帧音频（通常 20ms）
3. **替换音频**：通过 `switch_core_media_bug_set_write_replace_frame()` 替换原音频
4. **播放给线路**：线路听到的是 TTS 音频

### 3. 核心函数：stream_play_frame()

```cpp
void stream_play_frame(switch_media_bug_t *bug, private_t *tech_pvt) {
    // 1. 获取写方向的替换帧
    switch_frame_t *out_frame = switch_core_media_bug_get_write_replace_frame(bug);
    
    // 2. 从播放缓冲区读取音频
    switch_mutex_lock(tech_pvt->play_mutex);
    size_t inuse = switch_buffer_inuse(tech_pvt->play_buffer);
    
    if (inuse > 0) {
        // 读取一帧数据（20ms）
        switch_buffer_read(tech_pvt->play_buffer, out_frame->data, read_size);
        
        // 设置帧参数
        out_frame->datalen = target_bytes;
        out_frame->samples = target_bytes / (channels * sizeof(int16_t));
        out_frame->rate = sampling;
        out_frame->channels = channels;
    }
    switch_mutex_unlock(tech_pvt->play_mutex);
    
    // 3. 设置替换帧
    switch_core_media_bug_set_write_replace_frame(bug, out_frame);
}
```

## 数据流程图

```
┌─────────────────────────────────────────────────────────────────┐
│                         AI 服务 (WebSocket)                      │
└────────────────────────────┬────────────────────────────────────┘
                             │
                             │ TTS 音频数据
                             │ (Float32, 24000Hz)
                             ↓
┌─────────────────────────────────────────────────────────────────┐
│                    AudioStreamer::processMessage()               │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │ 1. Base64 解码                                            │   │
│  │ 2. Float32 → 16-bit PCM                                  │   │
│  │ 3. 重采样 (24000Hz → 通话采样率)                          │   │
│  │ 4. 写入 play_buffer                                       │   │
│  └──────────────────────────────────────────────────────────┘   │
└────────────────────────────┬────────────────────────────────────┘
                             │
                             ↓
                    ┌────────────────┐
                    │  play_buffer   │
                    │  (10秒缓冲)     │
                    └────────┬───────┘
                             │
                             │ 每 20ms 读取一帧
                             ↓
┌─────────────────────────────────────────────────────────────────┐
│              capture_callback (WRITE_REPLACE)                    │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │ stream_play_frame()                                       │   │
│  │  - 从 play_buffer 读取音频                                │   │
│  │  - 填充到 write_replace_frame                             │   │
│  │  - 设置帧参数（采样率、声道等）                            │   │
│  └──────────────────────────────────────────────────────────┘   │
└────────────────────────────┬────────────────────────────────────┘
                             │
                             │ 替换后的音频
                             ↓
┌─────────────────────────────────────────────────────────────────┐
│                    FreeSWITCH 核心                               │
│                    (编码并通过 SIP 发送)                          │
└────────────────────────────┬────────────────────────────────────┘
                             │
                             │ RTP 音频流
                             ↓
                    ┌────────────────┐
                    │  线路 (SIP)     │
                    │  听到 TTS 音频  │
                    └────────────────┘
```

## 关键配置

### 1. Media Bug 标志

```c
// 在 start_capture() 中
flags |= SMBF_READ_STREAM;      // 采集上行音频
flags |= SMBF_WRITE_STREAM;     // 监听下行音频
flags |= SMBF_WRITE_REPLACE;    // 替换下行音频
```

### 2. 缓冲区配置

```cpp
// 播放缓冲区：10 秒
const size_t play_buflen = desiredSampling * channels * sizeof(int16_t) * 10;
```

### 3. 采样率处理

- **输入**：24000 Hz (TTS 音频)
- **输出**：通话采样率（通常 8000 Hz 或 16000 Hz）
- **重采样**：使用 SpeexDSP 库

## 音频格式转换

### 接收到的音频格式
```json
{
  "type": "streamAudio",
  "data": {
    "audioDataType": "raw",
    "sampleRate": 24000,
    "audioData": "base64编码的Float32数据"
  }
}
```

### 转换步骤

1. **Base64 解码** → 原始字节流
2. **Float32 解析** → 浮点数组 (范围: -1.0 ~ 1.0)
3. **PCM 转换** → 16-bit 整数 (范围: -32768 ~ 32767)
   ```cpp
   pcm16bit[i] = static_cast<int16_t>(float_sample * 32767.0f);
   ```
4. **重采样** → 目标采样率
   ```cpp
   speex_resampler_process_int(resampler, 0,
                               pcm16bit.data(), &in_len,
                               playbackSamples.data(), &out_len);
   ```

## 线程安全

所有对 `play_buffer` 的访问都通过互斥锁保护：

```cpp
switch_mutex_lock(tech_pvt->play_mutex);
// 读写 play_buffer
switch_mutex_unlock(tech_pvt->play_mutex);
```

## 错误处理

### 1. 缓冲区满
```cpp
if (available >= data_size) {
    // 写入成功
} else {
    // 丢弃音频并记录警告
    switch_log_printf(..., SWITCH_LOG_WARNING,
        "Play buffer full, dropping samples");
}
```

### 2. 缓冲区空
```cpp
if (inuse > 0) {
    // 播放音频
} else {
    // 不设置替换帧，保持原音频（静音或对端音频）
    return;
}
```

## 性能考虑

### 1. 缓冲区大小
- **10 秒缓冲**：足够处理网络抖动和突发音频
- **内存占用**：8000 Hz × 1 声道 × 2 字节 × 10 秒 = 160 KB

### 2. 回调频率
- **WRITE 回调**：每 20ms 触发一次（50 Hz）
- **每次处理**：320 字节 @ 8000 Hz（20ms 音频）

### 3. 重采样开销
- 使用 SpeexDSP 高质量重采样
- 只在接收时重采样一次，播放时直接使用

## 调试日志

### 关键日志点

1. **音频入队**
```
Streaming playback: queued 9599 samples @ 8000 Hz (buffer: 3443.50 ms)
```

2. **音频播放**
```
stream_play_frame injected 320/320 bytes, 160 samples @ 8000 Hz, 1 ch (buffer left: 3298.42 ms)
```

3. **回调触发**
```
capture_callback WRITE/WRITE_REPLACE (type=4) - stream_play_enabled=1
```

## 常见问题

### Q1: 为什么使用 WRITE_REPLACE 而不是 READ_REPLACE？

**A**: 
- **READ** 方向：线路 → FS（采集线路的声音）
- **WRITE** 方向：FS → 线路（播放给线路）
- 我们要播放 TTS 给线路听，所以用 WRITE_REPLACE

### Q2: 为什么不直接使用 switch_ivr_play_file？

**A**: 
- `switch_ivr_play_file` 是阻塞式播放，会阻塞通话
- 流式播放允许边接收边播放，延迟更低
- 可以与采集同时进行，实现全双工

### Q3: 如何确保音频不会播放太快或太慢？

**A**: 
- WRITE 回调由 FreeSWITCH 核心按固定频率触发（20ms）
- 每次只读取一帧（20ms）的数据
- 播放速度由 FreeSWITCH 的时钟控制，自动同步

### Q4: 缓冲区满了怎么办？

**A**: 
- 新的音频会被丢弃，记录警告日志
- 10 秒缓冲通常足够，除非网络严重延迟
- 可以通过调整缓冲区大小来适应不同场景

## 未来优化方向

1. **动态缓冲区**：根据网络状况自动调整缓冲区大小
2. **音频混合**：支持 TTS 与麦克风音频混合
3. **优先级队列**：支持紧急音频插队播放
4. **音量控制**：支持动态调整播放音量
5. **淡入淡出**：音频切换时的平滑过渡

## 相关文件

- `mod_audio_stream.c` - 模块主文件，回调处理
- `audio_streamer_glue.cpp` - 音频处理和播放逻辑
- `mod_audio_stream.h` - 数据结构定义

## 参考资料

- [FreeSWITCH Media Bug API](https://freeswitch.org/confluence/display/FREESWITCH/Media+Bug+API)
- [SpeexDSP Resampler](https://www.speex.org/docs/api/speex-api-reference/group__Resampler.html)
