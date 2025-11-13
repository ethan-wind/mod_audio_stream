// 独立的 PCM 到 G.711 A-law WAV 转换工具
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <speex/speex_resampler.h>

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

// G.711 A-law 编码 - 标准 ITU-T G.711 实现
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

// 读取 WAV 文件头
struct WavHeader {
    char riff[4];
    uint32_t fileSize;
    char wave[4];
    char fmt[4];
    uint32_t fmtSize;
    uint16_t audioFormat;
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
};

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "用法: " << argv[0] << " <输入PCM WAV文件> <输出A-law WAV文件>" << std::endl;
        std::cerr << "示例: " << argv[0] << " input.wav output_alaw.wav" << std::endl;
        return 1;
    }

    const char* inputFile = argv[1];
    const char* outputFile = argv[2];

    // 打开输入文件
    std::ifstream inFile(inputFile, std::ios::binary);
    if (!inFile) {
        std::cerr << "错误: 无法打开输入文件 " << inputFile << std::endl;
        return 1;
    }

    // 读取 WAV 头
    WavHeader header;
    inFile.read(reinterpret_cast<char*>(&header), sizeof(WavHeader));

    // 验证文件格式
    if (std::strncmp(header.riff, "RIFF", 4) != 0 || 
        std::strncmp(header.wave, "WAVE", 4) != 0) {
        std::cerr << "错误: 不是有效的 WAV 文件" << std::endl;
        return 1;
    }

    if (header.audioFormat != 1) {
        std::cerr << "错误: 输入文件必须是 PCM 格式 (format=1)" << std::endl;
        return 1;
    }

    if (header.bitsPerSample != 16) {
        std::cerr << "错误: 输入文件必须是 16-bit PCM" << std::endl;
        return 1;
    }

    std::cout << "输入文件信息:" << std::endl;
    std::cout << "  采样率: " << header.sampleRate << " Hz" << std::endl;
    std::cout << "  声道数: " << header.numChannels << std::endl;
    std::cout << "  位深度: " << header.bitsPerSample << " bit" << std::endl;

    // 查找 data chunk
    char chunkId[4];
    uint32_t chunkSize;
    while (inFile.read(chunkId, 4)) {
        inFile.read(reinterpret_cast<char*>(&chunkSize), 4);
        if (std::strncmp(chunkId, "data", 4) == 0) {
            break;
        }
        inFile.seekg(chunkSize, std::ios::cur);
    }

    // 读取 PCM 数据
    std::vector<int16_t> pcmData(chunkSize / sizeof(int16_t));
    inFile.read(reinterpret_cast<char*>(pcmData.data()), chunkSize);
    inFile.close();

    std::cout << "读取了 " << pcmData.size() << " 个样本" << std::endl;

    // 重采样到 8000 Hz（如果需要）
    std::vector<int16_t> resampledData;
    uint32_t outputSampleRate = 8000;
    
    if (header.sampleRate != outputSampleRate) {
        std::cout << "重采样: " << header.sampleRate << " Hz -> " << outputSampleRate << " Hz" << std::endl;
        
        int err;
        SpeexResamplerState* resampler = speex_resampler_init(
            header.numChannels,
            header.sampleRate,
            outputSampleRate,
            10,  // 质量等级 (0-10, 10最高)
            &err
        );
        
        if (err != 0 || !resampler) {
            std::cerr << "错误: 无法初始化重采样器, 错误码: " << err << std::endl;
            return 1;
        }
        
        // 计算输出样本数
        uint32_t inputSamples = pcmData.size() / header.numChannels;
        uint32_t outputSamples = (uint32_t)((uint64_t)inputSamples * outputSampleRate / header.sampleRate);
        
        resampledData.resize(outputSamples * header.numChannels);
        
        if (header.numChannels == 1) {
            // 单声道
            spx_uint32_t in_len = inputSamples;
            spx_uint32_t out_len = outputSamples;
            
            speex_resampler_process_int(
                resampler,
                0,
                pcmData.data(),
                &in_len,
                resampledData.data(),
                &out_len
            );
            
            resampledData.resize(out_len);
            std::cout << "重采样完成: " << in_len << " -> " << out_len << " 样本" << std::endl;
        } else {
            // 多声道交错
            spx_uint32_t in_len = inputSamples;
            spx_uint32_t out_len = outputSamples;
            
            speex_resampler_process_interleaved_int(
                resampler,
                pcmData.data(),
                &in_len,
                resampledData.data(),
                &out_len
            );
            
            resampledData.resize(out_len * header.numChannels);
            std::cout << "重采样完成: " << in_len << " -> " << out_len << " 样本/声道" << std::endl;
        }
        
        speex_resampler_destroy(resampler);
    } else {
        std::cout << "采样率已经是 8000 Hz，无需重采样" << std::endl;
        resampledData = pcmData;
    }

    // 转换为 A-law
    std::vector<uint8_t> alawData(resampledData.size());
    for (size_t i = 0; i < resampledData.size(); i++) {
        alawData[i] = linear_to_alaw(resampledData[i]);
    }

    // 写入 A-law WAV 文件
    std::ofstream outFile(outputFile, std::ios::binary);
    if (!outFile) {
        std::cerr << "错误: 无法创建输出文件 " << outputFile << std::endl;
        return 1;
    }

    uint32_t dataSize = alawData.size();
    uint32_t fileSize = 36 + dataSize;
    uint16_t bitsPerSample = 8;
    uint32_t byteRate = outputSampleRate * header.numChannels * bitsPerSample / 8;
    uint16_t blockAlign = header.numChannels * bitsPerSample / 8;

    // RIFF header (小端序)
    outFile.write("RIFF", 4);
    write_le32(outFile, fileSize);
    outFile.write("WAVE", 4);

    // fmt chunk (小端序)
    outFile.write("fmt ", 4);
    write_le32(outFile, 16);  // fmtSize
    write_le16(outFile, 6);   // audioFormat: G.711 A-law
    write_le16(outFile, header.numChannels);
    write_le32(outFile, outputSampleRate);
    write_le32(outFile, byteRate);
    write_le16(outFile, blockAlign);
    write_le16(outFile, bitsPerSample);

    // data chunk (小端序)
    outFile.write("data", 4);
    write_le32(outFile, dataSize);
    outFile.write(reinterpret_cast<char*>(alawData.data()), dataSize);

    outFile.close();

    std::cout << "成功创建 G.711 A-law WAV 文件: " << outputFile << std::endl;
    std::cout << "  格式: G.711 A-law" << std::endl;
    std::cout << "  采样率: " << outputSampleRate << " Hz" << std::endl;
    std::cout << "  声道数: " << header.numChannels << std::endl;
    std::cout << "  位深度: 8 bit" << std::endl;
    std::cout << "  文件大小: " << (fileSize + 8) << " 字节" << std::endl;

    return 0;
}
