# mod_audio_stream

A FreeSWITCH module that streams L16 audio from a channel to a websocket endpoint. If websocket sends back responses (eg. JSON) it can be effectively used with ASR engines such as IBM Watson etc., or any other purpose you find applicable.

### Update (22/2/2025)

#### :rocket: **Introducing Bi-Directional Streaming with automatic playback**

A new version `mod-audio-stream v1.0.3` has been published, featuring **raw binary stream** from the websocket.
It can be downloaded from the **Releases** section (pre-release) and comes as a pre-built Debian 12 package.

- Playback feature allows continuous forward streaming while the playback runs independently.
- It is a **full-duplex streamer** between the caller and the websocket.
- It supports **base64 encoded audio** as well as the **raw binary stream** from the websocket.
- Playback can be **tracked, paused, or resumed** dynamically.

:small_blue_diamond: This release is a commercial product that is available for **free use**, including commercial use, with a limitation of **10 concurrent streaming channels**. For users requiring more than 10 channels, or access to the source code, please [contact us](mailto:amsoftswitch@gmail.com)
 for further information and licensing options.

### About

- The purpose of `mod_audio_stream` was to provide a simple, low-dependency yet effective module for streaming audio and receiving responses from a websocket server.
- Introduced [libwsc](https://github.com/amigniter/libwsc), our in-house, **RFC-6455 compliant** websocket client developed specifically for `mod_audio_stream`.
  - Replaces [ixwebsocket](https://machinezone.github.io/IXWebSocket/), which served us well for the past few years. `libwsc` is libevent-based, extremely lightweight, and optimized for low-latency audio streaming.
- This module was inspired by mod_audio_fork.

## Installation

### Dependencies
It requires `libfreeswitch-dev`, `libssl-dev`, `zlib1g-dev`, `libevent-dev` and `libspeexdsp-dev` on Debian/Ubuntu which are regular packages for Freeswitch installation.
### Building
After cloning please execute: **git submodule init** and **git submodule update** to initialize the submodule.
#### Custom path
If you built FreeSWITCH from source, eq. install dir is /usr/local/freeswitch, add path to pkgconfig:
```
export PKG_CONFIG_PATH=/usr/local/freeswitch/lib/pkgconfig
```
To build the module, from the cloned repository:
```
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
sudo make install
```
**TLS** is `OFF` by default. To build with TLS support add `-DUSE_TLS=ON` to cmake line.

#### DEB Package
To build DEB package after making the module:
```
cpack -G DEB
```
Debian package will be placed in root directory `_packages` folder.

## Scripted Build & Installation

```
sudo apt-get -y install git \
    && cd /usr/src/ \
    && git clone https://github.com/amigniter/mod_audio_stream.git \
    && cd mod_audio_stream \
    && sudo bash ./build-mod-audio-stream.sh
```

### Channel variables
The following channel variables can be used to fine tune websocket connection and also configure mod_audio_stream logging:

| Variable                               | Description                                             | Default |
| -------------------------------------- | ------------------------------------------------------- | ------- |
| STREAM_MESSAGE_DEFLATE                 | true or 1, disables per message deflate                 | off     |
| STREAM_HEART_BEAT                      | number of seconds, interval to send the heart beat      | off     |
| STREAM_SUPPRESS_LOG                    | true or 1, suppresses printing to log                   | off     |
| STREAM_BUFFER_SIZE                     | buffer duration in milliseconds, divisible by 20        | 20      |
| STREAM_EXTRA_HEADERS                   | JSON object for additional headers in string format     | none    |
| ~~STREAM_NO_RECONNECT~~                    | true or 1, disables automatic websocket reconnection    | off     |
| STREAM_TLS_CA_FILE                     | CA cert or bundle, or the special values SYSTEM or NONE | SYSTEM  |
| STREAM_TLS_KEY_FILE                    | optional client key for WSS connections                 | none    |
| STREAM_TLS_CERT_FILE                   | optional client cert for WSS connections                | none    |
| STREAM_TLS_DISABLE_HOSTNAME_VALIDATION | true or 1 disable hostname check in WSS connections     | false   |

- Per message deflate compression option is enabled by default. It can lead to a very nice bandwidth savings. To disable it set the channel var to `true|1`.
- Heart beat, sent every xx seconds when there is no traffic to make sure that load balancers do not kill an idle connection.
- Suppress parameter is omitted by default(false). All the responses from websocket server will be printed to the log. Not to flood the log you can suppress it by setting the value to `true|1`. Events are fired still, it only affects printing to the log.
- `Buffer Size` actually represents a duration of audio chunk sent to websocket. If you want to send e.g. 100ms audio packets to your ws endpoint
you would set this variable to 100. If ommited, default packet size of 20ms will be sent as grabbed from the audio channel (which is default FreeSWITCH frame size)
- Extra headers should be a JSON object with key-value pairs representing additional HTTP headers. Each key should be a header name, and its corresponding value should be a string.
  ```json
  {
      "Header1": "Value1",
      "Header2": "Value2",
      "Header3": "Value3"
  }
- ~~Websocket automatic reconnection is on by default. To disable it set this channel variable to true or 1.~~
  - libwsc does not support automatic reconnection.
- TLS (for WSS) options can be fine tuned with the `STREAM_TLS_*` channel variables:
  - `STREAM_TLS_CA_FILE` the ca certificate (or certificate bundle) file. By default is `SYSTEM` which means use the system defaults.
Can be `NONE` which result in no peer verification.
  - `STREAM_TLS_CERT_FILE` optional client tls certificate file sent to the server.
  - `STREAM_TLS_KEY_FILE` optional client tls key file for the given certificate.
  - `STREAM_TLS_DISABLE_HOSTNAME_VALIDATION` if `true`, disables the check of the hostname against the peer server certificate.
Defaults to `false`, which enforces hostname match with the peer certificate.

## API

### Commands
The freeswitch module exposes the following API commands:

```
uuid_audio_stream <uuid> start <wss-url> <mix-type> <sampling-rate> <metadata>
```
Attaches a media bug and starts streaming audio (in L16 format) to the websocket server. FS default is 8k. If sampling-rate is other than 8k it will be resampled.
- `uuid` - Freeswitch channel unique id
- `wss-url` - websocket url `ws://` or `wss://`
- `mix-type` - choice of 
  - "mono" - single channel containing caller's audio
  - "mixed" - single channel containing both caller and callee audio
  - "stereo" - two channels with caller audio in one and callee audio in the other.
- `sampling-rate` - choice of
  - "8k" = 8000 Hz sample rate will be generated
  - "16k" = 16000 Hz sample rate will be generated
- `metadata` - (optional) a valid `utf-8` text to send. It will be sent the first before audio streaming starts.

```
uuid_audio_stream <uuid> send_text <metadata>
```
Sends a text to the websocket server. Requires a valid `utf-8` text.

```
uuid_audio_stream <uuid> stop <metadata>
```
Stops audio stream and closes websocket connection. If _metadata_ is provided it will be sent before the connection is closed.

```
uuid_audio_stream <uuid> pause
```
Pauses audio stream

```
uuid_audio_stream <uuid> resume
```
Resumes audio stream

## Events
Module will generate the following event types:
- `mod_audio_stream::json`
- `mod_audio_stream::connect`
- `mod_audio_stream::disconnect`
- `mod_audio_stream::error`
- `mod_audio_stream::play`

### response
Message received from websocket endpoint. Json expected, but it contains whatever the websocket server's response is.
#### Freeswitch event generated
**Name**: mod_audio_stream::json
**Body**: WebSocket server response

### connect
Successfully connected to websocket server.
#### Freeswitch event generated
**Name**: mod_audio_stream::connect
**Body**: JSON
```json
{
	"status": "connected"
}
```

### disconnect
Disconnected from websocket server.
#### Freeswitch event generated
**Name**: mod_audio_stream::disconnect
**Body**: JSON
```json
{
	"status": "disconnected",
	"message": {
		"code": 1000,
		"reason": "Normal closure"
	}
}
```
- code: `<int>`
- reason: `<string>`

### error
There is an error with the connection. Multiple fields will be available on the event to describe the error.
#### Freeswitch event generated
**Name**: mod_audio_stream::error
**Body**: JSON
```json
{
	"status": "error",
	"message": {
		"code": 1,
		"error": "String explaining the error"
	}
}
```
- code: `<int>`
- error: `<string>`

#### Possible `code` values

| Code | Enum Name             | Meaning                                              |
|:----:|:----------------------|:-----------------------------------------------------|
| 1    | `IO`                  | I/O error when reading/writing sockets               |
| 2    | `INVALID_HEADER`      | Server sent a malformed WebSocket header             |
| 3    | `SERVER_MASKED`       | Server frames were masked (not allowed by spec)      |
| 4    | `NOT_SUPPORTED`       | Requested feature (e.g. extension) not supported     |
| 5    | `PING_TIMEOUT`        | No PONG received within timeout                      |
| 6    | `CONNECT_FAILED`      | TCP connection or DNS lookup failed                  |
| 7    | `TLS_INIT_FAILED`     | Couldn't initialize SSL/TLS context                  |
| 8    | `SSL_HANDSHAKE_FAILED`| SSL/TLS handshake with server failed                 |
| 9    | `SSL_ERROR`           | Generic OpenSSL error (certificate, cipher, etc.)    |


### play
**Name**: mod_audio_stream::play
**Body**: JSON

Websocket server may return JSON object containing base64 encoded audio to be played by the user. To use this feature, response must follow the format:
```json
{
  "type": "streamAudio",
  "data": {
    "audioDataType": "raw",
    "sampleRate": 8000,
    "audioData": "base64 encoded audio"
  }
}
```
- audioDataType: `<raw|wav|mp3|ogg>`

Event generated by the module (subclass: _mod_audio_stream::play_) will be the same as the `data` element with the **file** added to it representing filePath:
```json
{
  "audioDataType": "raw",
  "sampleRate": 8000,
  "file": "/path/to/the/file"
}
```
If printing to the log is not suppressed, `response` printed to the console will look the same as the event. The original response containing base64 encoded audio is replaced because it can be quite huge.

All the files generated by this feature will reside at the temp directory and will be deleted when the session is closed.

---

# Technical Documentation

## WebSocket Binary Audio Support

### Overview
This module supports receiving raw PCM S16BE (16-bit Signed Big Endian) binary audio streams directly from WebSocket, in addition to the traditional Base64-encoded JSON format.

### Audio Format Requirements

**Binary Stream Format:**
- Encoding: PCM S16BE (16-bit Signed Big Endian)
- Sample Rate: 8000 Hz (default, configurable)
- Channels: Mono (1 channel)
- Byte Order: Big Endian
- No Base64 encoding
- No JSON wrapper

**JSON Format (still supported):**
```json
{
  "type": "streamAudio",
  "data": {
    "audioData": "base64_encoded_audio_data",
    "audioDataType": "raw",
    "sampleRate": 8000
  }
}
```

### Message Type Detection

The module automatically detects message type using a smart detection algorithm:

1. **JSON Detection**: Checks if the first non-whitespace character is `{` or `[`
2. **Binary Detection**: All other messages are treated as binary audio

### Audio Processing Pipeline

```
WebSocket Binary Data (PCM S16BE, 8000 Hz)
    ↓
Big Endian → Little Endian Conversion
    ↓
PCM S16LE (8000 Hz, Mono)
    ↓
No Resampling Needed (already 8000 Hz)
    ↓
Play Buffer
    ↓
WRITE_REPLACE Injection
    ↓
FreeSWITCH Encoding (G.711/Opus/etc.)
    ↓
RTP Transmission to Remote Party
```

### Usage Example

**Python WebSocket Server:**
```python
import asyncio
import websockets
import numpy as np

async def send_audio(websocket, path):
    # Generate 440Hz sine wave at 8000Hz
    sample_rate = 8000
    duration = 1.0
    frequency = 440
    
    t = np.linspace(0, duration, int(sample_rate * duration), False)
    audio = np.sin(2 * np.pi * frequency * t)
    audio_int16 = (audio * 32767).astype(np.int16)
    
    # Convert to Big Endian
    audio_bytes = audio_int16.astype('>i2').tobytes()
    
    # Send binary data
    await websocket.send(audio_bytes)

start_server = websockets.serve(send_audio, "localhost", 8765)
asyncio.get_event_loop().run_until_complete(start_server)
asyncio.get_event_loop().run_forever()
```

**Node.js WebSocket Server:**
```javascript
const WebSocket = require('ws');
const wss = new WebSocket.Server({ port: 8765 });

wss.on('connection', (ws) => {
    const sampleRate = 8000;
    const duration = 1.0;
    const frequency = 440;
    const numSamples = Math.floor(sampleRate * duration);
    
    const buffer = Buffer.alloc(numSamples * 2);
    
    for (let i = 0; i < numSamples; i++) {
        const t = i / sampleRate;
        const sample = Math.sin(2 * Math.PI * frequency * t);
        const value = Math.floor(sample * 32767);
        buffer.writeInt16BE(value, i * 2);
    }
    
    ws.send(buffer, { binary: true });
});
```

## Audio Format Conversion

### Complete Conversion Flow

1. **WebSocket Reception**
   - Format: 32-bit Float or 16-bit PCM S16BE
   - Sample Rate: 8000 Hz
   - Channels: Mono
   - Encoding: Base64 (for JSON) or Raw Binary

2. **Float32 → 16-bit PCM Conversion** (if needed)
   ```cpp
   float sample = float_data[i];
   if (sample > 1.0f) sample = 1.0f;
   if (sample < -1.0f) sample = -1.0f;
   pcm16bit[i] = static_cast<int16_t>(sample * 32767.0f);
   ```

3. **No Resampling Needed**
   - WebSocket audio is already 8000 Hz
   - Matches call sample rate directly
   - No CPU overhead from resampling

4. **Play Buffer Write**
   - Format: 16-bit Linear PCM
   - Sample Rate: Matches call session
   - Buffer Size: 10 seconds (configurable)

5. **WRITE_REPLACE Injection**
   - Frame Size: 20ms (standard RTP frame)
   - 8000 Hz: 320 bytes (160 samples)
   - 16000 Hz: 640 bytes (320 samples)

### Format Comparison Table

| Stage | Format | Sample Rate | Bit Depth | Bytes/Sample | Byte Order | Data Size (1s) |
|-------|--------|-------------|-----------|--------------|------------|----------------|
| WebSocket | 32-bit Float | 8000 Hz | 32-bit | 4 bytes | Little Endian | 32,000 bytes |
| After Float→PCM | 16-bit PCM | 8000 Hz | 16-bit | 2 bytes | Little Endian | 16,000 bytes |
| No Resampling | 16-bit PCM | 8000 Hz | 16-bit | 2 bytes | Little Endian | 16,000 bytes |
| Play Buffer | 16-bit PCM | 8000 Hz | 16-bit | 2 bytes | Little Endian | 16,000 bytes |
| WRITE_REPLACE | 16-bit PCM | 8000 Hz | 16-bit | 2 bytes | Little Endian | 16,000 bytes |

## Streaming Playback Architecture

### Call Architecture
```
SIP Phone/Gateway <--SIP--> FreeSWITCH <--WebSocket--> AI Service
```

### Audio Directions

**Upstream (Capture):**
```
Line → FreeSWITCH → WebSocket
```
- Captures audio from the line via READ callback
- Sends to AI service for processing

**Downstream (Playback):**
```
WebSocket → FreeSWITCH → Line
```
- Receives TTS audio from AI service
- Plays to line via WRITE_REPLACE mechanism

### FreeSWITCH Media Bug Directions

- **READ Direction**: Capture audio from line (Line → FS)
- **WRITE Direction**: Play audio to line (FS → Line)

### Implementation Details

**Audio Reception and Buffering:**
```cpp
// Receive Float32 audio (8000 Hz)
// Convert to 16-bit PCM
// No resampling needed (already 8000 Hz)
// Write to play buffer
switch_buffer_write(tech_pvt->play_buffer,
                   (uint8_t*)playbackSamples.data(),
                   data_size);
```

**Playback Mechanism:**
```cpp
case SWITCH_ABC_TYPE_WRITE_REPLACE:
    if (tech_pvt->stream_play_enabled) {
        stream_play_frame(bug, tech_pvt);
    }
    return SWITCH_TRUE;
```

**Core Function: stream_play_frame()**
```cpp
void stream_play_frame(switch_media_bug_t *bug, private_t *tech_pvt) {
    // Get write replace frame
    switch_frame_t *out_frame = switch_core_media_bug_get_write_replace_frame(bug);
    
    // Read from play buffer
    switch_buffer_read(tech_pvt->play_buffer, out_frame->data, read_size);
    
    // Set frame parameters
    out_frame->datalen = target_bytes;
    out_frame->samples = target_bytes / (channels * sizeof(int16_t));
    out_frame->rate = sampling;
    out_frame->channels = channels;
    
    // Set replace frame
    switch_core_media_bug_set_write_replace_frame(bug, out_frame);
}
```

### Key Configuration

**Media Bug Flags:**
```c
flags |= SMBF_READ_STREAM;      // Capture upstream audio
flags |= SMBF_WRITE_STREAM;     // Monitor downstream audio
flags |= SMBF_WRITE_REPLACE;    // Replace downstream audio
```

**Buffer Configuration:**
```cpp
// 10 second play buffer
const size_t play_buflen = desiredSampling * channels * sizeof(int16_t) * 10;
```

## Audio Distortion Fix

### Common Causes of Distortion

1. Audio overload (peaks exceed ±1.0)
2. Excessive gain
3. Hard clipping
4. Sample rate mismatch
5. Incorrect data format

### Solutions

**1. Lower Target Peak (Recommended)**
```javascript
session.setVariable("STREAM_AUTO_GAIN", "true");
session.setVariable("STREAM_TARGET_PEAK", "0.7");  // Lower to 0.7
```

**2. Enable Soft Clipping (Recommended)**
```javascript
session.setVariable("STREAM_SOFT_CLIP", "true");
```

Soft clipping uses tanh function for smooth transitions:
```
Hard Clipping: 1.5 → 1.0 (abrupt, causes distortion)
Soft Clipping: 1.5 → 0.96 (smooth, reduces distortion)
```

**3. Manual Gain Control**
```javascript
session.setVariable("STREAM_GAIN", "0.5");  // 0.5x gain (reduce volume)
```

**4. Combined Approach (Best Practice)**

For small volume with distortion:
```javascript
session.setVariable("STREAM_AUTO_GAIN", "true");
session.setVariable("STREAM_TARGET_PEAK", "0.7");
session.setVariable("STREAM_SOFT_CLIP", "true");
```

For normal volume with distortion:
```javascript
session.setVariable("STREAM_GAIN", "0.8");
session.setVariable("STREAM_SOFT_CLIP", "true");
```

### Configuration Parameters

**STREAM_AUTO_GAIN**
- Type: Boolean (true/false)
- Default: false
- Enables automatic gain control

**STREAM_TARGET_PEAK**
- Type: Float (0.0 ~ 1.0)
- Default: 0.8
- Sets target peak for auto gain

**STREAM_SOFT_CLIP**
- Type: Boolean (true/false)
- Default: false
- Enables soft clipping to reduce distortion

**STREAM_GAIN**
- Type: Float (0.0 ~ 100.0)
- Default: 1.0
- Manual gain multiplier (disables auto gain)

### Diagnosing Distortion

Check logs for key indicators:

**Normal Audio:**
```
Peak: 0.8 | Gain: 1.00x | Clipping: 0 (0.0%)
```

**Slight Distortion:**
```
Peak: 1.2 | Gain: 1.00x | Clipping: 50 (2.1%)
```

**Severe Distortion:**
```
Peak: 2.5 | Gain: 1.00x | Clipping: 500 (20.8%)
```

### Recommended Configuration

**Production (Conservative):**
```javascript
session.setVariable("STREAM_AUTO_GAIN", "true");
session.setVariable("STREAM_TARGET_PEAK", "0.7");
session.setVariable("STREAM_SOFT_CLIP", "true");
```

**Known Overloaded Audio:**
```javascript
session.setVariable("STREAM_GAIN", "0.5");
session.setVariable("STREAM_SOFT_CLIP", "true");
```

## Troubleshooting

### Common Issues

**1. No Audio**
- Check WebSocket connection
- Verify binary message format
- Check play buffer status

**2. Distorted Audio**
- Enable soft clipping
- Reduce gain or target peak
- Check for audio overload

**3. Choppy Audio**
- Increase buffer size
- Check network latency
- Verify send rate

**4. Wrong Speed**
- Verify sample rate configuration
- Check resampling ratio

### Debug Logging

Enable detailed logging:
```
fsctl loglevel DEBUG
```

View audio stream logs:
```bash
grep "接收二进制音频\|Injected audio" /var/log/freeswitch/freeswitch.log
```

### Performance Considerations

**CPU Usage:**
- Float → PCM conversion: O(n) linear
- SpeexDSP resampling: Optimized DSP algorithms
- Buffer operations: Memory copy overhead

**Memory Usage:**
- Play buffer: 160 KB @ 8kHz (10 seconds)
- Temporary buffers: Managed by std::vector

**Latency:**
- Buffer latency: Configurable (default 10s max)
- Resampling latency: < 1ms
- Total latency: Mainly network and buffer strategy

## Related Files

- `mod_audio_stream.c` - Module main file, callback handling
- `audio_streamer_glue.cpp` - Audio processing and playback logic
- `audio_streamer_glue.h` - Header definitions
- `mod_audio_stream.h` - Data structure definitions
- `libs/libwsc` - WebSocket client library (submodule)

## References

- [FreeSWITCH Media Bug API](https://freeswitch.org/confluence/display/FREESWITCH/Media+Bug+API)
- [SpeexDSP Resampler](https://www.speex.org/docs/api/speex-api-reference/group__Resampler.html)
- [RFC 6455 - WebSocket Protocol](https://tools.ietf.org/html/rfc6455)
