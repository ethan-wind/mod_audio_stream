#include <string>
#include <cstring>
#include "mod_audio_stream.h"
//#include <ixwebsocket/IXWebSocket.h>
#include "WebSocketClient.h"
#include <switch_json.h>
#include <fstream>
#include <switch_buffer.h>
#include <unordered_map>
#include <unordered_set>
#include "base64.h"

#define FRAME_SIZE_8000  320 /* 1000x0.02 (20ms)= 160 x(16bit= 2 bytes) 320 frame size*/

// G.711 A-law encoding - Standard ITU-T G.711 implementation
namespace {
    // 小端序写入辅助函数
    inline void write_le16(std::ofstream& file, uint16_t value) {
        uint8_t bytes[2];
        bytes[0] = value & 0xFF;
        bytes[1] = (value >> 8) & 0xFF;
        file.write((char*)bytes, 2);
    }
    
    inline void write_le32(std::ofstream& file, uint32_t value) {
        uint8_t bytes[4];
        bytes[0] = value & 0xFF;
        bytes[1] = (value >> 8) & 0xFF;
        bytes[2] = (value >> 16) & 0xFF;
        bytes[3] = (value >> 24) & 0xFF;
        file.write((char*)bytes, 4);
    }
    
    // A-law 段查找表
    static const int16_t seg_aend[8] = {0x1F, 0x3F, 0x7F, 0xFF, 0x1FF, 0x3FF, 0x7FF, 0xFFF};
    
    // 查找段号
    static int16_t search(int16_t val, const int16_t *table, int16_t size) {
        for (int16_t i = 0; i < size; i++) {
            if (val <= *table++)
                return i;
        }
        return size;
    }
    
    // 将 16-bit 线性 PCM 转换为 8-bit A-law
    uint8_t linear_to_alaw(int16_t pcm_val) {
        int16_t mask;
        int16_t seg;
        uint8_t aval;

        // 获取符号位
        if (pcm_val >= 0) {
            mask = 0xD5;  // 正数
        } else {
            mask = 0x55;  // 负数
            pcm_val = -pcm_val - 1;
            if (pcm_val < 0) {
                pcm_val = 0;
            }
        }

        // 转换为 13-bit 值
        if (pcm_val > 0x7FFF) {
            pcm_val = 0x7FFF;
        }
        
        pcm_val = pcm_val >> 3;

        // 查找段号
        seg = search(pcm_val, seg_aend, 8);

        // 组合段号和量化值
        if (seg >= 8) {  // 超出范围
            return (uint8_t)(0x7F ^ mask);
        } else {
            aval = (uint8_t)seg << 4;
            if (seg < 2) {
                aval |= (pcm_val >> 1) & 0x0F;
            } else {
                aval |= (pcm_val >> seg) & 0x0F;
            }
            return aval ^ mask;
        }
    }
}

class AudioStreamer {
public:

    AudioStreamer(const char* uuid, const char* wsUri, responseHandler_t callback, int deflate, int heart_beat,
                    bool suppressLog, const char* extra_headers, bool no_reconnect,
                    const char* tls_cafile, const char* tls_keyfile, const char* tls_certfile,
                    bool tls_disable_hostname_validation): m_sessionId(uuid), m_notify(callback),
                    m_suppress_log(suppressLog), m_extra_headers(extra_headers), m_playFile(0){

        WebSocketHeaders hdrs;
        WebSocketTLSOptions tls;

        if (m_extra_headers) {
            cJSON *headers_json = cJSON_Parse(m_extra_headers);
            if (headers_json) {
                cJSON *iterator = headers_json->child;
                while (iterator) {
                    if (iterator->type == cJSON_String && iterator->valuestring != nullptr) {
                        hdrs.set(iterator->string, iterator->valuestring);
                    }
                    iterator = iterator->next;
                }
                cJSON_Delete(headers_json);
            }
        }

        client.setUrl(wsUri);

        // Setup eventual TLS options.
        // tls_cafile may hold the special values
        // NONE, which disables validation and SYSTEM which uses
        // the system CAs bundle
        if (tls_cafile) {
            tls.caFile = tls_cafile;
        }

        if (tls_keyfile) {
            tls.keyFile = tls_keyfile;
        }

        if (tls_certfile) {
            tls.certFile = tls_certfile;
        }

        tls.disableHostnameValidation = tls_disable_hostname_validation;
        client.setTLSOptions(tls);

        // Optional heart beat, sent every xx seconds when there is not any traffic
        // to make sure that load balancers do not kill an idle connection.
        if(heart_beat)
            client.setPingInterval(heart_beat);

        // Per message deflate connection is enabled by default. You can tweak its parameters or disable it
        if(deflate)
            client.enableCompression(false);

        // Set extra headers if any
        if(!hdrs.empty())
            client.setHeaders(hdrs);

        // Setup a callback to be fired when a message or an event (open, close, error) is received
        client.setMessageCallback([this](const std::string& message) {
            eventCallback(MESSAGE, message.c_str());
        });

        // Setup a callback for binary messages
        client.setBinaryCallback([this](const void* data, size_t length) {
            std::vector<uint8_t> vec(static_cast<const uint8_t*>(data), 
                                     static_cast<const uint8_t*>(data) + length);
            processBinaryMessage(vec);
        });

        client.setOpenCallback([this]() {
            cJSON *root;
            root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "status", "connected");
            char *json_str = cJSON_PrintUnformatted(root);
            eventCallback(CONNECT_SUCCESS, json_str);
            cJSON_Delete(root);
            switch_safe_free(json_str);
        });

