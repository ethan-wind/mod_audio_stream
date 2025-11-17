# 音频播放不清晰问题诊断指南

## 已添加的调试日志

代码中已添加详细的调试日志，运行后请检查以下信息：

### 1. Float32 转换日志
```
Step 1 完成: Float32→PCM, XXX samples | 范围: [min, max] | 限幅: X samples | 前3样本: X, X, X
```

**检查项**：
- ✅ 样本范围应该在 [-1.0, 1.0] 之间
- ⚠️ 如果范围是 [-0.001, 0.001]，说明音量太小
- ⚠️ 如果限幅样本数很多，说明音频过载
- ⚠️ 如果前3样本都是 0.0，说明可能是静音数据

### 2. 重采样日志
```
Step 2 完成: 重采样 24000 Hz → 8000 Hz (XXX → XXX samples) | PCM范围: [min, max] | 前3: X, X, X
```

**检查项**：
- ✅ 样本数比例应该是 3:1 (24000→8000) 或 3:2 (24000→16000)
- ✅ PCM 范围应该在 [-32767, 32767] 之间
- ⚠️ 如果 PCM 范围很小 (如 [-100, 100])，说明音量太小
- ⚠️ 如果前3样本都是 0，说明可能有问题

### 3. 缓冲区写入日志
```
✓ 已入队播放: XXX samples (XXX bytes, 写入 XXX bytes) @ 8000 Hz, 1 ch | 缓冲区: XX.XX ms | 前3 PCM: X, X, X
```

**检查项**：
- ✅ 写入字节数应该等于请求字节数
- ✅ 前3个 PCM 值应该与重采样后的值一致
- ⚠️ 如果写入字节数 < 请求字节数，说明缓冲区可能有问题

### 4. 播放注入日志
```
Injected audio: XXX/XXX bytes, XXX samples @ 8000 Hz, 1 ch | Buffer left: XX.XX ms | 前3 PCM: X, X, X
```

**检查项**：
- ✅ 采样率应该与通话采样率一致 (8000 或 16000 Hz)
- ✅ 前3个 PCM 值应该与缓冲区中的值一致
- ⚠️ 如果值不一致，说明数据在传输过程中被修改

---

## 常见问题和解决方案

### 问题 1: 音量太小（听不清）

**症状**：
- Float 范围: [-0.01, 0.01] (太小)
- PCM 范围: [-327, 327] (太小)

**原因**：WebSocket 发送的音频音量太小

**解决方案**：增加音量增益
```cpp
// 在 Float → PCM 转换时添加增益
float gain = 10.0f;  // 增益倍数，根据实际情况调整
pcm16bit[i] = static_cast<int16_t>(sample * 32767.0f * gain);

// 注意：增益后需要限幅
if (pcm16bit[i] > 32767) pcm16bit[i] = 32767;
if (pcm16bit[i] < -32767) pcm16bit[i] = -32767;
```

### 问题 2: 音频失真（有噪音）

**症状**：
- 限幅样本数很多
- PCM 值经常达到 ±32767

**原因**：音频过载或转换错误

**解决方案 A**：降低增益
```cpp
float gain = 0.5f;  // 降低增益
```

**解决方案 B**：检查 Float 数据是否正确
```cpp
// 打印原始 Float 数据的十六进制
for (int i = 0; i < 10; i++) {
    uint32_t hex = *reinterpret_cast<const uint32_t*>(&float_data[i]);
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
        "Sample %d: float=%.6f, hex=0x%08X\n", i, float_data[i], hex);
}
```

### 问题 3: 播放速度不对（快或慢）

**症状**：
- 音频内容正确但速度异常

**原因**：采样率不匹配

**解决方案**：确认采样率
```cpp
// 检查 sampleRate 变量是否正确
switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
    "Input rate: %d, Target rate: %d, Ratio: %.2f\n",
    sampleRate, target_rate, (float)sampleRate / target_rate);
```

### 问题 4: 音频断续（卡顿）

**症状**：
- 缓冲区经常为空
- 日志显示 "buffer empty, passthrough"

**原因**：数据发送速度不够快

**解决方案**：增加缓冲区大小
```cpp
// 在 stream_data_init 中
const size_t play_buflen = desiredSampling * channels * sizeof(int16_t) * 20; // 从10秒增加到20秒
```

