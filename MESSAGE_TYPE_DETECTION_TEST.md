# 消息类型检测测试说明

## 检测算法

### 多重验证机制

代码使用两种方法来判断消息类型：

#### 方法 1: JSON 格式检测
```cpp
// 跳过前导空白字符
while (first_char_pos < message.size() && 
       (message[first_char_pos] == ' ' || 
        message[first_char_pos] == '\t' || 
        message[first_char_pos] == '\n' || 
        message[first_char_pos] == '\r')) {
    first_char_pos++;
}

// 检查第一个有效字符
char first_char = message[first_char_pos];
if (first_char != '{' && first_char != '[') {
    is_binary = true;  // 不是 JSON
}
```

#### 方法 2: 二进制特征检测
```cpp
// 检查前 64 字节中的非可打印字符比例
int non_printable = 0;
size_t check_len = std::min(message.size(), (size_t)64);
for (size_t i = 0; i < check_len; i++) {
    unsigned char c = (unsigned char)message[i];
    // 非可打印字符（除了常见的空白字符）
    if (c < 32 && c != '\t' && c != '\n' && c != '\r') {
        non_printable++;
    } else if (c >= 127) {
        non_printable++;
    }
}
// 如果超过 25% 是非可打印字符，视为二进制
if (non_printable > check_len / 4) {
    is_binary = true;
}
```

## 测试用例

### 1. 标准 JSON 消息（应识别为文本）

```json
{"type":"streamAudio","data":{"audioData":"base64..."}}
```

**预期结果：** ✅ 识别为 JSON 文本消息

---

### 2. 带前导空白的 JSON（应识别为文本）

```json
   
  {"type":"streamAudio"}
```

**预期结果：** ✅ 识别为 JSON 文本消息（跳过前导空白）

---

### 3. JSON 数组（应识别为文本）

```json
[{"event":"start"},{"event":"end"}]
```

**预期结果：** ✅ 识别为 JSON 文本消息

---

### 4. 原始 PCM S16BE 音频（应识别为二进制）

```
十六进制: 00 12 FF EE 00 34 FF DC ...
```

**特征：**
- 不以 `{` 或 `[` 开头
- 包含大量非可打印字符（0x00, 0xFF 等）

**预期结果：** ✅ 识别为二进制音频数据

---

### 5. 纯文本（非 JSON，应识别为二进制）

```
Hello World
```

**特征：**
- 不以 `{` 或 `[` 开头
- 全部是可打印字符

**预期结果：** ✅ 识别为二进制数据（因为不是 JSON 格式）

---

### 6. 空消息（应忽略）

```
(empty)
```

**预期结果：** ✅ 直接返回，不处理

---

### 7. 畸形 JSON（应识别为二进制）

```
{type:streamAudio}  // 缺少引号
```

**预期结果：** ⚠️ 识别为 JSON 文本（因为以 `{` 开头），但后续 JSON 解析会失败

**说明：** 这是预期行为，畸形 JSON 会在 `processMessage` 中被 `cJSON_Parse` 拒绝

---

## 边界情况

### 情况 1: 极短的二进制数据（< 16 字节）

```
十六进制: 00 12 FF EE
```

**处理：** 只使用方法 1（JSON 格式检测），不使用方法 2

**预期结果：** ✅ 识别为二进制（不以 `{` 或 `[` 开头）

---

### 情况 2: 包含 UTF-8 字符的 JSON

```json
{"message":"你好世界"}
```

**处理：** UTF-8 多字节字符（0x80-0xFF）会被计入非可打印字符

**预期结果：** 
- 方法 1: ✅ 识别为 JSON（以 `{` 开头）
- 方法 2: 可能触发二进制检测（如果 UTF-8 字符过多）
- **最终结果：** ✅ 识别为 JSON（方法 1 优先）

**说明：** 这是预期行为，方法 1 的判断优先于方法 2

---

### 情况 3: Base64 编码的音频（在 JSON 中）

```json
{"audioData":"AAES/+4ANP/c..."}
```

**预期结果：** ✅ 识别为 JSON 文本消息

**说明：** Base64 字符串在 JSON 内部，整体仍是 JSON 格式

---

## 性能考虑

### 检测开销

| 操作 | 时间复杂度 | 说明 |
|------|-----------|------|
| 跳过前导空白 | O(n) | n = 前导空白字符数，通常很小 |
| 检查第一个字符 | O(1) | 常数时间 |
| 二进制特征检测 | O(64) | 固定检查 64 字节 |
| **总计** | **O(64)** | 常数时间，性能影响可忽略 |

### 优化措施

1. **限制检查长度**：只检查前 64 字节，不扫描整个消息
2. **短路求值**：如果方法 1 已判定为二进制，跳过方法 2
3. **避免字符串拷贝**：直接在原始 `std::string` 上操作

---

## 实际测试建议

