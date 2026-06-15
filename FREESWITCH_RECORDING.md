# FreeSWITCH 录音方式与 record_session 原理

## 录音方式概览

FreeSWITCH 的录音可以分成三类：应用层落文件、API 控制录音、媒体分流/旁路录音。

### 1. record 应用

`record` 是阻塞式录音应用，常用于 IVR、语音留言、提示音之后录制一段用户语音。

示例：

```xml
<action application="record" data="/tmp/msg.wav 60 200 3"/>
```

它会占用当前 dialplan 执行流程，直到录音结束、超时、静音检测命中或通话结束。因此它适合“录一段输入”，不适合透明地录完整通话。

### 2. record_session 应用

`record_session` 是最常见的通话全程录音方式。

示例：

```xml
<action application="record_session" data="/recordings/${uuid}.wav"/>
```

它是非阻塞的。执行后录音逻辑会挂到当前 channel 的媒体处理链路上，dialplan 可以继续向下执行，例如继续 `bridge`。

典型用法：

```xml
<action application="set" data="RECORD_STEREO=true"/>
<action application="set" data="record_sample_rate=8000"/>
<action application="record_session" data="/recordings/${uuid}.wav"/>
<action application="bridge" data="sofia/gateway/gw/10086"/>
```

停止录音：

```xml
<action application="stop_record_session" data="/recordings/${uuid}.wav"/>
```

### 3. uuid_record API

`uuid_record` 适合从外部系统控制录音，例如 ESL 服务、后台管理系统、坐席系统。

示例：

```bash
fs_cli -x "uuid_record <uuid> start /recordings/a.wav"
fs_cli -x "uuid_record <uuid> stop /recordings/a.wav"
fs_cli -x "uuid_record <uuid> mask /recordings/a.wav"
fs_cli -x "uuid_record <uuid> unmask /recordings/a.wav"
```

它和 `record_session` 的底层机制很接近，核心都是给指定 session 挂 media bug，然后在媒体帧经过 FreeSWITCH 时抓取音频并写入文件。

### 4. 应答后录音

如果只希望在对端接通后开始录音，可以配合 `execute_on_answer`、`api_on_answer` 等变量。

示例：

```xml
<action application="set" data="execute_on_answer=record_session /recordings/${uuid}.wav"/>
<action application="bridge" data="sofia/gateway/gw/10086"/>
```

这种方式可以避免把早期媒体、振铃音、失败呼叫等录入文件。

### 5. 单方向录音

可以通过 channel 变量控制只录 read 或 write 方向。

示例：

```xml
<action application="set" data="RECORD_READ_ONLY=true"/>
<action application="record_session" data="/recordings/${uuid}-read.wav"/>
```

或：

```xml
<action application="set" data="RECORD_WRITE_ONLY=true"/>
<action application="record_session" data="/recordings/${uuid}-write.wav"/>
```

方向需要站在当前 channel 视角理解：

- read：FreeSWITCH 从远端读入的音频。
- write：FreeSWITCH 写给远端的音频。

### 6. 双声道录音

可以通过 `RECORD_STEREO=true` 尽量把双方音频拆到左右声道。

示例：

```xml
<action application="set" data="RECORD_STEREO=true"/>
<action application="record_session" data="/recordings/${uuid}.wav"/>
```

双声道录音常用于质检、说话人分离、ASR 后处理。

### 7. 媒体分流与旁路录音

有些场景不直接使用 FreeSWITCH 文件录音，而是把音频实时送到外部系统：

- ESL 事件驱动配合 `uuid_record`。
- `mod_audio_stream` 或类似模块通过 WebSocket 推送音频。
- SIPREC。
- RTP proxy 或网关侧录音。
- 自定义 media bug 模块。
- `eavesdrop`、`uuid_audio` 等监听或音频注入能力。

这类方式更适合实时 ASR、实时质检、风控、坐席辅助、AI 语音分析等场景。

