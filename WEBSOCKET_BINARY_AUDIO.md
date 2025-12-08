# WebSocket 原始二进制音频流支持

## 概述

本次修改支持 WebSocket 接收原始的 PCM S16BE（16-bit Signed Big Endian）二进制音频流，而不是之前的 Base64 编码的 JSON 格式。

## 修改内容

### 1. 音频格式变更

**之前的格式（JSON + Base64）：**
```json
{
  "type": "streamAudio",
  "data": {
    "audioData": "base64_encoded_audio_data",
    "audioDataType": "raw",
    "sampleRate": 24000
  }
}
```

**现在的格式（原始二进制）：**
- 直接发送原始 PCM 音频数据
- 格式：PCM S16BE（16-bit Signed Big Endian）
- 采样率：24000 Hz（默认，可配置）
- 声道：单声道（Mono）
- 无 Base64 编码
- 无 JSON 封装

### 2. 代码修改

#### 2.1 添加二进制消息事件类型

**文件：`mod_audio_stream.h`**

在 `notifyEvent_t` 枚举中添加了 `BINARY_MESSAGE` 事件类型：

```cpp
enum notifyEvent_t {
    CONNECT_SUCCESS,
    CONNECT_ERROR,
    CONNECTION_DROPPED,
    MESSAGE,
    BINARY_MESSAGE  // 新增：处理二进制音频流
};
```

#### 2.2 添加二进制消息回调

**文件：`audio_streamer_glue.cpp`**

在 `AudioStreamer` 构造函数中添加了二进制消息回调：

```cpp
// Setup binary message callback for raw PCM S16BE audio stream
client.setBinaryCallback([this](const std::string& data) {
    eventCallback(BINARY_MESSAGE, data.c_str(), data.size());
});
```

#### 2.3 修改事件回调函数

更新 `eventCallback` 函数签名，支持传递二进制数据长度：

```cpp
void eventCallback(notifyEvent_t event, const char* message, size_t len = 0)
```

添加了 `BINARY_MESSAGE` 事件处理分支：

```cpp
case BINARY_MESSAGE:
    // 处理原始 PCM S16BE 二进制数据流
    processBinaryAudio(psession, (const uint8_t*)message, len);
    break;
```

#### 2.4 实现二进制音频处理函数

新增 `processBinaryAudio` 函数，处理原始 PCM S16BE 二进制音频流：

```cpp
void processBinaryAudio(switch_core_session_t* session, const uint8_t* data, size_t len)
```

**处理流程：**

1. **大端序转小端序（S16BE → S16LE）**
   - 输入：PCM S16BE（大端序）
   - 输出：PCM S16LE（小端序）
   - 转换公式：`(high_byte << 8) | low_byte`

2. **重采样到通话采样率**
   - 使用 SpeexDSP 重采样器
   - 从 24000 Hz 重采样到通话采样率（8000/16000 Hz）
   - 保持单声道

3. **写入播放缓冲区**
   - 将重采样后的音频写入 `play_buffer`
   - 通过 `stream_play_frame` 函数注入到通话中
   - 使用互斥锁保护缓冲区访问

4. **缓冲区管理**
   - 检查缓冲区可用空间
   - 如果缓冲区已满，丢弃数据并记录警告
   - 记录缓冲区使用情况（毫秒）

## 音频数据流

```
WebSocket 接收
    ↓
原始 PCM S16BE 二进制数据
(24000 Hz, Mono, Big Endian)
    ↓
大端序 → 小端序转换
    ↓
PCM S16LE
(24000 Hz, Mono, Little Endian)
    ↓
SpeexDSP 重采样
    ↓
PCM S16LE
(8000/16000 Hz, Mono, Little Endian)
    ↓
写入播放缓冲区 (play_buffer)
    ↓
stream_play_frame 读取并注入
    ↓
FreeSWITCH WRITE_REPLACE
    ↓
自动编码为通话编码格式 (G.711/Opus/etc.)
    ↓
发送到对端
```

## 配置说明

### 采样率配置

默认假设 WebSocket 音频流采样率为 **24000 Hz**。如果需要修改，请在 `processBinaryAudio` 函数中调整：

```cpp
const int sampleRate = 24000;  // 修改为实际采样率
```

### 缓冲区大小

播放缓冲区默认为 **10 秒**，在 `stream_data_init` 函数中配置：

```cpp
const size_t play_buflen = sampling * channels * sizeof(int16_t) * 10;
```

## 兼容性说明

### WebSocketClient 库兼容性

本实现使用 **智能消息类型检测** 方式，无需修改 `libwsc` 库。

#### 检测逻辑（多重验证）

**方法 1: JSON 格式检测**
- 忽略前导空白字符（空格、制表符、换行符）
- 检查第一个有效字符是否为 `{` 或 `[`
- JSON 必须以这两个字符之一开头

**方法 2: 二进制特征检测**
- 检查前 64 字节中的非可打印字符比例
- 如果超过 25% 是非可打印字符 → 视为二进制数据
- 排除常见空白字符（`\t`, `\n`, `\r`）

**判断流程：**
```
消息到达
    ↓
检查是否为空 → 是 → 忽略
    ↓ 否
跳过前导空白
    ↓
第一个字符是 '{' 或 '[' ? 
    ↓ 否
    视为二进制 ✓
    ↓ 是
检查非可打印字符比例
    ↓
> 25% ? 
    ↓ 是
    视为二进制 ✓
    ↓ 否
    视为 JSON 文本 ✓
```

**优点：**
- ✅ 兼容现有 `libwsc` 库，无需修改
- ✅ 支持带前导空白的 JSON
- ✅ 准确识别二进制音频数据
- ✅ 防止误判（多重验证）
- ✅ 性能优化（只检查前 64 字节）

## 调试日志

启用调试日志可以查看音频处理详情：

```cpp
switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG,
    "(%s) 接收二进制音频: %zu bytes → %zu samples @ %d Hz | 缓冲区: %.2f ms\n",
    m_sessionId.c_str(), len, playbackSamples.size(), target_rate, buffer_ms);
```

## 测试建议

1. **验证字节序转换**
   - 发送已知的测试音频（如 440Hz 正弦波）
   - 检查播放音频是否正确

2. **验证重采样质量**
   - 比较原始音频和重采样后的音频
   - 检查是否有失真或噪音

3. **验证缓冲区管理**
   - 发送大量音频数据
   - 检查缓冲区是否溢出
   - 验证丢包处理

4. **验证实时性**
   - 测量端到端延迟
   - 检查音频是否流畅播放

## 注意事项

1. **字节序**：确保 WebSocket 发送端使用大端序（Big Endian）格式
2. **采样率**：确保发送端和接收端采样率配置一致
3. **缓冲区**：根据网络延迟调整缓冲区大小
4. **性能**：大端序转换和重采样会消耗 CPU 资源

## 相关文件

- `mod_audio_stream.h` - 事件类型定义
- `audio_streamer_glue.cpp` - 音频流处理实现
- `libs/libwsc` - WebSocket 客户端库（子模块）
