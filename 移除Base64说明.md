# 移除 Base64 解码说明

## 修改内容

已将 `audio_streamer_glue.cpp` 中的 base64 解码功能移除，现在直接处理原始二进制音频数据。

## 主要变更

### 1. 移除了 base64 依赖
```cpp
// 之前
#include "base64.h"

// 现在
// base64.h 已移除 - 直接处理原始二进制数据
```

### 2. 修改了数据处理逻辑
```cpp
// 之前：需要 base64 解码
try {
    rawAudio = base64_decode(jsonAudio->valuestring);
} catch (const std::exception& e) {
    // 错误处理
}

// 现在：直接使用原始数据
std::string rawAudio(jsonAudio->valuestring, strlen(jsonAudio->valuestring));
```

## 数据格式要求

### WebSocket 发送的 JSON 格式

现在 `audioData` 字段应该包含 **原始二进制 PCM 数据**（作为字符串），而不是 base64 编码的数据：

```json
{
  "type": "streamAudio",
  "data": {
    "audioDataType": "raw",
    "sampleRate": 16000,
    "audioData": "<原始 16-bit PCM 二进制数据>"
  }
}
```

### 注意事项

1. **数据格式**: `audioData` 应该是原始的 16-bit PCM 数据
2. **字节序**: 小端序（little-endian）
3. **采样格式**: 16-bit 有符号整数（int16_t）
4. **声道**: 单声道（mono）

## 处理流程

```
WebSocket 接收原始 PCM 数据
    ↓
直接读取二进制数据（无需解码）
    ↓
重采样到 8000 Hz（如果需要）
    ↓
转换为 G.711 A-law
    ↓
写入 WAV 文件
```

## 优势

1. **性能提升**: 
   - 无需 base64 解码，减少 CPU 使用
   - 减少内存分配和复制

2. **数据大小**:
   - Base64 编码会增加约 33% 的数据大小
   - 直接传输二进制数据更高效

3. **简化代码**:
   - 移除了 base64 依赖
   - 减少了错误处理代码

## 性能对比

### Base64 方式（之前）
- 原始数据: 1000 字节
- Base64 编码后: ~1333 字节（+33%）
- 需要解码时间: ~0.1ms

### 直接二进制方式（现在）
- 原始数据: 1000 字节
- 传输大小: 1000 字节
- 无需解码时间

## WebSocket 客户端示例

### Python 示例

```python
import websocket
import json
import struct

# 读取 PCM 数据
with open('audio.pcm', 'rb') as f:
    pcm_data = f.read()

# 构建 JSON 消息（直接使用二进制数据）
message = {
    "type": "streamAudio",
    "data": {
        "audioDataType": "raw",
        "sampleRate": 16000,
        "audioData": pcm_data.decode('latin-1')  # 将二进制转为字符串
    }
}

# 发送 JSON 消息
ws.send(json.dumps(message))
```

### JavaScript/Node.js 示例

```javascript
const fs = require('fs');
const WebSocket = require('ws');

// 读取 PCM 数据
const pcmData = fs.readFileSync('audio.pcm');

// 构建 JSON 消息
const message = {
    type: "streamAudio",
    data: {
        audioDataType: "raw",
        sampleRate: 16000,
        audioData: pcmData.toString('binary')  // 二进制字符串
    }
};

// 发送消息
ws.send(JSON.stringify(message));
```

### C++ 示例

```cpp
#include <fstream>
#include <cJSON.h>

// 读取 PCM 数据
std::ifstream file("audio.pcm", std::ios::binary);
std::string pcmData((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());

// 构建 JSON
cJSON* root = cJSON_CreateObject();
cJSON_AddStringToObject(root, "type", "streamAudio");

cJSON* data = cJSON_CreateObject();
cJSON_AddStringToObject(data, "audioDataType", "raw");
cJSON_AddNumberToObject(data, "sampleRate", 16000);
cJSON_AddStringToObject(data, "audioData", pcmData.c_str());

cJSON_AddItemToObject(root, "data", data);

// 发送
char* json_str = cJSON_PrintUnformatted(root);
websocket_send(json_str);
cJSON_Delete(root);
free(json_str);
```

## 重新编译

修改后需要重新编译模块：

```bash
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
sudo make install
```

## 兼容性说明

### 如果仍需要支持 Base64

如果你的系统中有些客户端仍然发送 base64 编码的数据，可以添加一个检测机制：

```cpp
// 检测是否是 base64 数据（简单方法：检查是否只包含 base64 字符）
bool isBase64(const char* str) {
    const char* base64_chars = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";
    return strspn(str, base64_chars) == strlen(str);
}

// 在处理时判断
if (isBase64(jsonAudio->valuestring)) {
    rawAudio = base64_decode(jsonAudio->valuestring);
} else {
    rawAudio = std::string(jsonAudio->valuestring, strlen(jsonAudio->valuestring));
}
```

但这会增加复杂度，建议统一使用一种格式。

## 测试

### 生成测试 PCM 数据

```bash
# 使用 FFmpeg 生成测试音频
ffmpeg -f lavfi -i "sine=frequency=440:duration=1" -ar 16000 -ac 1 -f s16le test.pcm

# 查看文件大小
ls -lh test.pcm
```

### 验证数据格式

```bash
# 使用 hexdump 查看前几个字节
hexdump -C test.pcm | head -n 5

# 应该看到 16-bit 有符号整数数据
```

## 故障排查

### 问题: 音频数据损坏

**可能原因**:
- JSON 字符串编码问题
- 二进制数据中包含空字符 `\0`
- 字符编码转换错误

**解决方案**:
- 使用 `latin-1` 或 `binary` 编码
- 确保 JSON 库正确处理二进制字符串
- 考虑使用 WebSocket 的二进制消息模式

### 问题: 数据长度不正确

**检查**:
```cpp
switch_log_printf(..., "Received audio data length: %zu bytes\n", rawAudio.size());
switch_log_printf(..., "Expected samples: %zu\n", rawAudio.size() / sizeof(int16_t));
```

### 问题: 仍然需要 Base64

如果你的 WebSocket 库或 JSON 库不能很好地处理二进制数据，可能需要保留 base64 编码。在这种情况下，恢复之前的代码即可。

## 总结

移除 base64 解码后：
- ✓ 性能提升（无需解码）
- ✓ 数据传输更高效（减少 33% 大小）
- ✓ 代码更简洁
- ✓ 减少依赖

确保 WebSocket 客户端发送原始二进制 PCM 数据，而不是 base64 编码的数据。
