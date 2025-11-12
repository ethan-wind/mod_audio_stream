# EVENT_PLAY 事件缺少 file 字段排查指南

## 问题描述

收到了 `mod_audio_stream::play` 事件，但是事件 Body 中没有 `file` 字段。

---

## 可能的原因

根据代码逻辑，`file` 字段缺失可能由以下原因导致：

### 1. ❌ audioDataType 字段缺失或为 NULL

**条件检查**：
```cpp
const char* jsAudioDataType = cJSON_GetObjectCstr(jsonData, "audioDataType");
if (jsAudioDataType && 0 == strcmp(jsAudioDataType, "raw")) {
    // ...
}
```

**如果 `audioDataType` 为 NULL**，所有类型判断都会失败，导致 `fileType` 为空。

**WebSocket 服务器应发送**：
```json
{
  "type": "streamAudio",
  "data": {
    "audioDataType": "raw",  // ← 必须存在
    "sampleRate": 8000,
    "audioData": "base64..."
  }
}
```

---

### 2. ❌ audioDataType 不是支持的类型

**支持的类型**：
- `"raw"` - 需要配合 `sampleRate` 字段
- `"wav"`
- `"mp3"`
- `"ogg"`

**如果是其他值**（如 `"flac"`, `"pcm"`, `"audio"` 等），会进入 else 分支：
```cpp
else {
    switch_log_printf(..., "unsupported audio type: %s\n", jsAudioDataType);
}
```

此时 `fileType` 保持为空字符串。

---

### 3. ❌ raw 类型但 sampleRate 不支持

**支持的采样率**：
- 8000 Hz → `.r8`
- 16000 Hz → `.r16`
- 24000 Hz → `.r24`
- 32000 Hz → `.r32`
- 48000 Hz → `.r48`
- 64000 Hz → `.r64`

**如果 sampleRate 是其他值**（如 11025, 22050, 44100），映射会失败：
```cpp
auto it = sampleRateMap.find(sampleRate);
fileType = (it != sampleRateMap.end()) ? it->second : "";  // 返回空字符串
```

**WebSocket 服务器应发送**：
```json
{
  "type": "streamAudio",
  "data": {
    "audioDataType": "raw",
    "sampleRate": 8000,  // ← 必须是支持的值
    "audioData": "base64..."
  }
}
```

---

### 4. ❌ audioData 字段缺失或为 NULL

**条件检查**：
```cpp
cJSON* jsonAudio = cJSON_DetachItemFromObject(jsonData, "audioData");
if(jsonAudio && jsonAudio->valuestring != nullptr && !fileType.empty()) {
    // 保存文件并创建 file 字段
}
```

**如果 `audioData` 不存在或不是字符串**，不会创建文件。

---

### 5. ❌ audioData 是空字符串

即使 `audioData` 字段存在，如果是空字符串 `""`，Base64 解码后大小为 0，但仍会创建文件。

---

## 新增的调试日志

我已经添加了详细的日志来帮助排查问题：

### 日志 1：接收到的数据
```
[INFO] (uuid) processMessage - audioDataType: raw, audioData: present
[INFO] (uuid) processMessage - audioDataType: NULL, audioData: NULL
```

### 日志 2：raw 类型的采样率
```
[INFO] (uuid) processMessage - raw audio, sampleRate: 8000, fileType: .r8
[INFO] (uuid) processMessage - raw audio, sampleRate: 44100, fileType: 
```

### 日志 3：文件类型确定结果
```
[INFO] (uuid) processMessage - fileType determined: .r8, jsonAudio: valid
[INFO] (uuid) processMessage - fileType determined: EMPTY, jsonAudio: valid
```

### 日志 4：文件保存
```
[INFO] (uuid) processMessage - saving to file: /tmp/uuid_0.tmp.r8, size: 3200 bytes
[INFO] (uuid) processMessage - file saved, jsonFile created
```

### 日志 5：跳过文件创建
```
[WARNING] (uuid) processMessage - skipped file creation: jsonAudio=NULL, fileType=EMPTY
[WARNING] (uuid) processMessage - skipped file creation: jsonAudio=valid, fileType=EMPTY
```

### 日志 6：事件触发
```
[INFO] (uuid) processMessage - firing EVENT_PLAY: {"audioDataType":"raw","sampleRate":8000,"file":"/tmp/uuid_0.tmp.r8"}
```

