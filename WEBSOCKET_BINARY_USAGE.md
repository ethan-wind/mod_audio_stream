# WebSocket 原始二进制音频流使用指南

## 快速开始

### 1. WebSocket 服务端发送音频

**Python 示例（使用 websockets 库）：**

```python
import asyncio
import websockets
import struct
import numpy as np

async def send_audio(websocket, path):
    # 生成测试音频：440Hz 正弦波，24000Hz 采样率
    sample_rate = 24000
    duration = 1.0  # 1秒
    frequency = 440  # A4 音符
    
    t = np.linspace(0, duration, int(sample_rate * duration), False)
    audio = np.sin(2 * np.pi * frequency * t)
    
    # 转换为 16-bit PCM
    audio_int16 = (audio * 32767).astype(np.int16)
    
    # 转换为大端序（Big Endian）
    audio_bytes = audio_int16.astype('>i2').tobytes()
    
    # 发送二进制数据
    await websocket.send(audio_bytes)
    print(f"发送 {len(audio_bytes)} 字节音频数据")

# 启动 WebSocket 服务器
start_server = websockets.serve(send_audio, "localhost", 8765)
asyncio.get_event_loop().run_until_complete(start_server)
asyncio.get_event_loop().run_forever()
```

**Node.js 示例（使用 ws 库）：**

```javascript
const WebSocket = require('ws');

const wss = new WebSocket.Server({ port: 8765 });

wss.on('connection', (ws) => {
    console.log('客户端已连接');
    
    // 生成测试音频：440Hz 正弦波，24000Hz 采样率
    const sampleRate = 24000;
    const duration = 1.0;
    const frequency = 440;
    const numSamples = Math.floor(sampleRate * duration);
    
    const buffer = Buffer.alloc(numSamples * 2); // 16-bit = 2 bytes
    
    for (let i = 0; i < numSamples; i++) {
        const t = i / sampleRate;
        const sample = Math.sin(2 * Math.PI * frequency * t);
        const value = Math.floor(sample * 32767);
        
        // 写入大端序（Big Endian）
        buffer.writeInt16BE(value, i * 2);
    }
    
    // 发送二进制数据
    ws.send(buffer, { binary: true });
    console.log(`发送 ${buffer.length} 字节音频数据`);
});

console.log('WebSocket 服务器运行在 ws://localhost:8765');
```

### 2. FreeSWITCH 配置

**拨号计划示例（dialplan）：**

```xml
<extension name="audio_stream_test">
  <condition field="destination_number" expression="^9999$">
    <action application="answer"/>
    <action application="audio_stream" data="ws://localhost:8765"/>
  </condition>
</extension>
```

### 3. 测试流程

1. 启动 WebSocket 服务器（Python 或 Node.js）
2. 在 FreeSWITCH 中拨打 9999
3. 应该能听到 440Hz 的正弦波音频

## 音频格式要求

### 必须遵守的格式

| 参数 | 值 | 说明 |
|------|-----|------|
| 编码格式 | PCM S16BE | 16-bit Signed Big Endian |
| 采样率 | 24000 Hz | 默认值，可在代码中修改 |
| 声道数 | 1 (Mono) | 单声道 |
| 字节序 | Big Endian | 高字节在前 |
| 数据格式 | 原始二进制 | 无封装，无 Base64 |

### 数据结构

```
每个样本 = 2 字节（16-bit）
字节序 = Big Endian

示例：样本值 = 0x1234
  字节 0: 0x12 (高字节)
  字节 1: 0x34 (低字节)

音频流 = 连续的样本数据
  [样本1_高字节][样本1_低字节][样本2_高字节][样本2_低字节]...
```

## 常见问题

### Q1: 听到的音频有噪音或失真

**可能原因：**
- 字节序错误（使用了小端序而不是大端序）
- 采样率不匹配
- 音频数据超出范围（-32768 到 32767）

**解决方法：**
```python
# 确保使用大端序
audio_bytes = audio_int16.astype('>i2').tobytes()

# 或者手动转换
import struct
audio_bytes = b''.join(struct.pack('>h', sample) for sample in audio_int16)
```

### Q2: 没有听到任何声音

**可能原因：**
- WebSocket 连接未建立
- 发送的是文本消息而不是二进制消息
- 缓冲区为空

**解决方法：**
```python
# 确保发送二进制消息
await websocket.send(audio_bytes)  # Python websockets

# Node.js
ws.send(buffer, { binary: true });  # 必须指定 binary: true
```

### Q3: 音频播放断断续续

**可能原因：**
- 网络延迟
- 发送速率不稳定
- 缓冲区太小

