# 音频播放不清晰问题修复指南

## 已实施的改进

### 1. 详细调试日志 ✅

代码中已添加完整的调试日志，可以追踪音频数据在每个转换步骤的状态。

**日志示例**：
```
Step 1 完成: Float32→PCM, 2400 samples | 范围: [-0.8, 0.9] 峰值: 0.9 | 增益: 1.00x | 限幅: 0 | 前3: 0.123→4000, -0.234→-7000, 0.345→11000
Step 2 完成: 重采样 24000 Hz → 8000 Hz (2400 → 800 samples) | PCM范围: [-7000, 11000] | 前3: 4000, -7000, 11000
✓ 已入队播放: 800 samples (1600 bytes, 写入 1600 bytes) @ 8000 Hz, 1 ch | 缓冲区: 100.00 ms | 前3 PCM: 4000, -7000, 11000
Injected audio: 320/320 bytes, 160 samples @ 8000 Hz, 1 ch | Buffer left: 80.00 ms | 前3 PCM: 4000, -7000, 11000
```

### 2. 自适应增益功能 ✅

添加了可选的自动增益控制（AGC），可以自动调整音量。

**启用方法**：
```javascript
// 在 FreeSWitch 拨号计划或脚本中设置通道变量
session.setVariable("STREAM_AUTO_GAIN", "true");
```

或在拨号计划中：
```xml
<action application="set" data="STREAM_AUTO_GAIN=true"/>
<action application="uuid_audio_stream" data="${uuid} start wss://your-server/audio mono 8000"/>
```

**工作原理**：
- 分析每个音频块的峰值
- 自动计算增益，使峰值归一化到 0.9
- 增益范围限制在 [0.1x, 20.0x]
- 避免过度放大导致失真

---

## 使用步骤

### 步骤 1: 编译并安装模块

```bash
# 编译
make

# 安装
make install

# 重启 FreeSWitch
fs_cli -x "reload mod_audio_stream"
```

### 步骤 2: 启用调试日志

```bash
# 在 FreeSWitch CLI 中
fs_cli
> console loglevel info
> log /var/log/freeswitch/freeswitch.log
```

### 步骤 3: 测试播放

**方案 A: 不启用自动增益（默认）**
```javascript
session.execute("uuid_audio_stream", `${uuid} start wss://your-server/audio mono 8000`);
```

**方案 B: 启用自动增益（推荐）**
```javascript
session.setVariable("STREAM_AUTO_GAIN", "true");
session.execute("uuid_audio_stream", `${uuid} start wss://your-server/audio mono 8000`);
```

### 步骤 4: 查看日志并诊断

```bash
# 实时查看日志
tail -f /var/log/freeswitch/freeswitch.log | grep "Step 1\|Step 2\|已入队\|Injected"
```

**查看关键指标**：
1. **Float 峰值**: 应该在 0.1 ~ 1.0 之间
2. **增益倍数**: 如果 > 5x，说明原始音量很小
3. **PCM 范围**: 应该在 [-30000, 30000] 左右
4. **限幅次数**: 应该为 0 或很少

---

## 问题诊断

### 问题 1: 音量太小

**症状**：
```
Float 峰值: 0.01 | 增益: 1.00x | PCM范围: [-327, 327]
```

**解决方案**：启用自动增益
```javascript
session.setVariable("STREAM_AUTO_GAIN", "true");
```

**预期结果**：
```
Float 峰值: 0.01 | 增益: 20.00x (自动) | PCM范围: [-6540, 6540]
```

### 问题 2: 音频失真

**症状**：
```
限幅: 500 | PCM范围: [-32767, 32767]
```

**原因**：音频过载或增益过高

**解决方案 A**：禁用自动增益
```javascript
// 不设置 STREAM_AUTO_GAIN，使用默认 1.0x 增益
```

**解决方案 B**：手动设置增益（需要修改代码）
```cpp
// 在代码中固定增益
float gain = 0.5f;  // 降低到 0.5x
```

### 问题 3: 播放速度不对

**症状**：音频内容正确但速度异常

**检查日志**：
```
Step 2 完成: 重采样 24000 Hz → 8000 Hz (2400 → 800 samples)
```

**验证比例**：
- 24000 → 8000: 应该是 3:1 (2400 → 800) ✓
- 24000 → 16000: 应该是 3:2 (2400 → 1600) ✓

**如果比例不对**：检查 `sampleRate` 变量是否正确

### 问题 4: 音频断续

**症状**：
```
buffer empty, passthrough original audio
```

**原因**：数据发送速度不够快

**解决方案**：增加缓冲区大小
```cpp
// 在 stream_data_init 函数中修改
const size_t play_buflen = desiredSampling * channels * sizeof(int16_t) * 20; // 20秒缓冲
```

### 问题 5: 完全无声

**症状**：
```
前3: 0.000→0, 0.000→0, 0.000→0
```

**可能原因**：
1. WebSocket 发送的是静音数据
2. 数据格式不是 Float32
3. 字节序错误

**诊断步骤**：
```cpp
// 添加原始数据十六进制输出
const uint8_t* raw = (const uint8_t*)rawAudio.data();
switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
    "Raw hex: %02X %02X %02X %02X %02X %02X %02X %02X\n",
    raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], raw[6], raw[7]);