### 日志 7：事件未触发
```
[WARNING] (uuid) processMessage - jsonFile is NULL, EVENT_PLAY not fired
```

---

## 排查步骤

### 步骤 1：查看 FreeSWITCH 日志

```bash
# 实时查看日志
tail -f /var/log/freeswitch/freeswitch.log | grep processMessage

# 或在 fs_cli 中
fs_cli> console loglevel info
```

### 步骤 2：检查 WebSocket 发送的消息

**在 WebSocket 服务器端添加日志**：
```javascript
ws.send(JSON.stringify({
  type: 'streamAudio',
  data: {
    audioDataType: 'raw',
    sampleRate: 8000,
    audioData: base64Audio
  }
}));

console.log('Sent streamAudio:', {
  audioDataType: 'raw',
  sampleRate: 8000,
  audioDataLength: base64Audio.length
});
```

### 步骤 3：根据日志判断问题

#### 场景 A：audioDataType 为 NULL
```
[INFO] (uuid) processMessage - audioDataType: NULL, audioData: present
[ERROR] (uuid) processMessage - unsupported or missing audio type: NULL
[INFO] (uuid) processMessage - fileType determined: EMPTY, jsonAudio: valid
[WARNING] (uuid) processMessage - skipped file creation: jsonAudio=valid, fileType=EMPTY
[WARNING] (uuid) processMessage - jsonFile is NULL, EVENT_PLAY not fired
```

**解决方案**：WebSocket 服务器添加 `audioDataType` 字段。

---

#### 场景 B：不支持的 audioDataType
```
[INFO] (uuid) processMessage - audioDataType: flac, audioData: present
[ERROR] (uuid) processMessage - unsupported or missing audio type: flac
[INFO] (uuid) processMessage - fileType determined: EMPTY, jsonAudio: valid
[WARNING] (uuid) processMessage - skipped file creation: jsonAudio=valid, fileType=EMPTY
```

**解决方案**：使用支持的类型（raw/wav/mp3/ogg）。

---

#### 场景 C：不支持的 sampleRate
```
[INFO] (uuid) processMessage - audioDataType: raw, audioData: present
[INFO] (uuid) processMessage - raw audio, sampleRate: 44100, fileType: 
[INFO] (uuid) processMessage - fileType determined: EMPTY, jsonAudio: valid
[WARNING] (uuid) processMessage - skipped file creation: jsonAudio=valid, fileType=EMPTY
```

**解决方案**：使用支持的采样率（8000/16000/24000/32000/48000/64000）或转换为 wav 格式。

---

#### 场景 D：audioData 缺失
```
[INFO] (uuid) processMessage - audioDataType: raw, audioData: NULL
[INFO] (uuid) processMessage - raw audio, sampleRate: 8000, fileType: .r8
[INFO] (uuid) processMessage - fileType determined: .r8, jsonAudio: NULL
[WARNING] (uuid) processMessage - skipped file creation: jsonAudio=NULL, fileType=.r8
```

**解决方案**：WebSocket 服务器添加 `audioData` 字段。

---

#### 场景 E：正常情况
```
[INFO] (uuid) processMessage - audioDataType: raw, audioData: present
[INFO] (uuid) processMessage - raw audio, sampleRate: 8000, fileType: .r8
[INFO] (uuid) processMessage - fileType determined: .r8, jsonAudio: valid
[INFO] (uuid) processMessage - saving to file: /tmp/uuid_0.tmp.r8, size: 3200 bytes
[INFO] (uuid) processMessage - file saved, jsonFile created
[INFO] (uuid) processMessage - firing EVENT_PLAY: {"audioDataType":"raw","sampleRate":8000,"file":"/tmp/uuid_0.tmp.r8"}
```

---

## 常见错误示例

### 错误 1：缺少 audioDataType
```json
// ❌ 错误
{
  "type": "streamAudio",
  "data": {
    "sampleRate": 8000,
    "audioData": "base64..."
  }
}

// ✅ 正确
{
  "type": "streamAudio",
  "data": {
    "audioDataType": "raw",
    "sampleRate": 8000,
    "audioData": "base64..."
  }
}
```

