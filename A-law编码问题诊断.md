# A-law 编码杂音问题诊断

## 问题描述

转换出来的 G.711 A-law WAV 文件用 Int8 PCM @ 8kHz 播放有杂音。

## 根本原因

**重要**: G.711 A-law 不是简单的 Int8 PCM！

- **A-law**: 对数压缩编码，需要专门的解码器
- **Int8 PCM**: 线性 8-bit PCM，直接表示音频样本

如果用 Int8 PCM 播放器播放 A-law 文件，会产生严重失真和杂音，因为播放器没有进行 A-law 解码。

## 正确的播放方式

### 方法 1: 使用支持 A-law 的播放器

```bash
# 使用 aplay（ALSA）- 自动识别格式
aplay your_file.wav

# 使用 ffplay - 自动识别格式
ffplay your_file.wav

# 使用 SoX play - 自动识别格式
play your_file.wav
```

这些播放器会读取 WAV 文件头，识别出是 A-law 格式，并自动解码。

### 方法 2: 转换为 PCM 后播放

如果播放器不支持 A-law，先转换为 PCM：

```bash
# 使用 FFmpeg 转换为 16-bit PCM
ffmpeg -i input_alaw.wav -acodec pcm_s16le output_pcm.wav

# 使用 SoX 转换
sox input_alaw.wav -e signed-integer -b 16 output_pcm.wav
```

## 验证编码正确性

### 步骤 1: 编译验证程序

```bash
g++ -o verify_alaw verify_alaw.cpp -std=c++11 -lm
```

### 步骤 2: 运行验证

```bash
./verify_alaw
```

**预期输出**:
```
========================================
G.711 A-law 编码验证程序
========================================

=== 标准值测试 ===
   输入 PCM    A-law (hex)        预期值     匹配  描述
----------------------------------------------------------------------
           0           0xd5           0xd5         ✓  静音（0）
           8           0xd4           0xd4         ✓  最小正值
          -8           0x54           0x54         ✓  最小负值
        1000           0xe5           0xe5         ✓  中等正值
       -1000           0x65           0x65         ✓  中等负值
       10000           0xf9           0xf9         ✓  大正值
      -10000           0x79           0x79         ✓  大负值
       32767           0xff           0xff         ✓  最大正值
      -32768           0x7f           0x7f         ✓  最大负值（饱和）

通过: 9/9

=== 对称性测试 ===
所有测试应该显示 ✓

=== 正弦波测试 ===
最大误差和平均误差应该在合理范围内
```

如果所有测试都通过（✓），说明编码实现正确。

## 编码算法修正

已更新为标准的 ITU-T G.711 实现，主要改进：

1. **使用段查找表**: `seg_aend[8]` 定义了 8 个段的边界
2. **正确的段查找**: 使用 `search()` 函数查找段号
3. **正确的量化**: 
   - 段 0-1: `(pcm_val >> 1) & 0x0F`
   - 段 2-7: `(pcm_val >> seg) & 0x0F`

## 测试生成的文件

### 检查文件格式

```bash
# 查看文件信息
file your_file.wav
# 应显示: RIFF (little-endian) data, WAVE audio, ITU G.711 A-law, mono 8000 Hz

# 使用 soxi 查看详细信息
soxi your_file.wav
# 应显示:
# Encoding    : A-law
# Channels    : 1
# Sample Rate : 8000
```

### 使用 FFmpeg 验证

```bash
# 查看详细信息
ffprobe your_file.wav 2>&1 | grep Audio
# 应显示: Audio: pcm_alaw, 8000 Hz, 1 channels, s16, 64 kb/s
```

### 对比标准实现

使用 FFmpeg 作为参考，对比编码结果：

```bash
# 准备测试 PCM 文件（16-bit, 8000Hz, mono）
ffmpeg -f lavfi -i "sine=frequency=440:duration=1" \
       -ar 8000 -ac 1 -f s16le test.pcm

# 使用 FFmpeg 转换为 A-law
ffmpeg -f s16le -ar 8000 -ac 1 -i test.pcm \
       -acodec pcm_alaw reference.wav

# 使用你的转换工具
./convert_to_alaw test_pcm.wav your_output.wav

# 对比音频数据（跳过 WAV 头）
cmp -i 44:44 reference.wav your_output.wav
# 如果没有输出，说明完全一致
```

## 常见问题

### Q1: 为什么 SoX 显示 "13-bit"？

**A**: 这是正常的！
- G.711 A-law 内部使用 13-bit 对数表示
- 存储格式是 8-bit
- 这是标准行为，不影响播放

### Q2: 如何确认是编码问题还是播放器问题？

**A**: 使用多个播放器测试：

```bash
# 测试 1: aplay
aplay your_file.wav

# 测试 2: ffplay
ffplay your_file.wav

# 测试 3: 转换后播放
ffmpeg -i your_file.wav -f s16le - | aplay -f S16_LE -r 8000 -c 1
```

如果所有播放器都有杂音，是编码问题。如果只有某些播放器有问题，是播放器不支持 A-law。

### Q3: 如何生成测试音频？

```bash
# 生成 440Hz 正弦波，16-bit PCM, 8000Hz
ffmpeg -f lavfi -i "sine=frequency=440:duration=2" \
       -ar 8000 -ac 1 -acodec pcm_s16le test_pcm.wav

# 转换为 A-law
./convert_to_alaw test_pcm.wav test_alaw.wav

# 播放测试
aplay test_alaw.wav
```

### Q4: 如何可视化音频波形？

```bash
# 使用 SoX 生成波形图
sox your_file.wav -n spectrogram -o spectrogram.png

# 或使用 Audacity（图形界面）
audacity your_file.wav
```

## A-law vs PCM 对比

| 特性 | A-law | 16-bit PCM | 8-bit PCM |
|------|-------|------------|-----------|
| 位深度 | 8-bit | 16-bit | 8-bit |
| 编码方式 | 对数压缩 | 线性 | 线性 |
| 动态范围 | ~72 dB | ~96 dB | ~48 dB |
| 文件大小 | 小 | 大 | 小 |
| 需要解码 | 是 | 否 | 否 |
| 适用场景 | 语音通话 | 音乐 | 低质量音频 |

## 解码示例

如果需要在代码中解码 A-law：

```cpp
int16_t alaw_to_linear(uint8_t aval) {
    int16_t t;
    int16_t seg;

    aval ^= 0x55;
    
    t = (aval & 0x0F) << 4;
    seg = (aval & 0x70) >> 4;
    
    switch (seg) {
        case 0:
            t += 8;
            break;
        case 1:
            t += 0x108;
            break;
        default:
            t += 0x108;
            t <<= seg - 1;
    }
    
    return (aval & 0x80) ? t : -t;
}
```

## 总结

1. **不要用 Int8 PCM 播放器播放 A-law 文件** - 这会产生杂音
2. **使用支持 A-law 的播放器** - aplay, ffplay, SoX play
3. **运行验证程序** - 确认编码实现正确
4. **对比 FFmpeg 输出** - 验证编码结果
5. **如果仍有问题** - 检查输入 PCM 数据是否正确

编码算法已更新为标准 ITU-T G.711 实现，应该能生成正确的 A-law 文件。