```

---

## 性能调优

### 调整增益范围

如果自动增益的范围 [0.1x, 20.0x] 不合适，可以修改：

```cpp
// 在代码中找到这段
if (gain < 0.1f) gain = 0.1f;
if (gain > 20.0f) gain = 20.0f;

// 修改为
if (gain < 0.5f) gain = 0.5f;   // 最小增益 0.5x
if (gain > 10.0f) gain = 10.0f;  // 最大增益 10.0x
```

### 调整目标峰值

如果 0.9 的目标峰值导致失真，可以降低：

```cpp
// 在代码中找到这段
gain = 0.9f / max_abs;

// 修改为
gain = 0.7f / max_abs;  // 降低到 0.7，留更多余量
```

### 禁用自动增益

如果不需要自动增益，可以完全移除相关代码，或者简单地不设置通道变量。

---

## 测试用例

### 测试 1: 正常音量音频
```
输入: Float 峰值 0.8
预期: 增益 1.0x (不启用自动增益) 或 1.125x (启用自动增益)
结果: PCM 范围约 [-26000, 26000]
```

### 测试 2: 低音量音频
```
输入: Float 峰值 0.05
预期: 增益 1.0x (不启用) 或 18.0x (启用)
结果: PCM 范围约 [-1600, 1600] (不启用) 或 [-29000, 29000] (启用)
```

### 测试 3: 过载音频
```
输入: Float 峰值 2.0
预期: 增益 1.0x (不启用) 或 0.45x (启用)
结果: 限幅次数应该很少
```

---

## 常见日志解读

### 正常日志
```
Step 1 完成: Float32→PCM, 2400 samples | 范围: [-0.8, 0.9] 峰值: 0.9 | 增益: 1.00x | 限幅: 0
```
✅ 音频正常，无需调整

### 音量太小
```
Step 1 完成: Float32→PCM, 2400 samples | 范围: [-0.01, 0.01] 峰值: 0.01 | 增益: 1.00x | 限幅: 0
```
⚠️ 建议启用自动增益

### 自动增益生效
```
Step 1 完成: Float32→PCM, 2400 samples | 范围: [-0.05, 0.05] 峰值: 0.05 | 增益: 18.00x (自动) | 限幅: 0
```
✅ 自动增益正常工作

### 音频过载
```
Step 1 完成: Float32→PCM, 2400 samples | 范围: [-1.5, 2.0] 峰值: 2.0 | 增益: 1.00x | 限幅: 500
```
⚠️ 原始音频过载，建议启用自动增益或降低输入音量

---

## 总结

1. **默认行为**: 不修改音量，直接转换（增益 1.0x）
2. **推荐配置**: 启用自动增益 (`STREAM_AUTO_GAIN=true`)
3. **调试方法**: 查看日志中的峰值、增益、PCM 范围
4. **性能影响**: 自动增益只增加一次遍历，性能影响很小

如果问题仍然存在，请提供完整的日志输出以便进一步诊断。
