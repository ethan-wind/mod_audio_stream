# 二进制音频流支持

## 概述

mod_audio_stream 现在支持通过 WebSocket 接收二进制格式的音频流，并实时播放给通话对方。

## 支持的音频格式

- **PCM S16LE** (16-bit Signed Little Endian) - 默认格式
- **PCM S16BE** (16-bit Signed Big Endian)
- 采样率：8000 Hz - 48000 Hz（可配置，默认 8000 Hz）
- 声道：单声道 (Mono)

## 工作原理

1. **消息类型自动识别**：WebSocket 收到消息后，自动判断是 JSON 文本还是二进制音频
   - JSON 消息：以 `{` 或 `[` 开头
   - 二进制消息：其他所有数据

2. **数据缓冲**：由于 WebSocket 可能以小片段发送数据，系统会自动累积数据直到达到配置的块大小
   - 默认块大小：640 bytes (20ms @ 16kHz 单声道)
   - 可通过 `STREAM_BINARY_CHUNK_SIZE` 变量配置

3. **自动重采样**：接收到的音频会自动重采样到通话采样率（8000 Hz 或 16000 Hz）

4. **实时播放**：处理后的音频写入播放缓冲区，通过 WRITE_REPLACE 回调播放给通话对方

## FreeSWITCH 配置变量

在拨号计划中设置以下变量来配置二进制音频流：

### 基本配置

```xml
<!-- 设置二进制音频格式 (s16le 或 s16be) -->
<action application="set" data="STREAM_BINARY_FORMAT=s16le"/>

<!-- 设置二进制音频采样率 (Hz) -->
<action application="set" data="STREAM_BINARY_RATE=16000"/>

<!-- 设置二进制数据块大小 (bytes) -->
<!-- 默认 640 bytes = 20ms @ 16kHz -->
<!-- 计算公式: 采样率 * 0.02 * 2 (16-bit = 2 bytes) -->
<action application="set" data="STREAM_BINARY_CHUNK_SIZE=640"/>

<!-- 禁用二进制缓冲（直接处理每个 WebSocket 消息） -->
<action application="set" data="STREAM_BINARY_BUFFER_DISABLED=true"/>
```

### 完整示例

```xml
<extension name="audio_stream_binary">
  <condition field="destination_number" expression="^9999$">
    <!-- 配置二进制音频流 -->
    <action application="set" data="STREAM_BINARY_FORMAT=s16le"/>
    <action application="set" data="STREAM_BINARY_RATE=16000"/>
    <action application="set" data="STREAM_BINARY_CHUNK_SIZE=640"/>
    
    <!-- 启动音频流 -->
    <action application="answer"/>
    <action application="uuid_audio_stream" data="${uuid} start wss://your-server.com/audio mono 8000"/>
    <action application="park"/>
  </condition>
</extension>
```

## 块大小计算

根据采样率和帧长度计算块大小：

| 采样率 | 帧长度 | 块大小 (bytes) | 计算公式 |
|--------|--------|----------------|----------|
| 8000 Hz | 20ms | 320 | 8000 * 0.02 * 2 |
| 16000 Hz | 20ms | 640 | 16000 * 0.02 * 2 |
| 24000 Hz | 20ms | 960 | 24000 * 0.02 * 2 |
| 48000 Hz | 20ms | 1920 | 48000 * 0.02 * 2 |

## WebSocket 服务器端实现示例

### Node.js (使用 ws 库)

```javascript
const WebSocket = require('ws');
const wss = new WebSocket.Server({ port: 8080 });

wss.on('connection', (ws) => {
  console.log('客户端已连接');
  
  // 发送二进制音频数据
  const sampleRate = 16000;
  const chunkSize = 640; // 20ms @ 16kHz
  
  // 生成测试音频（440Hz 正弦波）
  const buffer = Buffer.alloc(chunkSize);
  for (let i = 0; i < chunkSize / 2; i++) {
    const sample = Math.sin(2 * Math.PI * 440 * i / sampleRate) * 32767;
    buffer.writeInt16LE(sample, i * 2);
  }
  
  // 每 20ms 发送一次
  const interval = setInterval(() => {
    if (ws.readyState === WebSocket.OPEN) {
      ws.send(buffer, { binary: true });
    }
  }, 20);
  
  ws.on('close', () => {
    clearInterval(interval);
    console.log('客户端已断开');
  });
});
```

