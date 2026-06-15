#ifndef MOD_AUDIO_STREAM_H
#define MOD_AUDIO_STREAM_H

#include <stdio.h>
#include <switch.h>
#include <speex/speex_resampler.h>

#define MY_BUG_NAME "audio_stream"
#define MAX_SESSION_ID (256)
#define MAX_WS_URI (4096)
#define MAX_METADATA_LEN (8192)
#define MAX_RECORD_PATH (4096)

#define EVENT_CONNECT           "mod_audio_stream::connect"
#define EVENT_DISCONNECT        "mod_audio_stream::disconnect"
#define EVENT_ERROR             "mod_audio_stream::error"
#define EVENT_JSON              "mod_audio_stream::json"
#define EVENT_PLAY              "mod_audio_stream::play"

typedef void (*responseHandler_t)(switch_core_session_t* session, const char* eventName, const char* json);

struct private_data {
    switch_mutex_t *mutex;
    char sessionId[MAX_SESSION_ID];
    SpeexResamplerState *resampler;
    responseHandler_t responseHandler;
    void *pAudioStreamer;
    char ws_uri[MAX_WS_URI];
    int sampling;
    int channels;
    int audio_paused:1;
    int close_requested:1;
    char initialMetadata[8192];
    switch_buffer_t *sbuffer;
    int rtp_packets;
    
    // 流式播放支持
    switch_buffer_t *play_buffer;      // 播放缓冲区
    switch_mutex_t *play_mutex;        // 播放互斥锁
    int stream_play_enabled:1;         // 启用流式播放
    int playback_paused:1;             // 下行播放暂停标记（仅暂停缓冲区读取）
    switch_frame_t write_frame;        // WRITE_REPLACE 输出帧
    uint8_t *write_frame_data;         // WRITE_REPLACE 帧缓冲
    uint64_t play_frame_count;         // 播放帧计数器（用于速率控制）
    size_t last_buffer_inuse;          // 上次缓冲区大小（用于检测播放结束）
    uint32_t interrupt_count;          // 打断计数器（用于检测频繁打断）
    switch_time_t last_interrupt_time; // 上次打断时间
    
    // 播放线程支持
    switch_thread_t *play_thread;      // 播放线程
    int play_thread_running:1;         // 播放线程运行标志

    // 模块内录音：左声道=客户上行，右声道=TTS 下行，默认关闭
    switch_mutex_t *record_mutex;
    switch_buffer_t *record_read_buffer;
    FILE *record_file;
    char record_path[MAX_RECORD_PATH];
    uint32_t record_rate;
    uint32_t record_channels;
    uint64_t record_data_bytes;
    uint64_t record_frame_count;
    uint64_t record_dropped_bytes;
    int record_enabled:1;
    int record_failed:1;
    int record_stereo:1;
};

typedef struct private_data private_t;

enum notifyEvent_t {
    CONNECT_SUCCESS,
    CONNECT_ERROR,
    CONNECTION_DROPPED,
    MESSAGE
};

#ifdef __cplusplus
extern "C" {
#endif

// 流式播放函数声明
void stream_play_frame(switch_media_bug_t *bug, private_t *tech_pvt);

#ifdef __cplusplus
}
#endif

#endif //MOD_AUDIO_STREAM_H