## record_session 的工作原理

`record_session` 的关键点是：它不是在 dialplan 当前线程里循环读音频写文件，而是通过 FreeSWITCH 的 media bug 机制挂接到 session 的媒体处理链路上。

整体流程如下：

```text
dialplan 执行 record_session
    ↓
mod_dptools 调用录音 API
    ↓
switch_ivr_record_session 创建录音上下文
    ↓
switch_core_media_bug_add 挂载 media bug
    ↓
通话媒体帧经过 channel media pipeline
    ↓
media bug callback 捕获 read/write 音频
    ↓
写入 switch_file_handle
    ↓
生成 wav/mp3/raw 等录音文件
```

### 1. dialplan 入口

当 dialplan 执行：

```xml
<action application="record_session" data="/recordings/${uuid}.wav"/>
```

`mod_dptools` 中的 `record_session` 应用入口会被调用。入口函数主要做参数解析和变量读取，然后调用核心录音逻辑。

核心调用路径可以概括为：

```text
record_session application
    -> switch_ivr_record_session(...)
    -> switch_core_media_bug_add(...)
```

### 2. 创建录音上下文

`switch_ivr_record_session` 会准备录音所需的上下文数据，包括：

- 录音文件路径。
- 文件句柄。
- 采样率。
- 声道数。
- 是否追加写入。
- 是否双声道。
- 是否只录 read 或 write。
- 是否启用 mask。
- media bug 私有数据。
- 录音开始时间、限制时长等元信息。

文件格式通常由扩展名和已加载的文件模块决定，例如：

- `.wav`：常见由 `mod_sndfile` 处理。
- `.mp3`：通常需要相关编码模块，例如 `mod_shout`。
- `.raw`：原始音频数据。

### 3. 挂载 media bug

FreeSWITCH 的 media bug 是媒体链路上的 hook。`record_session` 会把一个录音 bug 挂到当前 session 上。

挂载后，dialplan 不会被录音阻塞，通话仍然继续执行。之后每当该 channel 处理媒体帧时，录音回调就有机会拿到音频。

### 4. 回调处理音频帧

media bug 回调通常会处理几类事件：

- 初始化：准备缓冲、文件句柄、录音状态。
- 读方向音频：抓取远端进入 FreeSWITCH 的音频。
- 写方向音频：抓取 FreeSWITCH 发往远端的音频。
- 双声道处理：把双方音频按左右声道写入。
- mask：录音继续进行，但敏感片段写静音或不写真实音频。
- 关闭：flush 文件、关闭句柄、释放上下文。

可以把它理解成：

```text
RTP / codec
    ↓
FreeSWITCH 解码后的媒体帧
    ↓
channel read/write path
    ↓
record_session media bug
    ↓
文件写入
```

因此，`record_session` 录到的通常是 FreeSWITCH 媒体层看到的解码后音频，而不是原始 RTP 包。

### 5. 停止与清理

录音停止通常有几种情况：

- 通话挂断。
- 执行 `stop_record_session`。
- 执行 `uuid_record <uuid> stop ...`。
- 达到录音时长限制。
- session 被销毁。

停止时 media bug 会被移除，录音回调完成最后的 flush 和资源释放，文件最终落盘。

## 关键变量

常用变量如下：

```xml
<action application="set" data="RECORD_STEREO=true"/>
<action application="set" data="RECORD_READ_ONLY=true"/>
<action application="set" data="RECORD_WRITE_ONLY=true"/>
<action application="set" data="record_sample_rate=8000"/>
<action application="set" data="record_append=true"/>
```

实践中要注意变量作用域和设置时机。一般应在 `record_session` 执行前设置。

## 使用注意事项

### 1. bypass media 会影响录音

`record_session` 依赖 FreeSWITCH 能看到媒体帧。如果通话使用 bypass media、proxy media 或 RTP 直接在两端之间传输，FreeSWITCH 可能拿不到实际音频，录音会为空或不完整。

