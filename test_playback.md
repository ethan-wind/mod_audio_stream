# 播放问题修复说明

## 问题分析

根据日志显示，FreeSWITCH 没有调用 `capture_callback` 的 `WRITE_REPLACE` 类型，导致 `stream_play_frame` 函数从未被执行。

### 主要问题：

1. **函数返回值错误**：`stream_play_frame` 原本返回 `switch_bool_t`，但在 `WRITE_REPLACE` 回调中应该使用 `void` 类型，并通过 `switch_core_media_bug_set_write_replace_frame` 设置替换帧
2. **日志级别**：将关键的播放日志从 DEBUG 提升到 INFO，便于观察

## 修复内容

### 1. 修改函数签名（mod_audio_stream.h）
```c
// 从
switch_bool_t stream_play_frame(switch_media_bug_t *bug, private_t *tech_pvt);

// 改为
void stream_play_frame(switch_media_bug_t *bug, private_t *tech_pvt);
```

### 2. 修改函数实现（audio_streamer_glue.cpp）
- 将返回类型从 `switch_bool_t` 改为 `void`
- 移除所有 `return SWITCH_TRUE;` 语句，改为 `return;`
- 将注入成功的日志级别从 DEBUG 提升到 INFO

### 3. 修改回调处理（mod_audio_stream.c）
```c
case SWITCH_ABC_TYPE_WRITE_REPLACE:
    if (tech_pvt->stream_play_enabled) {
        stream_play_frame(bug, tech_pvt);
        return SWITCH_TRUE;  // 回调本身返回 TRUE
    }
    break;
```

## 测试步骤

1. 重新编译模块：
```bash
./build-mod-audio-stream.sh
```

2. 重启 FreeSWITCH 或重新加载模块：
```bash
fs_cli -x "reload mod_audio_stream"
```

3. 发起测试通话并观察日志：
```bash
fs_cli -x "console loglevel debug"
```

4. 查找以下关键日志：
- `capture_callback type=7 stream_play` - 确认 WRITE_REPLACE 被调用
- `stream_play_frame invoked` - 确认播放函数被执行
- `stream_play_frame injected X/Y bytes` - 确认音频被注入
- `Streaming playback: queued X samples` - 确认数据写入缓冲区

## 预期结果

修复后，当 WebSocket 接收到音频数据时：
1. 数据会被写入 `play_buffer`
2. `WRITE_REPLACE` 回调会被触发
3. `stream_play_frame` 从缓冲区读取数据并替换通话音频
4. 用户应该能听到播放的音频流