### Python (使用 websockets 库)

```python
import asyncio
import websockets
import struct
import math

async def audio_handler(websocket, path):
    print("客户端已连接")
    
    sample_rate = 16000
    chunk_size = 640  # 20ms @ 16kHz
    frequency = 440  # Hz
    
    try:
        while True:
            # 生成测试音频（440Hz 正弦波）
            samples = []
            for i in range(chunk_size // 2):
                sample = int(math.sin(2 * math.pi * frequency * i / sample_rate) * 32767)
                samples.append(sample)
            
            # 打包为 16-bit little-endian
            audio_data = struct.pack('<' + 'h' * len(samples), *samples)
            
            # 发送二进制数据
            await websocket.send(audio_data)
            
            # 等待 20ms
            await asyncio.sleep(0.02)
            
    except websockets.exceptions.ConnectionClosed:
        print("客户端已断开")

start_server = websockets.serve(audio_handler, "0.0.0.0", 8080)
asyncio.get_event_loop().run_until_complete(start_server)
asyncio.get_event_loop().run_forever()
```

## 调试

### 启用详细日志

```bash
# 在 FreeSWITCH 控制台
fs_cli> console loglevel debug
```

### 查看二进制数据接收情况

日志会显示：
- 接收到的二进制消息大小
- 字节序转换信息
- 重采样过程
- 缓冲区状态

示例日志：
```
[DEBUG] audio_streamer_glue.cpp:185 🎵 [WebSocket] 二进制消息: 640 bytes
[DEBUG] audio_streamer_glue.cpp:460 接收二进制数据 #50: 640 bytes (格式: S16LE, 采样率: 16000 Hz)
[DEBUG] audio_streamer_glue.cpp:1169 🔊 [播放音频] 帧#50: 读取=320 bytes (160 samples), 缓冲区: 2.00 ms → 1.80 ms
```

## 性能优化建议

1. **块大小设置**：
   - 较小的块（如 320 bytes）：延迟更低，但 CPU 开销更高
   - 较大的块（如 1920 bytes）：CPU 开销更低，但延迟稍高
   - 推荐：640 bytes (20ms @ 16kHz)

2. **缓冲区管理**：
   - 默认启用缓冲，适合大多数场景
   - 如果 WebSocket 服务器已经按固定大小发送数据，可以禁用缓冲

3. **采样率选择**：
   - 如果 WebSocket 音频采样率与通话采样率一致，无需重采样，性能最佳
   - 推荐：WebSocket 使用 8000 Hz 或 16000 Hz

## 故障排除

### 问题：收到的音频有杂音或断断续续

**可能原因**：
- WebSocket 发送的数据块太小，导致频繁处理
- 网络延迟或丢包

**解决方案**：
- 增加 `STREAM_BINARY_CHUNK_SIZE` 值
- 检查网络连接质量
- 在 WebSocket 服务器端增加缓冲

### 问题：音频播放延迟过高

**可能原因**：
- 块大小设置过大
- 播放缓冲区积累过多数据

**解决方案**：
- 减小 `STREAM_BINARY_CHUNK_SIZE` 值
- 调整 WebSocket 服务器的发送频率

### 问题：日志显示"二进制音频长度不是 2 字节对齐"

**可能原因**：
- WebSocket 发送的数据大小为奇数字节
- 数据在传输过程中被截断

**解决方案**：
- 确保 WebSocket 服务器发送的数据大小是偶数（16-bit = 2 bytes）
- 启用缓冲区（默认已启用）会自动处理对齐问题

## 技术细节

### 数据流程

```
WebSocket 二进制消息
    ↓
消息类型识别 (JSON vs 二进制)
    ↓
数据缓冲累积 (可选)
    ↓
字节序转换 (S16BE → S16LE)
    ↓
重采样 (输入采样率 → 通话采样率)
    ↓
写入播放缓冲区
    ↓
WRITE_REPLACE 回调读取
    ↓
播放给通话对方
```

### 内存管理

- 二进制缓冲区使用 `std::vector<uint8_t>`，自动管理内存
- 播放缓冲区使用 FreeSWITCH 的 `switch_buffer_t`，默认 10 秒容量
- 重采样使用 SpeexDSP 库，高质量低延迟

## 兼容性

- FreeSWITCH 1.10+
- 需要 SpeexDSP 库
- 支持所有 WebSocket 库（只要能发送二进制数据）
