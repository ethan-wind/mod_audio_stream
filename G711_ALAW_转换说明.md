# G.711 A-law WAV 格式转换说明

## 修改内容

已将 `audio_streamer_glue.cpp` 中的音频文件生成功能修改为输出 **G.711 A-law 编码的 WAV 格式**。

### 主要变更

1. **添加了 A-law 编码函数**
   - `linear_to_alaw()`: 将 16-bit PCM 样本转换为 8-bit A-law 编码
   - 使用标准的 G.711 A-law 压缩算法

2. **修改了 WAV 文件头**
   - `audioFormat`: 从 1 (PCM) 改为 6 (G.711 A-law)
   - `bitsPerSample`: 从 16 改为 8
   - 相应调整了 `byteRate` 和 `blockAlign`

3. **数据转换流程**
   - 接收 base64 编码的音频数据
   - 解码为原始 PCM 数据
   - 如需要，重采样到 8000 Hz
   - 将 16-bit PCM 转换为 8-bit A-law
   - 写入 G.711 A-law 格式的 WAV 文件

## G.711 A-law 格式特点

- **编码**: G.711 A-law (ITU-T G.711)
- **采样率**: 8000 Hz
- **位深度**: 8 bit (压缩后)
- **声道数**: 1 (单声道)
- **压缩比**: 2:1 (相比 16-bit PCM)
- **应用**: 主要用于电话系统，特别是欧洲和国际标准

## 独立转换工具

提供了 `convert_to_alaw.cpp` 独立转换工具，可以将任何 16-bit PCM WAV 文件转换为 G.711 A-law 格式。

### 编译转换工具

```bash
g++ -o convert_to_alaw convert_to_alaw.cpp -std=c++11
```

### 使用方法

```bash
./convert_to_alaw input.wav output_alaw.wav
```

**示例**:
```bash
# 转换单个文件
./convert_to_alaw recording.wav recording_alaw.wav

# 批量转换
for file in *.wav; do
    ./convert_to_alaw "$file" "${file%.wav}_alaw.wav"
done
```

## 重新编译模块

修改代码后需要重新编译 FreeSWITCH 模块：

```bash
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
sudo make install
```

## 验证输出

可以使用以下工具验证生成的 WAV 文件格式：

### 使用 ffprobe
```bash
ffprobe output_alaw.wav
```

应该显示:
```
Stream #0:0: Audio: pcm_alaw, 8000 Hz, 1 channels, s16, 64 kb/s
```

### 使用 file 命令
```bash
file output_alaw.wav
```

应该显示:
```
output_alaw.wav: RIFF (little-endian) data, WAVE audio, ITU G.711 A-law, mono 8000 Hz
```

### 使用 soxi (SoX)
```bash
soxi output_alaw.wav
```

应该显示:
```
Encoding    : A-law
Channels    : 1
Sample Rate : 8000
```

## 播放测试

可以使用以下工具播放 A-law WAV 文件：

```bash
# 使用 aplay (ALSA)
aplay output_alaw.wav

# 使用 ffplay
ffplay output_alaw.wav

# 使用 SoX
play output_alaw.wav
```

## 技术细节

### A-law 编码算法

G.711 A-law 使用对数压缩算法，将 16-bit 线性 PCM 压缩为 8-bit：

1. 取 PCM 值的符号
2. 右移 3 位进行预处理
3. 根据幅度确定段号 (segment)
4. 计算量化值
5. 应用 XOR 掩码

### 文件大小对比

对于相同时长的音频：
- **16-bit PCM**: 8000 Hz × 2 bytes = 16,000 bytes/秒
- **8-bit A-law**: 8000 Hz × 1 byte = 8,000 bytes/秒
- **压缩比**: 50% 文件大小

### 音质说明

G.711 A-law 是有损压缩，但对于语音通话质量足够：
- **动态范围**: 约 72 dB
- **信噪比**: 约 38 dB
- **MOS 评分**: 4.1 (接近 CD 质量的 4.5)
- **适用场景**: 电话、VoIP、语音通话

## 测试编码正确性

提供了测试程序来验证 A-law 编码实现：

```bash
# 编译测试程序
g++ -o test_alaw test_alaw_encoding.cpp -std=c++11

# 运行测试
./test_alaw
```

测试程序会显示不同 PCM 值的编码和解码结果，验证实现的正确性。

## 故障排查

### 问题: 播放有杂音

**可能原因和解决方案**:

1. **编码算法错误** (已修复)
   - 使用标准 ITU-T G.711 A-law 实现
   - 运行 `./test_alaw` 验证编码正确性

2. **WAV 文件头错误**
   - 检查格式码是否为 6 (A-law)
   - 检查位深度是否为 8
   - 使用 `hexdump -C file.wav | head -n 3` 查看文件头

3. **采样率不匹配**
   - 确认采样率为 8000 Hz
   - 使用 `soxi file.wav` 查看详细信息

4. **字节序问题**
   - A-law 是单字节编码，不应有字节序问题
   - 但 WAV 头部的多字节字段必须是小端序

### 问题: 文件显示 "13-bit" 精度

**原因**: 这是 SoX 的显示方式，实际上是正确的
- G.711 A-law 内部使用 13-bit 对数表示
- 但存储格式是 8-bit
- 这是正常现象，不影响播放

### 问题: 文件无法播放

**解决方案**:
- 确认文件格式: `file output.wav`
- 应显示: `RIFF (little-endian) data, WAVE audio, ITU G.711 A-law`
- 检查文件大小是否合理
- 使用 `hexdump -C output.wav | head -n 5` 查看文件头

### 问题: 编译错误

**解决方案**:
- 确保安装了 `libspeexdsp-dev`
- 检查 C++ 编译器版本 (需要 C++11 或更高)
- 查看完整的编译错误信息

### 验证 WAV 文件头

使用 hexdump 检查文件头是否正确：

```bash
hexdump -C output.wav | head -n 3
```

正确的 A-law WAV 文件头应该显示：
```
00000000  52 49 46 46 xx xx xx xx  57 41 56 45 66 6d 74 20  |RIFF....WAVEfmt |
00000010  10 00 00 00 06 00 01 00  40 1f 00 00 40 1f 00 00  |........@...@...|
00000020  01 00 08 00 64 61 74 61  xx xx xx xx ...          |....data....|
```

关键字段：
- `06 00`: 格式码 6 (A-law)
- `01 00`: 单声道
- `40 1f 00 00`: 8000 Hz (0x1F40 = 8000)
- `08 00`: 8-bit

## 参考资料

- [ITU-T G.711 标准](https://www.itu.int/rec/T-REC-G.711)
- [WAV 文件格式规范](http://soundfile.sapp.org/doc/WaveFormat/)
- [FreeSWITCH 文档](https://freeswitch.org/confluence/)