        client.setErrorCallback([this](int code, const std::string &msg) {
            cJSON *root, *message;
            root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "status", "error");
            message = cJSON_CreateObject();
            cJSON_AddNumberToObject(message, "code", code);
            cJSON_AddStringToObject(message, "error", msg.c_str());
            cJSON_AddItemToObject(root, "message", message);

            char *json_str = cJSON_PrintUnformatted(root);

            eventCallback(CONNECT_ERROR, json_str);

            cJSON_Delete(root);
            switch_safe_free(json_str);
        });

        client.setCloseCallback([this](int code, const std::string &reason) {
            cJSON *root, *message;
            root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "status", "disconnected");
            message = cJSON_CreateObject();
            cJSON_AddNumberToObject(message, "code", code);
            cJSON_AddStringToObject(message, "reason", reason.c_str());
            cJSON_AddItemToObject(root, "message", message);
            char *json_str = cJSON_PrintUnformatted(root);

            eventCallback(CONNECTION_DROPPED, json_str);

            cJSON_Delete(root);
            switch_safe_free(json_str);
        });

        // Now that our callback is setup, we can start our background thread and receive messages
        client.connect();
    }

    switch_media_bug_t *get_media_bug(switch_core_session_t *session) {
        switch_channel_t *channel = switch_core_session_get_channel(session);
        if(!channel) {
            return nullptr;
        }
        auto *bug = (switch_media_bug_t *) switch_channel_get_private(channel, MY_BUG_NAME);
        return bug;
    }

    inline void media_bug_close(switch_core_session_t *session) {
        auto *bug = get_media_bug(session);
        if(bug) {
            auto* tech_pvt = (private_t*) switch_core_media_bug_get_user_data(bug);
            tech_pvt->close_requested = 1;
            switch_core_media_bug_close(&bug, SWITCH_FALSE);
        }
    }

    inline void send_initial_metadata(switch_core_session_t *session) {
        auto *bug = get_media_bug(session);
        if(bug) {
            auto* tech_pvt = (private_t*) switch_core_media_bug_get_user_data(bug);
            if(tech_pvt && strlen(tech_pvt->initialMetadata) > 0) {
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG,
                                          "sending initial metadata %s\n", tech_pvt->initialMetadata);
                writeText(tech_pvt->initialMetadata);
            }
        }
    }

    void eventCallback(notifyEvent_t event, const char* message) {
        switch_core_session_t* psession = switch_core_session_locate(m_sessionId.c_str());
        if(psession) {
            switch (event) {
                case CONNECT_SUCCESS:
                    send_initial_metadata(psession);
                    m_notify(psession, EVENT_CONNECT, message);
                    break;
                case CONNECTION_DROPPED:
                    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(psession), SWITCH_LOG_INFO, "connection closed\n");
                    m_notify(psession, EVENT_DISCONNECT, message);
                    break;
                case CONNECT_ERROR:
                    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(psession), SWITCH_LOG_INFO, "connection error\n");
                    m_notify(psession, EVENT_ERROR, message);

                    media_bug_close(psession);

                    break;
                case MESSAGE:
                    std::string msg(message);
                    if(processMessage(psession, msg) != SWITCH_TRUE) {
                        m_notify(psession, EVENT_JSON, msg.c_str());
                    }
                    // if(!m_suppress_log)
                        // switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(psession), SWITCH_LOG_DEBUG, "response: %s\n", msg.c_str());
                    break;
            }
            switch_core_session_rwunlock(psession);
        }
    }

    void processBinaryMessage(const std::vector<uint8_t>& data) {
        switch_core_session_t* psession = switch_core_session_locate(m_sessionId.c_str());
        if (!psession) {
            return;
        }

        // 直接播放收到的二进制原始消息
        // 假设数据格式：16-bit Linear PCM，采样率与通话采样率一致
        if (data.empty()) {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(psession), SWITCH_LOG_WARNING,
                "(%s) 收到空的二进制消息\n", m_sessionId.c_str());
            switch_core_session_rwunlock(psession);
            return;
        }

        // 获取 tech_pvt 用于流式播放
        auto *bug = get_media_bug(psession);
        private_t *tech_pvt = nullptr;
        if (bug) {
            tech_pvt = (private_t*) switch_core_media_bug_get_user_data(bug);
        }

        if (tech_pvt && tech_pvt->stream_play_enabled) {
            // 写入播放缓冲区（使用互斥锁保护，防止与打断操作冲突）
            switch_mutex_lock(tech_pvt->play_mutex);
            
            size_t data_size = data.size();
            size_t available = switch_buffer_freespace(tech_pvt->play_buffer);
            size_t buffer_inuse = switch_buffer_inuse(tech_pvt->play_buffer);
            
            if (available >= data_size) {
                switch_buffer_write(tech_pvt->play_buffer,
                                   data.data(),
                                   data_size);

                // 计算缓冲区时长（毫秒）
                size_t new_inuse = switch_buffer_inuse(tech_pvt->play_buffer);
                double buffer_ms = (double)new_inuse /
                                  (tech_pvt->sampling * tech_pvt->channels * sizeof(int16_t)) * 1000.0;

                // 更新 last_buffer_inuse，用于检测播放结束
                tech_pvt->last_buffer_inuse = new_inuse;

                // 只在缓冲区从空变为有数据时记录日志（表示新音频流开始）
                if (buffer_inuse == 0 && new_inuse > 0) {
                    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(psession), SWITCH_LOG_INFO,
                        "(%s) 开始播放新音频流，缓冲区: %.2f ms (打断计数: %u)\n",
                        m_sessionId.c_str(), buffer_ms, tech_pvt->interrupt_count);

                    // 重置打断计数器（新音频流开始）
                    if (switch_time_now() - tech_pvt->last_interrupt_time > 2000000) {  // 2秒后重置
                        tech_pvt->interrupt_count = 0;
                    }
                }
            } else {
                double buffer_ms = (double)buffer_inuse / 
                                  (tech_pvt->sampling * tech_pvt->channels * sizeof(int16_t)) * 1000.0;
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(psession), SWITCH_LOG_WARNING,
                    "(%s) 播放缓冲区已满 (%.2f ms)，丢弃 %zu bytes\n",
                    m_sessionId.c_str(), buffer_ms, data_size);
            }
            
            switch_mutex_unlock(tech_pvt->play_mutex);
        } else {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(psession), SWITCH_LOG_DEBUG,
                "(%s) 流式播放未启用或 tech_pvt 为空\n", m_sessionId.c_str());
        }

        switch_core_session_rwunlock(psession);
    }

    switch_bool_t processMessage(switch_core_session_t* session, std::string& message) {
        cJSON* json = cJSON_Parse(message.c_str());
        switch_bool_t status = SWITCH_FALSE;
        if (!json) {
            return status;
        }
        
        // 处理 turnHuman 消息
        const char* jsAction = cJSON_GetObjectCstr(json, "action");
        if (jsAction && strcmp(jsAction, "turnHuman") == 0) {
            const char* jsServiceNumber = cJSON_GetObjectCstr(json, "serviceNumber");
            const char* jsAccountId = cJSON_GetObjectCstr(json, "accountId");
            const char* jsCustomerNumber = cJSON_GetObjectCstr(json, "customerNumber");
            const char* jsCallUniqueId = cJSON_GetObjectCstr(json, "callUniqueId");
            
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
                "(%s) 收到 turnHuman 消息，serviceNumber: %s, accountId: %s, customerNumber: %s\n",
                m_sessionId.c_str(), 
                jsServiceNumber ? jsServiceNumber : "未指定",
                jsAccountId ? jsAccountId : "未指定",
                jsCustomerNumber ? jsCustomerNumber : "未指定");
            
            // 抛出自定义事件给 ESL
            switch_event_t *event = NULL;
            if (switch_event_create(&event, SWITCH_EVENT_CUSTOM) == SWITCH_STATUS_SUCCESS) {
                switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "Event-Subclass", "audio_stream");
                switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "Action", "turnHuman");
                if (jsServiceNumber) {
                    switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "Service-Number", jsServiceNumber);
                }
                if (jsAccountId) {
                    switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "AccountId", jsAccountId);
                }
                if (jsCustomerNumber) {
                    switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "Customer-Number", jsCustomerNumber);
                }
                if (jsCallUniqueId) {
                    switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "CallUniqueId", jsCallUniqueId);
                }
                switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "Channel-Call-UUID", m_sessionId.c_str());
                switch_event_fire(&event);
                
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
                    "(%s) turnHuman 事件已抛出\n", m_sessionId.c_str());
            }
            
            cJSON_Delete(json);
            return SWITCH_TRUE;
        }
        
        // 处理 hangup 消息
        if (jsAction && strcmp(jsAction, "hangup") == 0) {
            const char* jsReason = cJSON_GetObjectCstr(json, "reason");
            
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
                "(%s) 收到 hangup 消息，reason: %s\n",
                m_sessionId.c_str(), 
                jsReason ? jsReason : "未指定");
            
            // 抛出自定义挂机事件给 ESL
            switch_event_t *event = NULL;
            if (switch_event_create(&event, SWITCH_EVENT_CUSTOM) == SWITCH_STATUS_SUCCESS) {
                switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "Event-Subclass", "audio_stream");
                switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "Action", "hangup");
                switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "Channel-Call-UUID", m_sessionId.c_str());
                switch_event_fire(&event);
                
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
                    "(%s) hangup 事件已抛出\n", m_sessionId.c_str());
            }
            
            cJSON_Delete(json);
            return SWITCH_TRUE;
        }
        
        // 处理 stop_playback 消息（语音打断）
        if (jsAction && strcmp(jsAction, "stop_playback") == 0) {
            const char* jsReason = cJSON_GetObjectCstr(json, "reason");
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
                "(%s) 收到 stop_playback 消息，原因: %s\n",
                m_sessionId.c_str(), jsReason ? jsReason : "未指定");
            
            // 清空播放缓冲区，打断当前播放
            auto *bug = get_media_bug(session);
            if (bug) {
                auto* tech_pvt = (private_t*) switch_core_media_bug_get_user_data(bug);
                if (tech_pvt && tech_pvt->play_buffer) {
                    switch_mutex_lock(tech_pvt->play_mutex);
                    
                    // 获取打断前的缓冲区状态
                    size_t buffer_inuse = switch_buffer_inuse(tech_pvt->play_buffer);
                    double buffer_ms = (double)buffer_inuse / 
                                      (tech_pvt->sampling * tech_pvt->channels * sizeof(int16_t)) * 1000.0;
                    
                    // 检测频繁打断（1秒内超过5次）
                    switch_time_t now = switch_time_now();
                    switch_time_t time_diff = now - tech_pvt->last_interrupt_time;
                    
                    if (time_diff < 1000000) {  // 1秒 = 1,000,000 微秒
                        tech_pvt->interrupt_count++;
                        if (tech_pvt->interrupt_count > 5) {
                            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_WARNING,
                                "(%s) 检测到频繁打断 (%u 次/秒)，可能存在异常\n",
                                m_sessionId.c_str(), tech_pvt->interrupt_count);
                        }
                    } else {
                        // 重置计数器
                        tech_pvt->interrupt_count = 1;
                    }
                    tech_pvt->last_interrupt_time = now;
                    
                    // 清空播放缓冲区
                    switch_buffer_zero(tech_pvt->play_buffer);
                    
                    // 重置播放状态
                    tech_pvt->last_buffer_inuse = 0;
                    
                    switch_mutex_unlock(tech_pvt->play_mutex);
                    
                    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
                        "(%s) 播放已打断，清空缓冲区 %.2f ms (%.2f KB)，准备接收新音频\n",
                        m_sessionId.c_str(), buffer_ms, buffer_inuse / 1024.0);
                } else {
                    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_WARNING,
                        "(%s) 无法打断播放：tech_pvt 或 play_buffer 为空\n", m_sessionId.c_str());
                }
            } else {
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_WARNING,
                    "(%s) 无法打断播放：media bug 未找到\n", m_sessionId.c_str());
            }
            
            cJSON_Delete(json);
            return SWITCH_TRUE;
        }
        
        const char* jsType = cJSON_GetObjectCstr(json, "type");
        if(jsType && strcmp(jsType, "streamAudio") == 0) {
            cJSON* jsonData = cJSON_GetObjectItem(json, "data");
            if(jsonData) {
                cJSON* jsonFile = nullptr;
                cJSON* jsonAudio = cJSON_DetachItemFromObject(jsonData, "audioData");
                const char* jsAudioDataType = cJSON_GetObjectCstr(jsonData, "audioDataType");
                
                std::string fileType;
                int sampleRate = 0;
                if (jsAudioDataType && 0 == strcmp(jsAudioDataType, "raw")) {
                    cJSON* jsonSampleRate = cJSON_GetObjectItem(jsonData, "sampleRate");
                    if (jsonSampleRate && jsonSampleRate->valueint > 0) {
                        sampleRate = jsonSampleRate->valueint;
                    } else {
                        // 如果 JSON 中没有 sampleRate 或值无效，假设是 24000 Hz
                        sampleRate = 24000;
                        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_WARNING,
                            "(%s) JSON 中缺少或无效 sampleRate，假设为 24000 Hz\n", m_sessionId.c_str());
                    }
                    
                    std::unordered_map<int, const char*> sampleRateMap = {
                            {8000, ".r8"},
                            {16000, ".r16"},
                            {24000, ".r24"},
                            {32000, ".r32"},
                            {48000, ".r48"},
                            {64000, ".r64"}
                    };
                    auto it = sampleRateMap.find(sampleRate);
                    fileType = (it != sampleRateMap.end()) ? it->second : "";
                    
                    // switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
                    //                  "(%s) 接收音频: sampleRate=%d Hz, fileType=%s\n",
                    //                  m_sessionId.c_str(), sampleRate, fileType.c_str());
                } else if (jsAudioDataType && 0 == strcmp(jsAudioDataType, "wav")) {
                    fileType = ".wav";
                } else if (jsAudioDataType && 0 == strcmp(jsAudioDataType, "mp3")) {
                    fileType = ".mp3";
                } else if (jsAudioDataType && 0 == strcmp(jsAudioDataType, "ogg")) {
                    fileType = ".ogg";
                } else {
                    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, 
                                      "(%s) processMessage - unsupported or missing audio type: %s\n",
                                      m_sessionId.c_str(), jsAudioDataType ? jsAudioDataType : "NULL");
                }

                if(jsonAudio && jsonAudio->valuestring != nullptr && !fileType.empty()) {
                    char finalFilePath[256];
                    std::string rawAudio;
                    try {
                        rawAudio = base64_decode(jsonAudio->valuestring);
                    } catch (const std::exception& e) {
                        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "(%s) processMessage - base64 decode error: %s\n",
                                          m_sessionId.c_str(), e.what());
                        cJSON_Delete(jsonAudio); cJSON_Delete(json);
                        return status;
                    }
                    
                    // 只处理raw格式的音频，转换为 FreeSWitch 可播放的格式
                    if (jsAudioDataType && 0 == strcmp(jsAudioDataType, "raw") && sampleRate > 0) {
                        
                        // ========== 步骤 1: 32-bit Float → 16-bit Linear PCM ==========
                        // 输入格式 (WebSocket 原始流):
                        //   - 编码: 32-bit Float (IEEE 754)
                        //   - 采样率: 24000 Hz (可变)
                        //   - 声道: 单声道 (Mono)
                        //   - 位深: 32-bit (4 字节/样本)
                        //   - 字节序: 小端序 (Little Endian)
                        //   - 数值范围: [-1.0, 1.0]
                        //
                        // 输出格式 (FreeSWitch 内部格式):
                        //   - 编码: 16-bit Linear PCM (有符号整数)
                        //   - 采样率: 保持不变 (24000 Hz)
                        //   - 声道: 单声道 (Mono)
                        //   - 位深: 16-bit (2 字节/样本)
                        //   - 字节序: 小端序 (Little Endian)
                        //   - 数值范围: [-32768, 32767]
                        
                        // 计算样本数: 总字节数 / 每样本字节数
                        // 32-bit Float = 4 字节/样本
                        size_t input_samples = rawAudio.size() / sizeof(float);
                        std::vector<int16_t> pcm16bit(input_samples);
                        
                        // 将原始数据解释为 float 数组 (小端序)
                        const float* float_data = reinterpret_cast<const float*>(rawAudio.data());
                        
                        // 逐样本转换: 32-bit Float → 16-bit PCM
                        // 使用与文件保存完全相同的转换逻辑，确保一致性
                        for (size_t i = 0; i < input_samples; i++) {
                            float sample = float_data[i];
                            
                            // 限幅处理，防止溢出
                            if (sample > 1.0f) sample = 1.0f;
                            if (sample < -1.0f) sample = -1.0f;
                            
                            // 转换公式: Float32 [-1.0, 1.0] → 16-bit PCM [-32767, 32767]
                            pcm16bit[i] = static_cast<int16_t>(sample * 32767.0f);
                        }
                        
                        // 获取 tech_pvt 用于流式播放
                        auto *bug = get_media_bug(session);
                        private_t *tech_pvt = nullptr;
                        if (bug) {
                            tech_pvt = (private_t*) switch_core_media_bug_get_user_data(bug);
                        }
                        
                        // ========== 步骤 2: 重采样到通话采样率 ==========
                        // 输入: 16-bit PCM @ 24000 Hz, Mono
                        // 输出: 16-bit PCM @ 8000/16000 Hz, Mono (FreeSWitch 播放格式)
                        //
                        // 目标格式 (FreeSWitch WRITE_REPLACE 播放格式):
                        //   - 编码: 16-bit Linear PCM
                        //   - 采样率: 通话采样率 (通常 8000 或 16000 Hz)
                        //   - 声道: 单声道 (Mono)
                        //   - 位深: 16-bit (2 字节/样本)
                        //   - 字节序: 小端序 (Little Endian)
                        //   - 帧长: 20ms (标准 RTP 帧)
                        //
                        // 重采样比例:
                        //   - 24000 Hz → 8000 Hz: 降采样 3:1
                        //   - 24000 Hz → 16000 Hz: 降采样 3:2
                        if (tech_pvt && tech_pvt->stream_play_enabled) {
                            int target_rate = tech_pvt->sampling;      // 通话采样率 (8000/16000 Hz)
                            int target_channels = tech_pvt->channels;  // 通话声道数 (通常为1)
                            
                            std::vector<int16_t> playbackSamples;
                            
                            // 使用 SpeexDSP 重采样器进行高质量重采样
                            if (sampleRate != target_rate) {
                                int err;
                                SpeexResamplerState* resampler = speex_resampler_init(
                                    1,                           // 输入声道数 (单声道)
                                    sampleRate,                  // 输入采样率 (24000 Hz)
                                    target_rate,                 // 输出采样率 (8000/16000 Hz)
                                    SWITCH_RESAMPLE_QUALITY,     // 高质量模式
                                    &err
                                );
                                
                                if (err == 0 && resampler) {
                                    // 计算输出样本数，防止溢出
                                    // 例如: 24000 Hz → 8000 Hz, 1000 samples → 333 samples
                                    size_t output_samples = ((uint64_t)input_samples * target_rate + sampleRate - 1) / sampleRate;
                                    playbackSamples.resize(output_samples);
                                    spx_uint32_t in_len = input_samples;
                                    spx_uint32_t out_len = output_samples;
                                    
                                    int resample_err = speex_resampler_process_int(resampler, 0,
                                                                pcm16bit.data(),
                                                                &in_len,
                                                                playbackSamples.data(),
                                                                &out_len);
                                    
                                    if (resample_err != 0) {
                                        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
                                            "(%s) 重采样处理失败: %s\n",
                                            m_sessionId.c_str(), speex_resampler_strerror(resample_err));
                                    }
                                    
                                    playbackSamples.resize(out_len);
                                    speex_resampler_destroy(resampler);
                                    
                                    // 检查重采样后的样本
                                    int16_t min_pcm = 0, max_pcm = 0;
                                    if (out_len > 0) {
                                        min_pcm = max_pcm = playbackSamples[0];
                                        for (spx_uint32_t i = 1; i < out_len; i++) {
                                            if (playbackSamples[i] < min_pcm) min_pcm = playbackSamples[i];
                                            if (playbackSamples[i] > max_pcm) max_pcm = playbackSamples[i];
                                        }
                                    }
                                    
                                } else {
                                    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
                                        "(%s) 重采样器初始化失败: %s\n",
                                        m_sessionId.c_str(), speex_resampler_strerror(err));
                                    playbackSamples = pcm16bit;
                                }
                            } else {
                                // 采样率已匹配，无需重采样
                                playbackSamples = pcm16bit;
                                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG,
                                    "(%s) 无需重采样: %d Hz\n",
                                    m_sessionId.c_str(), sampleRate);
                            }
                            
                            // ========== 步骤 3: 写入播放缓冲区 ==========
                            // 数据格式验证:
                            //   - playbackSamples 是 std::vector<int16_t>
                            //   - 16-bit Linear PCM, 小端序 ✓
                            //   - 采样率已匹配通话采样率 ✓
                            //   - 单声道 ✓
                            //
                            // 此格式可直接被 FreeSWitch WRITE_REPLACE 使用
                            
                            switch_mutex_lock(tech_pvt->play_mutex);
                            size_t data_size = playbackSamples.size() * sizeof(int16_t);
                            size_t available = switch_buffer_freespace(tech_pvt->play_buffer);
                            
                            if (available >= data_size) {
                                // 写入 16-bit Linear PCM 数据到播放缓冲区
                                // FreeSWitch 会在 WRITE_REPLACE 回调中读取并自动编码
                                size_t written = switch_buffer_write(tech_pvt->play_buffer,
                                                   (uint8_t*)playbackSamples.data(),
                                                   data_size);

                                size_t buffer_inuse = switch_buffer_inuse(tech_pvt->play_buffer);
                                double buffer_ms = (double)buffer_inuse / (tech_pvt->sampling * tech_pvt->channels * sizeof(int16_t)) * 1000.0;

                                // 更新 last_buffer_inuse，用于检测播放结束
                                tech_pvt->last_buffer_inuse = buffer_inuse;

                            } else {
                                size_t buffer_inuse = switch_buffer_inuse(tech_pvt->play_buffer);
                                double buffer_ms = (double)buffer_inuse / (tech_pvt->sampling * tech_pvt->channels * sizeof(int16_t)) * 1000.0;
                                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_WARNING,
                                    "(%s) 播放缓冲区已满 (%.2f ms)，丢弃 %zu samples\n",
                                    m_sessionId.c_str(), buffer_ms, playbackSamples.size());
                            }
                            switch_mutex_unlock(tech_pvt->play_mutex);
                        }
                        
                        std::vector<int16_t> outputSamples;
                        
                        // 步骤 2b: 文件保存 - 重采样到 8000Hz（兼容性）
                        if (sampleRate != 8000) {
                            int err;
                            SpeexResamplerState* resampler = speex_resampler_init(1, sampleRate, 8000, SWITCH_RESAMPLE_QUALITY, &err);
                            
                            if (err == 0 && resampler) {
                                // 防止溢出：使用 uint64_t 进行中间计算
                                size_t output_samples = ((uint64_t)input_samples * 8000 + sampleRate - 1) / sampleRate;
                                
                                outputSamples.resize(output_samples);
                                spx_uint32_t in_len = input_samples;
                                spx_uint32_t out_len = output_samples;
                                
                                speex_resampler_process_int(resampler, 0,
                                                            pcm16bit.data(),
                                                            &in_len,
                                                            outputSamples.data(),
                                                            &out_len);
                                
                                outputSamples.resize(out_len);
                                speex_resampler_destroy(resampler);
                                
                                // switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
                                //                  "(%s) processMessage - resampled from %d to 8000 Hz: %zu -> %zu samples\n",
                                //                  m_sessionId.c_str(), sampleRate, input_samples, out_len);
                            } else {
                                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
                                                  "(%s) processMessage - failed to initialize resampler: %s\n",
                                                  m_sessionId.c_str(), speex_resampler_strerror(err));
                                cJSON_Delete(jsonAudio); cJSON_Delete(json);
                                return status;
                            }
                        } else {
                            // 采样率已经是8000Hz，直接使用 16-bit 数据
                            outputSamples = pcm16bit;
                            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
                                              "(%s) processMessage - no resampling needed, using %zu samples\n",
                                              m_sessionId.c_str(), input_samples);
                        }
                        
                        // 生成 G.711 A-law WAV 文件
                        switch_snprintf(finalFilePath, 256, "%s%s%s_%d.wav", SWITCH_GLOBAL_dirs.temp_dir,
                                        SWITCH_PATH_SEPARATOR, m_sessionId.c_str(), m_playFile++);
                        
                        // 将 PCM 转换为 A-law
                        std::vector<uint8_t> alawData(outputSamples.size());
                        for (size_t i = 0; i < outputSamples.size(); i++) {
                            alawData[i] = linear_to_alaw(outputSamples[i]);
                        }
                        
                        // switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
                        //                  "(%s) processMessage - converted %zu samples to A-law\n",
                        //                  m_sessionId.c_str(), outputSamples.size());
                        
                        // 写入 G.711 A-law WAV 文件头和数据（小端序）
                        std::ofstream wavFile(finalFilePath, std::ofstream::binary);
                        if (wavFile.is_open()) {
                            uint32_t dataSize = alawData.size();  // A-law 是 8-bit，每样本 1 字节
                            uint32_t fileSize = 38 + dataSize;    // 使用 fmt size=18 时，总大小为 38+dataSize
                            uint32_t outputSampleRate = 8000;     // 固定输出 8000 Hz
                            uint16_t numChannels = 1;
                            uint16_t bitsPerSample = 8;           // A-law 是 8-bit
                            uint32_t byteRate = outputSampleRate * numChannels * bitsPerSample / 8;
                            uint16_t blockAlign = numChannels * bitsPerSample / 8;
                            
                            // RIFF header (小端序)
                            wavFile.write("RIFF", 4);
                            write_le32(wavFile, fileSize);
                            wavFile.write("WAVE", 4);
                            
                            // fmt chunk (小端序) - 使用标准建议的 size=18
                            wavFile.write("fmt ", 4);
                            write_le32(wavFile, 18);  // fmtSize: 标准建议为 18
                            write_le16(wavFile, 6);   // audioFormat: G.711 A-law
                            write_le16(wavFile, numChannels);
                            write_le32(wavFile, outputSampleRate);
                            write_le32(wavFile, byteRate);
                            write_le16(wavFile, blockAlign);
                            write_le16(wavFile, bitsPerSample);
                            write_le16(wavFile, 0);   // cbSize: 扩展字段大小为 0
                            
                            // data chunk (小端序)
                            wavFile.write("data", 4);
                            write_le32(wavFile, dataSize);
                            wavFile.write((char*)alawData.data(), dataSize);
                            
                            wavFile.close();
                            
                            m_Files.insert(finalFilePath);
                            jsonFile = cJSON_CreateString(finalFilePath);
                            cJSON_AddItemToObject(jsonData, "file", jsonFile);
                            
                            // switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
                            //                  "(%s) processMessage - G.711 A-law WAV file created: %s (8000 Hz, %u bytes, %zu samples)\n",
                            //                  m_sessionId.c_str(), finalFilePath, dataSize, outputSamples.size());
                        } else {
                            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
                                              "(%s) processMessage - failed to create WAV file: %s\n",
                                              m_sessionId.c_str(), finalFilePath);
                        }
                    } else {
                        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_WARNING,
                                          "(%s) processMessage - unsupported audio format, only raw audio is supported\n",
                                          m_sessionId.c_str());
                    }
                } else {
                    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_WARNING,
                                      "(%s) processMessage - skipped file creation: jsonAudio=%s, fileType=%s\n",
                                      m_sessionId.c_str(),
                                      jsonAudio ? "valid" : "NULL",
                                      fileType.empty() ? "EMPTY" : fileType.c_str());
                }

                if(jsonFile) {
                    char *jsonString = cJSON_PrintUnformatted(jsonData);
                    
                    // switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
                    //                   "(%s) processMessage - firing EVENT_PLAY: %s\n",
                    //                  m_sessionId.c_str(), jsonString);
                    
                    m_notify(session, EVENT_PLAY, jsonString);
                    message.assign(jsonString);
                    free(jsonString);
                    status = SWITCH_TRUE;
                } else {
                    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_WARNING,
                                      "(%s) processMessage - jsonFile is NULL, EVENT_PLAY not fired\n",
                                      m_sessionId.c_str());
                }
                if (jsonAudio)
                    cJSON_Delete(jsonAudio);

            } else {
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "(%s) processMessage - no data in streamAudio\n", m_sessionId.c_str());
            }
        }
        cJSON_Delete(json);
        return status;
    }

    ~AudioStreamer()= default;

    void disconnect() {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "disconnecting...\n");
        client.disconnect();
    }

    bool isConnected() {
        return client.isConnected();
    }

    void writeBinary(uint8_t* buffer, size_t len) {
        if(!this->isConnected()) return;
        client.sendBinary(buffer, len);
    }

    void writeText(const char* text) {
        if(!this->isConnected()) return;
        client.sendMessage(text, strlen(text));
    }

    void deleteFiles() {
        if(m_playFile >0) {
            for (const auto &fileName: m_Files) {
                remove(fileName.c_str());
            }
        }
    }