### 问题 5: 字节序问题

**症状**：
- 播放出来是噪音
- PCM 值看起来不正常

**原因**：字节序错误（大端序 vs 小端序）

**解决方案**：检查字节序
```cpp
// 检查系统字节序
uint16_t test = 0x1234;
uint8_t* bytes = (uint8_t*)&test;
if (bytes[0] == 0x12) {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "System is BIG ENDIAN\n");
} else {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "System is LITTLE ENDIAN\n");
}
```

### 问题 6: 数据格式错误

**症状**：
- 前3样本都是 0
- 或者值看起来不合理

**原因**：WebSocket 发送的数据格式不是 Float32

**解决方案**：验证数据格式
```cpp
// 检查前几个字节
const uint8_t* raw_bytes = (const uint8_t*)rawAudio.data();
switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
    "First 16 bytes (hex): %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
    raw_bytes[0], raw_bytes[1], raw_bytes[2], raw_bytes[3],
    raw_bytes[4], raw_bytes[5], raw_bytes[6], raw_bytes[7],
    raw_bytes[8], raw_bytes[9], raw_bytes[10], raw_bytes[11],
    raw_bytes[12], raw_bytes[13], raw_bytes[14], raw_bytes[15]);
```

---

## 快速诊断步骤

### 步骤 1: 检查原始数据
运行代码并查看日志中的 Float 范围和前3样本：

```bash
# 查看 Float 转换日志
grep "Step 1 完成" /var/log/freeswitch/freeswitch.log | tail -20
```

**正常值示例**：
```
范围: [-0.8, 0.9] | 前3样本: 0.123456, -0.234567, 0.345678
```

**异常值示例**：
```
范围: [-0.001, 0.001] | 前3样本: 0.000123, -0.000234, 0.000345  ← 音量太小
范围: [-100.0, 100.0] | 前3样本: 50.123456, -60.234567, 70.345678  ← 不是归一化的 Float
```

### 步骤 2: 检查重采样
```bash
# 查看重采样日志
grep "Step 2 完成" /var/log/freeswitch/freeswitch.log | tail -20
```

**正常值示例**：
```
PCM范围: [-20000, 25000] | 前3: 4000, -7000, 11000
```

**异常值示例**：
```
PCM范围: [-100, 100] | 前3: 40, -70, 110  ← 音量太小
PCM范围: [-32767, 32767] | 前3: 32767, -32767, 32767  ← 过载
```

### 步骤 3: 检查播放注入
```bash
# 查看播放日志
grep "Injected audio" /var/log/freeswitch/freeswitch.log | tail -20
```

**正常值示例**：
```
320/320 bytes, 160 samples @ 8000 Hz, 1 ch | 前3 PCM: 4000, -7000, 11000
```

### 步骤 4: 对比数值
确保以下数值在整个流程中保持一致：
1. Float 转换后的前3样本
2. 重采样后的前3样本（值会变化，但应该成比例）
3. 缓冲区写入的前3样本
4. 播放注入的前3样本

---

## 推荐的修复方案

基于最常见的问题（音量太小），建议添加自适应增益：

```cpp
// 在 Float → PCM 转换时
float max_abs = 0.0f;
for (size_t i = 0; i < input_samples; i++) {
    float abs_val = std::abs(float_data[i]);
    if (abs_val > max_abs) max_abs = abs_val;
}

// 计算增益（目标峰值为 0.8）
float gain = 1.0f;
if (max_abs > 0.001f) {  // 避免除以0
    gain = 0.8f / max_abs;
    if (gain > 10.0f) gain = 10.0f;  // 限制最大增益
}

switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
    "Auto gain: %.2f (peak: %.6f)\n", gain, max_abs);

// 应用增益
for (size_t i = 0; i < input_samples; i++) {
    float sample = float_data[i] * gain;
    if (sample > 1.0f) sample = 1.0f;
    if (sample < -1.0f) sample = -1.0f;
    pcm16bit[i] = static_cast<int16_t>(sample * 32767.0f);
}
```

---

## 下一步

1. **运行代码**，查看新增的调试日志
2. **记录日志输出**，特别是：
   - Float 范围
   - PCM 范围
   - 前3样本的值
3. **根据日志判断问题类型**
4. **应用相应的解决方案**

如果需要进一步帮助，请提供完整的日志输出。
