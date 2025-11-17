# 音频失真问题解决方案

## 失真的常见原因

1. **音频过载**: 原始音频峰值超过 ±1.0
2. **增益过高**: 自动增益或手动增益设置过大
3. **硬限幅**: 使用硬截断导致波形失真
4. **采样率不匹配**: 重采样质量问题
5. **数据格式错误**: Float32 数据解析错误

---

## 解决方案

### 方案 1: 降低目标峰值（推荐）

**问题**: 自动增益将峰值归一化到 0.9，可能导致轻微失真

**解决方案**: 降低目标峰值到 0.7 或 0.6

```javascript
// 在 FreeSWitch 中设置
session.setVariable("STREAM_AUTO_GAIN", "true");
session.setVariable("STREAM_TARGET_PEAK", "0.7");  // 降低到 0.7
```

**效果**:
- 0.9 → 0.8: 轻微降低，减少失真风险
- 0.9 → 0.7: 中等降低，明显减少失真
- 0.9 → 0.6: 大幅降低，几乎无失真但音量较小

---

### 方案 2: 启用软限幅（推荐）

**问题**: 硬限幅（直接截断）会产生明显的失真

**解决方案**: 使用软限幅（tanh 函数）平滑过渡

```javascript
session.setVariable("STREAM_SOFT_CLIP", "true");
```

**原理**:
```
硬限幅: 1.5 → 1.0 (突变，产生失真)
软限幅: 1.5 → 0.96 (平滑，减少失真)
```

**对比**:
```
输入值    硬限幅    软限幅 (tanh)
0.5       0.5       0.5
0.9       0.9       0.9
1.0       1.0       0.995
1.2       1.0       0.998  ← 平滑过渡
1.5       1.0       0.999
2.0       1.0       1.000
```

---

### 方案 3: 手动设置增益

**问题**: 自动增益可能过高或过低

**解决方案**: 手动指定增益倍数

```javascript
// 不启用自动增益，手动设置
session.setVariable("STREAM_GAIN", "0.5");  // 0.5x 增益（降低音量）
```

**常用值**:
- `0.3` - 大幅降低音量，适合过载音频
- `0.5` - 中等降低，适合轻微过载
- `1.0` - 不调整（默认）
- `2.0` - 2倍增益，适合音量太小
- `5.0` - 5倍增益，适合极小音量

**注意**: 手动增益会禁用自动增益

---

### 方案 4: 组合使用（最佳实践）

**场景 A: 音量小且容易失真**
```javascript
session.setVariable("STREAM_AUTO_GAIN", "true");
session.setVariable("STREAM_TARGET_PEAK", "0.7");  // 保守的目标峰值
session.setVariable("STREAM_SOFT_CLIP", "true");   // 软限幅
```

**场景 B: 音量正常但有失真**
```javascript
session.setVariable("STREAM_GAIN", "0.8");         // 降低 20%
session.setVariable("STREAM_SOFT_CLIP", "true");   // 软限幅
```

**场景 C: 音量过大**
```javascript
session.setVariable("STREAM_GAIN", "0.5");         // 降低 50%
// 不需要软限幅
```

**场景 D: 音量太小**
```javascript
session.setVariable("STREAM_AUTO_GAIN", "true");
session.setVariable("STREAM_TARGET_PEAK", "0.8");  // 较高的目标峰值
session.setVariable("STREAM_SOFT_CLIP", "true");   // 防止偶尔的峰值失真
```

---

## 配置参数详解

### 1. STREAM_AUTO_GAIN

**类型**: Boolean (true/false)  
**默认**: false  
**作用**: 启用自动增益控制

```javascript
session.setVariable("STREAM_AUTO_GAIN", "true");
```

**工作原理**:
1. 分析每个音频块的峰值
2. 计算增益 = 目标峰值 / 实际峰值
3. 限制增益范围 [0.1x, 20.0x]

---

### 2. STREAM_TARGET_PEAK

**类型**: Float (0.0 ~ 1.0)  
**默认**: 0.8  
**作用**: 设置自动增益的目标峰值

```javascript
session.setVariable("STREAM_TARGET_PEAK", "0.7");
```

**推荐值**:
- `0.6` - 非常保守，几乎无失真，但音量较小
- `0.7` - 保守，适合大多数场景
- `0.8` - 默认值，平衡音量和失真
- `0.9` - 激进，音量大但可能失真

---

### 3. STREAM_SOFT_CLIP

**类型**: Boolean (true/false)  
**默认**: false  
**作用**: 启用软限幅，减少失真

```javascript
session.setVariable("STREAM_SOFT_CLIP", "true");
```

**效果**:
- 使用 tanh 函数平滑限幅
- 减少硬截断产生的失真
- 轻微增加 CPU 使用（可忽略）

---

### 4. STREAM_GAIN

**类型**: Float (0.0 ~ 100.0)  
**默认**: 1.0  
**作用**: 手动设置增益倍数

```javascript
session.setVariable("STREAM_GAIN", "0.5");  // 0.5x 增益
```

**注意**:
- 设置此参数会禁用自动增益
- 优先级最高
- 建议范围 [0.1, 10.0]

---

## 诊断失真问题

### 步骤 1: 查看日志

```bash
tail -f /var/log/freeswitch/freeswitch.log | grep "Step 1 完成"
```

**关键指标**:

#### 正常音频
```
峰值: 0.8 | 增益: 1.00x | 限幅: 0 (0.0%)
```
✅ 无失真