private:
    std::string m_sessionId;
    responseHandler_t m_notify;
    WebSocketClient client;
    bool m_suppress_log;
    const char* m_extra_headers;
    int m_playFile;
    std::unordered_set<std::string> m_Files;
};


// 前置声明
extern "C" {
    void stream_play_frame(switch_media_bug_t *bug, private_t *tech_pvt);
}

namespace {

    switch_status_t stream_data_init(private_t *tech_pvt, switch_core_session_t *session, char *wsUri,
                                     uint32_t sampling, int desiredSampling, int channels, char *metadata, responseHandler_t responseHandler,
                                     int deflate, int heart_beat, bool suppressLog, int rtp_packets, const char* extra_headers,
                                     bool no_reconnect, const char *tls_cafile, const char *tls_keyfile,
                                     const char *tls_certfile, bool tls_disable_hostname_validation)
    {
        int err; //speex

        switch_memory_pool_t *pool = switch_core_session_get_pool(session);

        memset(tech_pvt, 0, sizeof(private_t));

        strncpy(tech_pvt->sessionId, switch_core_session_get_uuid(session), MAX_SESSION_ID);
        strncpy(tech_pvt->ws_uri, wsUri, MAX_WS_URI);
        tech_pvt->sampling = sampling;  // 使用通话采样率，不是 desiredSampling
        tech_pvt->responseHandler = responseHandler;
        tech_pvt->rtp_packets = rtp_packets;
        tech_pvt->channels = channels;
        tech_pvt->audio_paused = 0;
        
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
            "(%s) ========== 初始化 ==========\n", tech_pvt->sessionId);
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
            "(%s) 通话采样率: %u Hz (tech_pvt->sampling)\n", tech_pvt->sessionId, sampling);
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
            "(%s) 用户指定采样率: %d Hz (desiredSampling)\n", tech_pvt->sessionId, desiredSampling);

        if (metadata) strncpy(tech_pvt->initialMetadata, metadata, MAX_METADATA_LEN);

        //size_t buflen = (FRAME_SIZE_8000 * desiredSampling / 8000 * channels * 1000 / RTP_PERIOD * BUFFERED_SEC);
        const size_t buflen = (FRAME_SIZE_8000 * desiredSampling / 8000 * channels * rtp_packets);

        auto* as = new AudioStreamer(tech_pvt->sessionId, wsUri, responseHandler, deflate, heart_beat,
                                        suppressLog, extra_headers, no_reconnect,
                                        tls_cafile, tls_keyfile, tls_certfile, tls_disable_hostname_validation);

        tech_pvt->pAudioStreamer = static_cast<void *>(as);

        switch_mutex_init(&tech_pvt->mutex, SWITCH_MUTEX_NESTED, pool);
        
        if (switch_buffer_create(pool, &tech_pvt->sbuffer, buflen) != SWITCH_STATUS_SUCCESS) {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
                "%s: Error creating switch buffer.\n", tech_pvt->sessionId);
            return SWITCH_STATUS_FALSE;
        }
        
        // 初始化播放缓冲区（300秒缓冲，用于流式播放，防止突发音频丢失）
        const size_t play_buflen = sampling * channels * sizeof(int16_t) * 300;
        if (switch_buffer_create(pool, &tech_pvt->play_buffer, play_buflen) != SWITCH_STATUS_SUCCESS) {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
                "%s: Error creating play buffer.\n", tech_pvt->sessionId);
            return SWITCH_STATUS_FALSE;
        }
        
        // 初始化播放互斥锁
        switch_mutex_init(&tech_pvt->play_mutex, SWITCH_MUTEX_NESTED, pool);
        
        // 默认启用流式播放
        tech_pvt->stream_play_enabled = 1;
        tech_pvt->play_thread_running = 0;
        tech_pvt->last_buffer_inuse = 0;
        tech_pvt->interrupt_count = 0;
        tech_pvt->last_interrupt_time = 0;

        tech_pvt->write_frame_data = (uint8_t*)switch_core_session_alloc(session, SWITCH_RECOMMENDED_BUFFER_SIZE);
        if (!tech_pvt->write_frame_data) {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
                "%s: Failed to allocate write frame buffer.\n", tech_pvt->sessionId);
            return SWITCH_STATUS_FALSE;
        }
        memset(&tech_pvt->write_frame, 0, sizeof(tech_pvt->write_frame));
        tech_pvt->write_frame.data = tech_pvt->write_frame_data;
        tech_pvt->write_frame.buflen = SWITCH_RECOMMENDED_BUFFER_SIZE;
        tech_pvt->write_frame.rate = sampling;  // 使用通话采样率
        tech_pvt->write_frame.channels = channels;


        // 注意：这里的 resampler 是用于上行音频（READ 方向）的重采样
        // desiredSampling 是用户指定的采样率，sampling 是通话采样率
        if (desiredSampling != sampling) {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, 
                "(%s) 上行重采样: %u Hz → %u Hz\n", tech_pvt->sessionId, sampling, desiredSampling);
            tech_pvt->resampler = speex_resampler_init(channels, sampling, desiredSampling, SWITCH_RESAMPLE_QUALITY, &err);
            if (0 != err) {
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, 
                    "Error initializing resampler: %s.\n", speex_resampler_strerror(err));
                return SWITCH_STATUS_FALSE;
            }
        }
        else {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, 
                "(%s) 上行无需重采样: %u Hz\n", tech_pvt->sessionId, sampling);
        }

        return SWITCH_STATUS_SUCCESS;
    }

    void destroy_tech_pvt(private_t* tech_pvt) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "%s destroy_tech_pvt\n", tech_pvt->sessionId);
        
        if (tech_pvt->resampler) {
            speex_resampler_destroy(tech_pvt->resampler);
            tech_pvt->resampler = nullptr;
        }
        if (tech_pvt->mutex) {
            switch_mutex_destroy(tech_pvt->mutex);
            tech_pvt->mutex = nullptr;
        }
        if (tech_pvt->play_mutex) {
            switch_mutex_destroy(tech_pvt->play_mutex);
            tech_pvt->play_mutex = nullptr;
        }
        if (tech_pvt->play_buffer) {
            switch_buffer_destroy(&tech_pvt->play_buffer);
            tech_pvt->play_buffer = nullptr;
        }
        if (tech_pvt->pAudioStreamer) {
            auto* as = (AudioStreamer *) tech_pvt->pAudioStreamer;
            delete as;
            tech_pvt->pAudioStreamer = nullptr;
        }
    }

    void finish(private_t* tech_pvt) {
        std::shared_ptr<AudioStreamer> aStreamer;
        aStreamer.reset((AudioStreamer *)tech_pvt->pAudioStreamer);
        tech_pvt->pAudioStreamer = nullptr;

        std::thread t([aStreamer]{
            aStreamer->disconnect();
        });
        t.detach();
    }

}