### 错误 2：使用不支持的类型
```json
// ❌ 错误
{
  "type": "streamAudio",
  "data": {
    "audioDataType": "pcm",  // 不支持
    "sampleRate": 8000,
    "audioData": "base64..."
  }
}

// ✅ 正确
{
  "type": "streamAudio",
  "data": {
    "audioDataType": "raw",  // 支持
    "sampleRate": 8000,
    "audioData": "base64..."
  }
}
```

### 错误 3：使用不支持的采样率
```json
// ❌ 错误
{
  "type": "streamAudio",
  "data": {
    "audioDataType": "raw",
    "sampleRate": 44100,  // 不支持
    "audioData": "base64..."
  }
}

// ✅ 正确（方案 1：使用支持的采样率）
{
  "type": "streamAudio",
  "data": {
    "audioDataType": "raw",
    "sampleRate": 16000,  // 支持
    "audioData": "base64..."
  }
}

// ✅ 正确（方案 2：使用 wav 格式）
{
  "type": "streamAudio",
  "data": {
    "audioDataType": "wav",  // wav 支持任意采样率
    "audioData": "base64..."  // WAV 文件的 base64
  }
}
```

### 错误 4：raw 类型缺少 sampleRate
```json
// ❌ 错误
{
  "type": "streamAudio",
  "data": {
    "audioDataType": "raw",
    "audioData": "base64..."
  }
}

// ✅ 正确
{
  "type": "streamAudio",
  "data": {
    "audioDataType": "raw",
    "sampleRate": 8000,  // raw 类型必须指定
    "audioData": "base64..."
  }
}
```

---

## 快速检查清单

在 WebSocket 服务器端发送消息前，检查：

- [ ] 消息包含 `type: "streamAudio"`
- [ ] 消息包含 `data` 对象
- [ ] `data` 中包含 `audioDataType` 字段
- [ ] `audioDataType` 是以下之一：`"raw"`, `"wav"`, `"mp3"`, `"ogg"`
- [ ] 如果是 `"raw"`，包含 `sampleRate` 字段
- [ ] 如果是 `"raw"`，`sampleRate` 是以下之一：8000, 16000, 24000, 32000, 48000, 64000
- [ ] `data` 中包含 `audioData` 字段
- [ ] `audioData` 是有效的 Base64 字符串
- [ ] `audioData` 不是空字符串

---

## 测试用例

### 测试 1：8kHz raw PCM
```json
{
  "type": "streamAudio",
  "data": {
    "audioDataType": "raw",
    "sampleRate": 8000,
    "audioData": "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8="
  }
}
```

**期望结果**：
- 文件：`/tmp/uuid_0.tmp.r8`
- 事件：`{"audioDataType":"raw","sampleRate":8000,"file":"/tmp/uuid_0.tmp.r8"}`

### 测试 2：16kHz raw PCM
```json
{
  "type": "streamAudio",
  "data": {
    "audioDataType": "raw",
    "sampleRate": 16000,
    "audioData": "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8="
  }
}
```

**期望结果**：
- 文件：`/tmp/uuid_0.tmp.r16`
- 事件：`{"audioDataType":"raw","sampleRate":16000,"file":"/tmp/uuid_0.tmp.r16"}`

### 测试 3：WAV 文件
```json
{
  "type": "streamAudio",
  "data": {
    "audioDataType": "wav",
    "audioData": "UklGRiQAAABXQVZFZm10IBAAAAABAAEAQB8AAAB9AAACABAAZGF0YQAAAAA="
  }
}
```

**期望结果**：
- 文件：`/tmp/uuid_0.tmp.wav`
- 事件：`{"audioDataType":"wav","file":"/tmp/uuid_0.tmp.wav"}`

---

## 下一步

1. **重新编译模块**：
```bash
cd build
make
sudo make install
```

2. **重启 FreeSWITCH 或重新加载模块**：
```bash
fs_cli> reload mod_audio_stream
```

3. **测试并查看日志**：
```bash
tail -f /var/log/freeswitch/freeswitch.log | grep processMessage
```

4. **根据日志输出判断问题所在**

---

## 总结

`file` 字段缺失的根本原因是 `jsonFile` 为 NULL，而 `jsonFile` 为 NULL 是因为以下条件之一不满足：

```cpp
if(jsonAudio && jsonAudio->valuestring != nullptr && !fileType.empty()) {
    // 创建文件和 jsonFile
}
```

通过新增的日志，你可以精确定位是哪个条件导致的问题。