**解决方法：**
```python
# 按固定速率发送音频块
chunk_size = 960  # 20ms @ 24000Hz = 480 samples = 960 bytes
for i in range(0, len(audio_bytes), chunk_size):
    chunk = audio_bytes[i:i+chunk_size]
    await websocket.send(chunk)
    await asyncio.sleep(0.02)  # 20ms 间隔
```

### Q4: 如何修改采样率

**修改代码：**

在 `audio_streamer_glue.cpp` 的 `processBinaryAudio` 函数中：

```cpp
// 修改这一行
const int sampleRate = 24000;  // 改为你的采样率，如 16000, 48000 等
```

**重新编译：**
```bash
./build-mod-audio-stream.sh
```

## 性能优化

### 1. 批量发送

不要逐个样本发送，而是批量发送：

```python
# 推荐：每次发送 20ms 的音频
chunk_duration = 0.02  # 20ms
chunk_samples = int(sample_rate * chunk_duration)
chunk_size = chunk_samples * 2  # 16-bit = 2 bytes

for i in range(0, len(audio_bytes), chunk_size):
    chunk = audio_bytes[i:i+chunk_size]
    await websocket.send(chunk)
```

### 2. 使用流式处理

对于实时音频，使用流式处理而不是一次性加载：

```python
async def stream_audio_file(websocket, filename):
    with open(filename, 'rb') as f:
        while True:
            chunk = f.read(960)  # 20ms @ 24000Hz
            if not chunk:
                break
            await websocket.send(chunk)
            await asyncio.sleep(0.02)
```

### 3. 缓冲区管理

根据网络延迟调整缓冲区大小（在 `stream_data_init` 中）：

```cpp
// 默认 10 秒缓冲区
const size_t play_buflen = sampling * channels * sizeof(int16_t) * 10;

// 低延迟场景（局域网）：减少到 2 秒
const size_t play_buflen = sampling * channels * sizeof(int16_t) * 2;

// 高延迟场景（互联网）：增加到 20 秒
const size_t play_buflen = sampling * channels * sizeof(int16_t) * 20;
```

## 调试技巧

### 1. 启用详细日志

在 FreeSWITCH 控制台：

```
fsctl loglevel DEBUG
```

### 2. 查看音频流日志

```
grep "接收二进制音频" /var/log/freeswitch/freeswitch.log
```

### 3. 保存接收的音频用于分析

在 `processBinaryAudio` 函数中添加：

```cpp
// 保存原始音频到文件
static std::ofstream debug_file("/tmp/received_audio.raw", std::ios::binary);
debug_file.write((char*)data, len);
debug_file.flush();
```

然后使用 FFmpeg 播放：

```bash
ffplay -f s16be -ar 24000 -ac 1 /tmp/received_audio.raw
```

### 4. 验证字节序

```python
# 生成测试样本
test_sample = 0x1234

# 大端序（正确）
big_endian = struct.pack('>h', test_sample)
print(f"大端序: {big_endian.hex()}")  # 应该输出: 1234

# 小端序（错误）
little_endian = struct.pack('<h', test_sample)
print(f"小端序: {little_endian.hex()}")  # 应该输出: 3412
```

## 完整示例：实时麦克风流

**Python + PyAudio 示例：**

```python
import asyncio
import websockets
import pyaudio
import struct

async def stream_microphone(websocket, path):
    CHUNK = 480  # 20ms @ 24000Hz
    FORMAT = pyaudio.paInt16
    CHANNELS = 1
    RATE = 24000
    
    p = pyaudio.PyAudio()
    stream = p.open(format=FORMAT,
                    channels=CHANNELS,
                    rate=RATE,
                    input=True,
                    frames_per_buffer=CHUNK)
    
    print("开始录音...")
    
    try:
        while True:
            # 读取麦克风数据
            data = stream.read(CHUNK)
            
            # 转换为大端序
            samples = struct.unpack(f'<{CHUNK}h', data)  # 小端序读取
            big_endian_data = struct.pack(f'>{CHUNK}h', *samples)  # 大端序打包
            
            # 发送到 WebSocket
            await websocket.send(big_endian_data)
            
    except Exception as e:
        print(f"错误: {e}")
    finally:
        stream.stop_stream()
        stream.close()
        p.terminate()

start_server = websockets.serve(stream_microphone, "localhost", 8765)
asyncio.get_event_loop().run_until_complete(start_server)
asyncio.get_event_loop().run_forever()
```

## 相关文档

- [WEBSOCKET_BINARY_AUDIO.md](WEBSOCKET_BINARY_AUDIO.md) - 技术实现细节
- [STREAMING_PLAYBACK.md](STREAMING_PLAYBACK.md) - 流式播放原理
- [AUDIO_FORMAT_QUICK_REF.md](AUDIO_FORMAT_QUICK_REF.md) - 音频格式参考