extern "C" {
    // 流式播放函数：从播放缓冲区读取音频并注入到通话中（WRITE_REPLACE）
    // 播放格式: 16-bit Linear PCM, 通话采样率, 单声道, 小端序, 20ms 帧
    void stream_play_frame(switch_media_bug_t *bug, private_t *tech_pvt) {
        switch_core_session_t *session = switch_core_media_bug_get_session(bug);
        
        if (!tech_pvt) {
            if (session) {
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_WARNING,
                                  "stream_play_frame: tech_pvt is NULL\n");
            }
            return;
        }
        
        if (!tech_pvt->play_buffer) {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_WARNING,
                              "(%s) stream_play_frame: play_buffer is NULL\n",
                              tech_pvt->sessionId);
            return;
        }
        
        if (!tech_pvt->stream_play_enabled) {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG,
                              "(%s) stream_play_frame: stream_play_enabled=0, 跳过\n",
                              tech_pvt->sessionId);
            return;
        }

        if (!session) {
            return;
        }

        // 从 media bug 获取写方向的替换帧（播放给线路的音频）
        switch_frame_t *out_frame = switch_core_media_bug_get_write_replace_frame(bug);
        if (!out_frame) {
            out_frame = &tech_pvt->write_frame;
        }
        if (!out_frame->data || !out_frame->buflen) {
            return;
        }

        // 计算 20ms 帧长度 (16-bit PCM 格式)
        // 公式: samples_per_20ms = sampling_rate * 0.02
        //       bytes = samples * channels * sizeof(int16_t)
        size_t target_bytes = out_frame->datalen;
        if (!target_bytes) {
            // 标准 20ms 帧: 8000Hz=320字节, 16000Hz=640字节
            target_bytes = FRAME_SIZE_8000 * tech_pvt->sampling / 8000 * tech_pvt->channels;
        }
        if (target_bytes > out_frame->buflen) {
            target_bytes = out_frame->buflen;
        }

        bool injected = false;
        bool playback_ended = false;
        size_t start_inuse = 0;
        size_t read_size = 0;

        switch_mutex_lock(tech_pvt->play_mutex);
        size_t inuse = switch_buffer_inuse(tech_pvt->play_buffer);
        start_inuse = inuse;

        if (inuse > 0) {
            read_size = target_bytes;
            if (inuse < read_size) {
                read_size = inuse;
            }

            // 从缓冲区读取 16-bit Linear PCM 数据 (小端序)
            switch_buffer_read(tech_pvt->play_buffer, out_frame->data, read_size);
            injected = true;

            // 如果数据不足一帧，用静音填充 (0x0000 = 静音)
            if (read_size < target_bytes) {
                memset((uint8_t*)out_frame->data + read_size, 0, target_bytes - read_size);
            }

            // 设置帧参数 (FreeSWitch 播放格式)
            out_frame->datalen = target_bytes;
            out_frame->samples = target_bytes / (tech_pvt->channels * sizeof(int16_t));
            out_frame->rate = tech_pvt->sampling;        // 通话采样率
            out_frame->channels = tech_pvt->channels;    // 通话声道数

            // 更新上次缓冲区大小（使用 tech_pvt 成员变量，避免静态变量问题）
            size_t remaining = switch_buffer_inuse(tech_pvt->play_buffer);
            tech_pvt->last_buffer_inuse = remaining;
            if (remaining == 0) {
                playback_ended = true;
            }
        } else {
            // 缓冲区为空
            if (tech_pvt->last_buffer_inuse > 0) {
                playback_ended = true;
                tech_pvt->last_buffer_inuse = 0;
            }
        }
    
        switch_mutex_unlock(tech_pvt->play_mutex);

        if (playback_ended) {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
                              "(%s) 音频播放完成，缓冲区已空\n", tech_pvt->sessionId);
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG,
                              "(%s) 播放缓冲区耗尽: before=%zu bytes, read=%zu bytes, target=%zu bytes\n",
                              tech_pvt->sessionId, start_inuse, read_size, target_bytes);

            // 发送播放完成消息给WebSocket服务端
            auto *pAudioStreamer = static_cast<AudioStreamer *>(tech_pvt->pAudioStreamer);
            if (pAudioStreamer) {
                if (pAudioStreamer->isConnected()) {
                    const char* playback_ended_msg = "{\"action\":\"voice_playback_ended\"}";
                    pAudioStreamer->writeText(playback_ended_msg);
                    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
                                      "(%s) 已发送播放完成消息: %s\n",
                                      tech_pvt->sessionId, playback_ended_msg);
                } else {
                    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_WARNING,
                                      "(%s) WebSocket未连接，无法发送播放完成消息\n",
                                      tech_pvt->sessionId);
                }
            } else {
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_WARNING,
                                  "(%s) pAudioStreamer为空，无法发送播放完成消息\n",
                                  tech_pvt->sessionId);
            }
        }

        if (!injected) {
            // 缓冲区为空，不替换音频，保持原始通话音频透传
            return;
        }
        
        // 设置替换帧，让本次写方向（播放给线路）使用我们准备的音频
        // FreeSWitch 会自动处理后续的编码 (G.711/Opus/etc.)
        switch_core_media_bug_set_write_replace_frame(bug, out_frame);
    }
    
    int validate_ws_uri(const char* url, char* wsUri) {
        const char* scheme = nullptr;
        const char* hostStart = nullptr;
        const char* hostEnd = nullptr;
        const char* portStart = nullptr;

        // Check scheme
        if (strncmp(url, "ws://", 5) == 0) {
            scheme = "ws";
            hostStart = url + 5;
        } else if (strncmp(url, "wss://", 6) == 0) {
            scheme = "wss";
            hostStart = url + 6;
        } else {
            return 0;
        }

        // Find host end or port start
        hostEnd = hostStart;
        while (*hostEnd && *hostEnd != ':' && *hostEnd != '/') {
            if (!std::isalnum(*hostEnd) && *hostEnd != '-' && *hostEnd != '.') {
                return 0;
            }
            ++hostEnd;
        }

        // Check if host is empty
        if (hostStart == hostEnd) {
            return 0;
        }

        // Check for port
        if (*hostEnd == ':') {
            portStart = hostEnd + 1;
            while (*portStart && *portStart != '/') {
                if (!std::isdigit(*portStart)) {
                    return 0;
                }
                ++portStart;
            }
        }

        // Copy valid URI to wsUri
        std::strncpy(wsUri, url, MAX_WS_URI);
        return 1;
    }

    switch_status_t is_valid_utf8(const char *str) {
        switch_status_t status = SWITCH_STATUS_FALSE;
        while (*str) {
            if ((*str & 0x80) == 0x00) {
                // 1-byte character
                str++;
            } else if ((*str & 0xE0) == 0xC0) {
                // 2-byte character
                if ((str[1] & 0xC0) != 0x80) {
                    return status;
                }
                str += 2;
            } else if ((*str & 0xF0) == 0xE0) {
                // 3-byte character
                if ((str[1] & 0xC0) != 0x80 || (str[2] & 0xC0) != 0x80) {
                    return status;
                }
                str += 3;
            } else if ((*str & 0xF8) == 0xF0) {
                // 4-byte character
                if ((str[1] & 0xC0) != 0x80 || (str[2] & 0xC0) != 0x80 || (str[3] & 0xC0) != 0x80) {
                    return status;
                }
                str += 4;
            } else {
                // invalid character
                return status;
            }
        }
        return SWITCH_STATUS_SUCCESS;
    }

    switch_status_t stream_session_send_text(switch_core_session_t *session, char* text) {
        switch_channel_t *channel = switch_core_session_get_channel(session);
        auto *bug = (switch_media_bug_t*) switch_channel_get_private(channel, MY_BUG_NAME);
        if (!bug) {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "stream_session_send_text failed because no bug\n");
            return SWITCH_STATUS_FALSE;
        }
        auto *tech_pvt = (private_t*) switch_core_media_bug_get_user_data(bug);

        if (!tech_pvt) return SWITCH_STATUS_FALSE;
        auto *pAudioStreamer = static_cast<AudioStreamer *>(tech_pvt->pAudioStreamer);
        if (pAudioStreamer && text) pAudioStreamer->writeText(text);

        return SWITCH_STATUS_SUCCESS;
    }

    switch_status_t stream_session_pauseresume(switch_core_session_t *session, int pause) {
        switch_channel_t *channel = switch_core_session_get_channel(session);
        auto *bug = (switch_media_bug_t*) switch_channel_get_private(channel, MY_BUG_NAME);
        if (!bug) {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "stream_session_pauseresume failed because no bug\n");
            return SWITCH_STATUS_FALSE;
        }
        auto *tech_pvt = (private_t*) switch_core_media_bug_get_user_data(bug);

        if (!tech_pvt) return SWITCH_STATUS_FALSE;

        switch_core_media_bug_flush(bug);
        tech_pvt->audio_paused = pause;
        return SWITCH_STATUS_SUCCESS;
    }

    switch_status_t stream_session_init(switch_core_session_t *session,
                                        responseHandler_t responseHandler,
                                        uint32_t samples_per_second,
                                        char *wsUri,
                                        int sampling,
                                        int channels,
                                        char* metadata,
                                        void **ppUserData)
    {
        int deflate, heart_beat;
        bool suppressLog = false;
        const char* buffer_size;
        const char* extra_headers;
        int rtp_packets = 1; //20ms burst
        bool no_reconnect = false;
        const char* tls_cafile = NULL;;
        const char* tls_keyfile = NULL;;
        const char* tls_certfile = NULL;;
        bool tls_disable_hostname_validation = false;

        switch_channel_t *channel = switch_core_session_get_channel(session);

        if (switch_channel_var_true(channel, "STREAM_MESSAGE_DEFLATE")) {
            deflate = 1;
        }

        if (switch_channel_var_true(channel, "STREAM_SUPPRESS_LOG")) {
            suppressLog = true;
        }

        if (switch_channel_var_true(channel, "STREAM_NO_RECONNECT")) {
            no_reconnect = true;
        }

        tls_cafile = switch_channel_get_variable(channel, "STREAM_TLS_CA_FILE");
        tls_keyfile = switch_channel_get_variable(channel, "STREAM_TLS_KEY_FILE");
        tls_certfile = switch_channel_get_variable(channel, "STREAM_TLS_CERT_FILE");

        if (switch_channel_var_true(channel, "STREAM_TLS_DISABLE_HOSTNAME_VALIDATION")) {
            tls_disable_hostname_validation = true;
        }

        const char* heartBeat = switch_channel_get_variable(channel, "STREAM_HEART_BEAT");
        if (heartBeat) {
            char *endptr;
            long value = strtol(heartBeat, &endptr, 10);
            if (*endptr == '\0' && value <= INT_MAX && value >= INT_MIN) {
                heart_beat = (int) value;
            }
        }

        if ((buffer_size = switch_channel_get_variable(channel, "STREAM_BUFFER_SIZE"))) {
            int bSize = atoi(buffer_size);
            if(bSize % 20 != 0) {
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_WARNING, "%s: Buffer size of %s is not a multiple of 20ms. Using default 20ms.\n",
                                  switch_channel_get_name(channel), buffer_size);
            } else if(bSize >= 20){
                rtp_packets = bSize/20;
            }
        }

        extra_headers = switch_channel_get_variable(channel, "STREAM_EXTRA_HEADERS");

        // allocate per-session tech_pvt
        auto* tech_pvt = (private_t *) switch_core_session_alloc(session, sizeof(private_t));

        if (!tech_pvt) {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "error allocating memory!\n");
            return SWITCH_STATUS_FALSE;
        }
        if (SWITCH_STATUS_SUCCESS != stream_data_init(tech_pvt, session, wsUri, samples_per_second, sampling, channels, metadata, responseHandler, deflate, heart_beat,
                                                        suppressLog, rtp_packets, extra_headers, no_reconnect, tls_cafile, tls_keyfile, tls_certfile, tls_disable_hostname_validation)) {
            destroy_tech_pvt(tech_pvt);
            return SWITCH_STATUS_FALSE;
        }

        *ppUserData = tech_pvt;

        return SWITCH_STATUS_SUCCESS;
    }

    switch_bool_t stream_frame(switch_media_bug_t *bug) {
        auto *tech_pvt = (private_t *)switch_core_media_bug_get_user_data(bug);
        if (!tech_pvt || tech_pvt->audio_paused) return SWITCH_TRUE;
        /*
        auto flush_sbuffer = [&]() {
            switch_size_t inuse = switch_buffer_inuse(tech_pvt->sbuffer);
            if (inuse > 0) {
                std::vector<uint8_t> tmp(inuse);
                switch_buffer_read(tech_pvt->sbuffer, tmp.data(), inuse);
                switch_buffer_zero(tech_pvt->sbuffer);
                pAudioStreamer->writeBinary(tmp.data(), inuse);
            }
        };
        */
        if (switch_mutex_trylock(tech_pvt->mutex) == SWITCH_STATUS_SUCCESS) {

            if (!tech_pvt->pAudioStreamer) {
                switch_mutex_unlock(tech_pvt->mutex);
                return SWITCH_TRUE;
            }

            auto *pAudioStreamer = static_cast<AudioStreamer *>(tech_pvt->pAudioStreamer);

            if (!pAudioStreamer->isConnected()) {
                switch_mutex_unlock(tech_pvt->mutex);
                return SWITCH_TRUE;
            }

            if (nullptr == tech_pvt->resampler) {
                
                uint8_t data_buf[SWITCH_RECOMMENDED_BUFFER_SIZE];
                switch_frame_t frame = {0};
                frame.data = data_buf;
                frame.buflen = SWITCH_RECOMMENDED_BUFFER_SIZE;
                size_t available = switch_buffer_freespace(tech_pvt->sbuffer);

                while (switch_core_media_bug_read(bug, &frame, SWITCH_TRUE) == SWITCH_STATUS_SUCCESS) {
                    if (frame.datalen) {
                        if (1 == tech_pvt->rtp_packets) {
                            pAudioStreamer->writeBinary((uint8_t *) frame.data, frame.datalen);
                            continue;
                        }
                        if (available >= frame.datalen) {
                            switch_buffer_write(tech_pvt->sbuffer, static_cast<uint8_t *>(frame.data), frame.datalen);
                        }
                        if (0 == switch_buffer_freespace(tech_pvt->sbuffer)) {
                            switch_size_t inuse = switch_buffer_inuse(tech_pvt->sbuffer);
                            if (inuse > 0) {
                                std::vector<uint8_t> tmp(inuse);
                                switch_buffer_read(tech_pvt->sbuffer, tmp.data(), inuse);
                                switch_buffer_zero(tech_pvt->sbuffer);
                                pAudioStreamer->writeBinary(tmp.data(), inuse);
                            }
                        }
                    }
                }
                
            } else {

                uint8_t data[SWITCH_RECOMMENDED_BUFFER_SIZE];
                switch_frame_t frame = {};
                frame.data = data;
                frame.buflen = SWITCH_RECOMMENDED_BUFFER_SIZE;
                const size_t available = switch_buffer_freespace(tech_pvt->sbuffer);

                while (switch_core_media_bug_read(bug, &frame, SWITCH_TRUE) == SWITCH_STATUS_SUCCESS) {
                    if(frame.datalen) {
                        spx_uint32_t in_len = frame.samples;
                        spx_uint32_t out_len = (available / (tech_pvt->channels * sizeof(spx_int16_t)));
                        spx_int16_t out[available / sizeof(spx_int16_t)];

                        if(tech_pvt->channels == 1) {
                            speex_resampler_process_int(tech_pvt->resampler,
                                            0,
                                            (const spx_int16_t *)frame.data,
                                            &in_len,
                                            &out[0],
                                            &out_len);
                        } else {
                            speex_resampler_process_interleaved_int(tech_pvt->resampler,
                                            (const spx_int16_t *)frame.data,
                                            &in_len,
                                            &out[0],
                                            &out_len);
                        }

                        if(out_len > 0) {
                            const size_t bytes_written = out_len * tech_pvt->channels * sizeof(spx_int16_t);
                            if (tech_pvt->rtp_packets == 1) { //20ms packet
                                pAudioStreamer->writeBinary((uint8_t *) out, bytes_written);
                                continue;
                            }
                            if (bytes_written <= available) {
                                switch_buffer_write(tech_pvt->sbuffer, (const uint8_t *)out, bytes_written);
                            }
                        }

                        if(switch_buffer_freespace(tech_pvt->sbuffer) == 0) {
                            switch_size_t inuse = switch_buffer_inuse(tech_pvt->sbuffer);
                            if (inuse > 0) {
                                std::vector<uint8_t> tmp(inuse);
                                switch_buffer_read(tech_pvt->sbuffer, tmp.data(), inuse);
                                switch_buffer_zero(tech_pvt->sbuffer);
                                pAudioStreamer->writeBinary(tmp.data(), inuse);
                            }
                        }
                    }
                }
            }
            
            switch_mutex_unlock(tech_pvt->mutex);
        }

        return SWITCH_TRUE;
    }

    switch_status_t stream_session_cleanup(switch_core_session_t *session, char* text, int channelIsClosing) {
        switch_channel_t *channel = switch_core_session_get_channel(session);
        auto *bug = (switch_media_bug_t*) switch_channel_get_private(channel, MY_BUG_NAME);
        if(bug)
        {
            auto* tech_pvt = (private_t*) switch_core_media_bug_get_user_data(bug);
            char sessionId[MAX_SESSION_ID];
            strcpy(sessionId, tech_pvt->sessionId);

            switch_mutex_lock(tech_pvt->mutex);
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "(%s) stream_session_cleanup\n", sessionId);

            switch_channel_set_private(channel, MY_BUG_NAME, nullptr);
            if (!channelIsClosing) {
                switch_core_media_bug_remove(session, &bug);
            }

            auto* audioStreamer = (AudioStreamer *) tech_pvt->pAudioStreamer;
            if(audioStreamer) {
                audioStreamer->deleteFiles();
                if (text) audioStreamer->writeText(text);
                finish(tech_pvt);
            }

            destroy_tech_pvt(tech_pvt);

            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "(%s) stream_session_cleanup: connection closed\n", sessionId);
            return SWITCH_STATUS_SUCCESS;
        }

        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "stream_session_cleanup: no bug - websocket connection already closed\n");
        return SWITCH_STATUS_FALSE;
    }
}
