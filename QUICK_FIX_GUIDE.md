# 快速修复指南 - WebSocket 音频流播放问题

## 问题描述
FreeSWITCH 从 WebSocket 接收音频流但无法播放，日志显示 `capture_callback` 和 `stream_play_frame` 没有被调用。

## 已修复的问题

### 核心问题
`stream_play_frame` 函数的返回值类型不正确，导致 FreeSWITCH 的 WRITE_REPLACE 回调机制无法正常工作。

### 修改文件
1. `mod_audio_stream.h` - 函数声明
2. `audio_streamer_glue.cpp` - 函数实现
3. `mod_audio_stream.c` - 回调处理

## 编译和部署

### 1. 清理旧的构建
```bash
rm -rf build
```

### 2. 重新编译
```bash
chmod +x build-mod-audio-stream.sh
sudo ./build-mod-audio-stream.sh
```

### 3. 重启 FreeSWITCH 或重新加载模块
```bash
# 方式1：重新加载模块（推荐）
fs_cli -x "reload mod_audio_stream"

# 方式2：重启 FreeSWITCH
sudo systemctl restart freeswitch
```

## 验证修复

### 1. 启用调试日志
```bash
fs_cli -x "console loglevel debug"
```

### 2. 发起测试通话
使用你的测试脚本或拨号计划发起通话并启动 audio_stream。

### 3. 检查关键日志

应该看到以下日志输出：

```
✓ capture_callback type=... stream_play
✓ stream_play_frame invoked
✓ Streaming playback: queued X samples (buffer: Y ms)
✓ stream_play_frame injected X/Y bytes (buffer left: Z ms)
```

### 4. 如果仍然没有日志

检查 media bug 标志是否正确设置：
```bash
fs_cli -x "uuid_buglist <uuid>"
```

应该看到包含 `WRITE_REPLACE` 标志。

## 常见问题排查

### Q1: 编译失败
**A:** 确保安装了所有依赖：
```bash
sudo apt-get install -y libfreeswitch-dev libssl-dev zlib1g-dev libspeexdsp-dev cmake build-essential
```

### Q2: 模块加载失败
**A:** 检查模块是否在 FreeSWITCH 配置中启用：
```bash
fs_cli -x "module_exists mod_audio_stream"
```

### Q3: 仍然没有播放
**A:** 检查以下几点：
1. WebSocket 是否成功连接（查找 "connected" 日志）
2. 是否收到音频数据（查找 "processMessage - audioDataType" 日志）
3. 数据是否写入缓冲区（查找 "Streaming playback: queued" 日志）
4. 缓冲区是否被读取（查找 "stream_play_frame injected" 日志）

### Q4: 音频断续或有杂音
**A:** 可能是缓冲区大小或采样率问题：
1. 检查 WebSocket 发送的采样率是否与通话采样率匹配
2. 调整播放缓冲区大小（在 `stream_data_init` 中修改 `play_buflen`）

## 技术细节

### WRITE_REPLACE 机制
FreeSWITCH 的 WRITE_REPLACE 回调允许模块替换发送给对方的音频：
1. 回调函数读取原始音频帧
2. 从播放缓冲区读取新音频数据
3. 使用 `switch_core_media_bug_set_write_replace_frame` 设置替换帧
4. FreeSWITCH 将替换后的音频发送给对方

### 数据流
```
WebSocket → processMessage → play_buffer → stream_play_frame → 对方听到
```

## 下一步优化建议

1. **动态缓冲区管理**：根据网络延迟自动调整缓冲区大小
2. **静音检测**：当缓冲区为空时生成舒适噪声而不是静音
3. **时间戳同步**：确保音频播放的时间戳连续性
4. **错误恢复**：处理 WebSocket 断线重连时的缓冲区状态