#### 轻微失真
```
峰值: 1.2 | 增益: 1.00x | 限幅: 50 (2.1%)
```
⚠️ 2.1% 样本被限幅，可能有轻微失真

#### 严重失真
```
峰值: 2.5 | 增益: 1.00x | 限幅: 500 (20.8%)
```
❌ 20.8% 样本被限幅，严重失真

---

### 步骤 2: 判断失真类型

#### 类型 A: 输入过载
```
峰值: 2.0 | 增益: 1.00x | 限幅: 300 (12.5%)
```

**原因**: WebSocket 发送的音频本身过载  
**解决**: 降低输入音量或使用软限幅

```javascript
session.setVariable("STREAM_SOFT_CLIP", "true");
// 或
session.setVariable("STREAM_GAIN", "0.5");
```

#### 类型 B: 增益过高
```
峰值: 0.1 | 增益: 9.00x (自动) | 限幅: 200 (8.3%)
```

**原因**: 自动增益将小音量放大过度  
**解决**: 降低目标峰值或使用手动增益

```javascript
session.setVariable("STREAM_TARGET_PEAK", "0.6");
// 或
session.setVariable("STREAM_GAIN", "3.0");  // 手动设置较低的增益
```

#### 类型 C: 偶尔峰值
```
峰值: 0.95 | 增益: 1.00x | 限幅: 5 (0.2%)
```

**原因**: 偶尔的峰值被硬限幅  
**解决**: 启用软限幅

```javascript
session.setVariable("STREAM_SOFT_CLIP", "true");
```

---

### 步骤 3: 应用解决方案

根据诊断结果选择方案：

| 峰值 | 限幅% | 问题 | 解决方案 |
|------|-------|------|----------|
| < 0.1 | 任意 | 音量太小 | `STREAM_AUTO_GAIN=true` + `STREAM_TARGET_PEAK=0.7` |
| 0.1-0.9 | 0% | 正常 | 无需调整 |
| 0.1-0.9 | < 1% | 偶尔峰值 | `STREAM_SOFT_CLIP=true` |
| 0.9-1.5 | 1-10% | 轻微过载 | `STREAM_SOFT_CLIP=true` |
| > 1.5 | > 10% | 严重过载 | `STREAM_GAIN=0.5` + `STREAM_SOFT_CLIP=true` |

---

## 测试用例

### 测试 1: 正常音频
```javascript
// 不做任何调整
session.execute("uuid_audio_stream", `${uuid} start wss://server/audio mono 8000`);
```

**预期日志**:
```
峰值: 0.7 | 增益: 1.00x | 限幅: 0 (0.0%)
```

---

### 测试 2: 小音量音频
```javascript
session.setVariable("STREAM_AUTO_GAIN", "true");
session.setVariable("STREAM_TARGET_PEAK", "0.7");
session.setVariable("STREAM_SOFT_CLIP", "true");
```

**预期日志**:
```
峰值: 0.05 | 增益: 14.00x (自动) (软限幅) | 限幅: 0 (0.0%)
```

---

### 测试 3: 过载音频
```javascript
session.setVariable("STREAM_GAIN", "0.5");
session.setVariable("STREAM_SOFT_CLIP", "true");
```

**预期日志**:
```
峰值: 2.0 | 增益: 0.50x (软限幅) | 限幅: 0 (0.0%)
```

---

## 性能影响

### 硬限幅（默认）
- CPU: 几乎无影响
- 延迟: 无
- 音质: 可能失真

### 软限幅
- CPU: 每样本增加 1 次 tanh 计算
- 延迟: < 0.1ms
- 音质: 明显改善

**结论**: 软限幅的性能影响可忽略，建议默认启用

---

## 常见问题

### Q1: 启用软限幅后还是失真？

**A**: 降低增益或目标峰值
```javascript
session.setVariable("STREAM_TARGET_PEAK", "0.6");  // 从 0.8 降到 0.6
```

### Q2: 音量太小怎么办？

**A**: 提高目标峰值或手动增益
```javascript
session.setVariable("STREAM_AUTO_GAIN", "true");
session.setVariable("STREAM_TARGET_PEAK", "0.9");  // 提高到 0.9
```

### Q3: 如何完全禁用增益？

**A**: 不设置任何增益相关变量，或设置手动增益为 1.0
```javascript
session.setVariable("STREAM_GAIN", "1.0");
```

### Q4: 软限幅和硬限幅可以同时用吗？

**A**: 不需要，软限幅会替代硬限幅

---

## 推荐配置

### 生产环境（保守）
```javascript
session.setVariable("STREAM_AUTO_GAIN", "true");
session.setVariable("STREAM_TARGET_PEAK", "0.7");
session.setVariable("STREAM_SOFT_CLIP", "true");
```

### 测试环境（激进）
```javascript
session.setVariable("STREAM_AUTO_GAIN", "true");
session.setVariable("STREAM_TARGET_PEAK", "0.9");
session.setVariable("STREAM_SOFT_CLIP", "true");
```

### 已知过载音频
```javascript
session.setVariable("STREAM_GAIN", "0.5");
session.setVariable("STREAM_SOFT_CLIP", "true");
```

---

## 总结

1. **默认行为**: 硬限幅，增益 1.0x
2. **推荐配置**: 自动增益 + 目标峰值 0.7 + 软限幅
3. **失真严重**: 手动降低增益 + 软限幅
4. **音量太小**: 自动增益 + 目标峰值 0.8-0.9
5. **调试方法**: 查看日志中的峰值、增益、限幅百分比

根据日志输出调整配置，直到找到最佳平衡点。