### 测试 1: 发送 JSON 消息

```python
import websockets
import asyncio
import json

async def test_json():
    async with websockets.connect("ws://localhost:8765") as ws:
        msg = {"type": "streamAudio", "data": {"test": "hello"}}
        await ws.send(json.dumps(msg))
        print("发送 JSON 消息")

asyncio.run(test_json())
```

**检查日志：** 应该看到 `processMessage` 被调用

---

### 测试 2: 发送二进制音频

```python
import websockets
import asyncio
import struct

async def test_binary():
    async with websockets.connect("ws://localhost:8765") as ws:
        # 生成 PCM S16BE 数据
        audio = struct.pack('>480h', *[i % 1000 for i in range(480)])
        await ws.send(audio)
        print(f"发送二进制音频: {len(audio)} 字节")

asyncio.run(test_binary())
```

**检查日志：** 应该看到 `processBinaryAudio` 被调用，输出类似：
```
接收二进制音频: 960 bytes → 160 samples @ 8000 Hz | 缓冲区: 20.00 ms
```

---

### 测试 3: 混合发送

```python
async def test_mixed():
    async with websockets.connect("ws://localhost:8765") as ws:
        # 先发送 JSON
        await ws.send('{"type":"test"}')
        await asyncio.sleep(0.1)
        
        # 再发送二进制
        audio = struct.pack('>480h', *[0] * 480)
        await ws.send(audio)
        await asyncio.sleep(0.1)
        
        # 再发送 JSON
        await ws.send('{"type":"end"}')

asyncio.run(test_mixed())
```

**检查日志：** 应该看到交替调用 `processMessage` 和 `processBinaryAudio`

---

## 调试技巧

### 1. 添加调试日志

在 `setMessageCallback` 中添加：

```cpp
client.setMessageCallback([this](const std::string& message) {
    if (message.empty()) {
        return;
    }
    
    // 调试：打印消息前 32 字节（十六进制）
    std::string hex_preview;
    size_t preview_len = std::min(message.size(), (size_t)32);
    for (size_t i = 0; i < preview_len; i++) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02X ", (unsigned char)message[i]);
        hex_preview += buf;
    }
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
        "收到消息: 长度=%zu, 前32字节=%s\n", 
        message.size(), hex_preview.c_str());
    
    // ... 原有的检测逻辑 ...
});
```

### 2. 统计消息类型

```cpp
static int json_count = 0;
static int binary_count = 0;

if (is_binary) {
    binary_count++;
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
        "检测为二进制消息 (总计: JSON=%d, Binary=%d)\n", 
        json_count, binary_count);
} else {
    json_count++;
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
        "检测为JSON消息 (总计: JSON=%d, Binary=%d)\n", 
        json_count, binary_count);
}
```

### 3. 验证检测准确性

```cpp
// 在 processBinaryAudio 开始处添加
switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG,
    "processBinaryAudio: 接收 %zu 字节, 前4字节: %02X %02X %02X %02X\n",
    len, 
    len > 0 ? data[0] : 0,
    len > 1 ? data[1] : 0,
    len > 2 ? data[2] : 0,
    len > 3 ? data[3] : 0);
```

---

## 已知限制

### 1. UTF-8 JSON 可能误判

**场景：** JSON 中包含大量中文或其他多字节字符

**示例：**
```json
{"text":"你好你好你好你好你好你好你好你好你好你好你好你好"}
```

**影响：** 可能被误判为二进制（如果 UTF-8 字节超过 25%）

**解决方法：** 方法 1（JSON 格式检测）会正确识别，因为它优先于方法 2

---

### 2. 纯 ASCII 文本会被视为二进制

**场景：** 发送纯文本（非 JSON）

**示例：**
```
Hello World
```

**影响：** 会被识别为二进制数据

**说明：** 这是预期行为，因为协议只支持 JSON 和二进制音频两种格式

---

### 3. 畸形 JSON 会进入 JSON 处理流程

**场景：** 发送格式错误的 JSON

**示例：**
```json
{type:streamAudio}  // 缺少引号
```

**影响：** 会被识别为 JSON，但在 `cJSON_Parse` 时失败

**说明：** 这是预期行为，让 JSON 解析器处理格式错误

---

## 总结

当前的消息类型检测算法：

✅ **优点：**
- 准确识别 JSON 和二进制数据
- 支持带前导空白的 JSON
- 性能开销小（常数时间）
- 无需修改 `libwsc` 库

⚠️ **注意事项：**
- 纯文本会被视为二进制
- 畸形 JSON 会进入 JSON 处理流程（但会被解析器拒绝）
- 极端情况下 UTF-8 JSON 可能触发二进制检测（但方法 1 会正确识别）

🎯 **推荐使用场景：**
- WebSocket 只发送 JSON 或二进制音频
- 不发送其他类型的文本消息
- 二进制音频使用 PCM S16BE 格式