## mod_audio_stream 内置录音

除了 `record_session`，当前 `mod_audio_stream` 还支持在模块内部直接把：

- 客户上行音频
- 机器人下行 TTS 音频

按实际播放时序写成一个 WAV 文件。

这个方案的特点是：

- 不依赖 `record_session` 是否能看到 `WRITE_REPLACE` 后的帧。
- 录到的下行音频就是 `stream_play_frame()` 实际注入给线路的 TTS PCM。
- 默认关闭，不设置变量时不启用。

### 启用方式

模块内录音通过 channel 变量控制：

```xml
<action application="set" data="AUDIO_STREAM_RECORD_PATH=/recordings/${uuid}.wav"/>
```

只要设置了 `AUDIO_STREAM_RECORD_PATH`，模块内录音就会启用。

### 录音模式

当前支持两种模式：

#### 1. 双声道录音

设置：

```xml
<action application="set" data="AUDIO_STREAM_RECORD_PATH=/recordings/${uuid}.wav"/>
<action application="set" data="AUDIO_STREAM_RECORD_STEREO=true"/>
```

输出文件格式：

- WAV
- 16-bit PCM
- 采样率跟当前通话采样率一致
- 双声道

声道含义：

- 左声道：客户上行
- 右声道：机器人下行 TTS

适合：

- 排查“客户听到了什么”
- 质检
- 后续分声道 ASR
- TTS 注入链路验证

#### 2. 单声道混音录音

设置：

```xml
<action application="set" data="AUDIO_STREAM_RECORD_PATH=/recordings/${uuid}.wav"/>
<action application="unset" data="AUDIO_STREAM_RECORD_STEREO"/>
```

或者显式设为 false：

```xml
<action application="set" data="AUDIO_STREAM_RECORD_PATH=/recordings/${uuid}.wav"/>
<action application="set" data="AUDIO_STREAM_RECORD_STEREO=false"/>
```

输出文件格式：

- WAV
- 16-bit PCM
- 采样率跟当前通话采样率一致
- 单声道

音频内容：

- 客户上行和机器人下行做饱和混音后写入同一个声道

适合：

- 只关心最终通话内容
- 希望文件更小
- 下游系统只接受单声道 wav

### dialplan 示例

#### 示例 1：双声道模块内录音

```xml
<action application="set" data="AUDIO_STREAM_RECORD_PATH=/recordings/${strftime(%Y%m%d)}/${uuid}.wav"/>
<action application="set" data="AUDIO_STREAM_RECORD_STEREO=true"/>
<action application="set" data="api_on_answer=uuid_audio_stream ${uuid} start '${ws_url}' mono 8000"/>
<action application="playback" data="silence_stream://-1,1400"/>
```

#### 示例 2：单声道模块内录音

```xml
<action application="set" data="AUDIO_STREAM_RECORD_PATH=/recordings/${strftime(%Y%m%d)}/${uuid}.wav"/>
<action application="set" data="AUDIO_STREAM_RECORD_STEREO=false"/>
<action application="set" data="api_on_answer=uuid_audio_stream ${uuid} start '${ws_url}' mono 8000"/>
<action application="playback" data="silence_stream://-1,1400"/>
```

#### 示例 3：同时保留 record_session 和模块内录音

```xml
<action application="set" data="RECORD_STEREO=true"/>
<action application="record_session" data="/recordings/${uuid}.fs.wav"/>
<action application="set" data="AUDIO_STREAM_RECORD_PATH=/recordings/${uuid}.stream.wav"/>
<action application="set" data="AUDIO_STREAM_RECORD_STEREO=true"/>
<action application="set" data="api_on_answer=uuid_audio_stream ${uuid} start '${ws_url}' mono 8000"/>
<action application="playback" data="silence_stream://-1,1400"/>
```

这样可以同时得到两份录音：

- `${uuid}.fs.wav`：FreeSWITCH 原生 `record_session`
- `${uuid}.stream.wav`：`mod_audio_stream` 实际播放链路录音

### 当前实现说明

模块内录音目前的行为是：

- `stream_frame()` 读取客户上行音频并写入内部缓存。
- `stream_play_frame()` 作为录音主时钟，每个 WRITE_REPLACE tick 写一帧录音。
- 如果当前 tick 没有 TTS，下行侧写静音。
- 如果当前 tick 上行缓存不够，上行侧补静音。
- 录音文件关闭时回填 WAV 头。

也就是说，录音文件的时间轴跟实际下行播放节奏对齐，而不是简单地“谁先到就先写谁”。

### 变量说明

#### `AUDIO_STREAM_RECORD_PATH`

含义：

- 录音输出路径
- 不设置则不开启模块内录音

注意：

- 父目录不存在时，模块会尝试自动创建
- 路径建议使用 `.wav`

#### `AUDIO_STREAM_RECORD_STEREO`

含义：

- `true`：双声道录音
- `false` 或不设置：单声道混音录音

### 对现有功能的影响

不设置 `AUDIO_STREAM_RECORD_PATH` 时：

- 模块内录音完全关闭
- 现有 WebSocket 上行推流逻辑不变
- 现有 TTS 播放逻辑不变
- 现有 `record_session` 使用方式不变

设置后会增加：

- 少量 PCM 拷贝
- WAV 文件写盘 IO
- 一段上行录音缓存

### 当前限制

当前实现有这些边界：

- 当前按现有通话格式处理，适合你现在的单声道语音机器人场景
- 录音输出固定是 WAV 16-bit PCM
- 采样率直接使用当前通话采样率
- 双声道模式是“左客户、右机器人”
- 单声道模式是“客户 + 机器人”混音

如果后续还要扩，可以再做：

- 独立录音线程，避免媒体回调里直接写文件
- 录音格式选择
- 强制输出采样率
- 更细的统计和事件上报

### 2. 大并发录音要关注 IO

录音会带来磁盘写入压力。高并发场景要关注：

- 磁盘吞吐。
- 文件系统性能。
- 是否写网络盘。
- 采样率。
- 声道数。
- 编码格式。
- 单文件目录数量。

一般来说，WAV 写入 CPU 开销较低但文件大；MP3 文件小但编码开销更高。

### 3. 双声道不等于严格说话人分离

`RECORD_STEREO=true` 可以让双方音频更容易区分，但实际效果受桥接方式、媒体路径、混音方式、早期媒体等因素影响。后续做 ASR 或质检时，仍应对声道和时间戳做校验。

### 4. 文件格式依赖模块

录音文件能否生成，不只取决于扩展名，还取决于 FreeSWITCH 是否加载对应文件模块。生产环境中应确认 `mod_sndfile`、`mod_shout` 等模块状态。

## 选型建议

| 场景 | 推荐方式 |
| --- | --- |
| IVR 留言、录一段用户输入 | `record` |
| 通话全程录音 | `record_session` |
| 外部系统按需启停 | `uuid_record` |
| 只录接通后的通话 | `execute_on_answer` + `record_session` |
| 双方分声道后处理 | `RECORD_STEREO=true` + `record_session` |
| 实时 ASR / 实时质检 | media bug 音频流、`mod_audio_stream`、SIPREC |
| 网关统一录音 | 网关侧录音或 SIPREC |

## 简要结论

`record_session` 是 FreeSWITCH 通话录音的主力方式。它的本质是在 session 上挂载 media bug，通过媒体回调捕获 read/write 音频帧，再通过 FreeSWITCH 文件接口写入录音文件。

它的优点是非阻塞、使用简单、和 dialplan/API 集成方便；限制是必须让 FreeSWITCH 经过媒体路径，并且在大并发场景下需要认真设计磁盘、编码和文件管理策略。
