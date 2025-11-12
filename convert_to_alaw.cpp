// 独立的 PCM 到 G.711 A-law WAV 转换工具
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstring>

// G.711 A-law 编码表
const int16_t alaw_compress_table[128] = {
    1, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7
};

// 将 16-bit PCM 转换为 A-law
uint8_t linear_to_alaw(int16_t pcm_val) {
    int16_t mask;
    int16_t seg;
    uint8_t aval;

    pcm_val = pcm_val >> 3;

    if (pcm_val >= 0) {
        mask = 0xD5;
    } else {
        mask = 0x55;
        pcm_val = -pcm_val - 1;
    }

    if (pcm_val > 32635) pcm_val = 32635;

    if (pcm_val < 256) {
        aval = pcm_val >> 4;
    } else {
        seg = alaw_compress_table[(pcm_val >> 8) & 0x7F];
        aval = (seg << 4) | ((pcm_val >> (seg + 3)) & 0x0F);
    }

    return aval ^ mask;
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

    // 转换为 A-law
    std::vector<uint8_t> alawData(pcmData.size());
    for (size_t i = 0; i < pcmData.size(); i++) {
        alawData[i] = linear_to_alaw(pcmData[i]);
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
    uint32_t byteRate = header.sampleRate * header.numChannels * bitsPerSample / 8;
    uint16_t blockAlign = header.numChannels * bitsPerSample / 8;

    // RIFF header
    outFile.write("RIFF", 4);
    outFile.write(reinterpret_cast<char*>(&fileSize), 4);
    outFile.write("WAVE", 4);

    // fmt chunk
    outFile.write("fmt ", 4);
    uint32_t fmtSize = 16;
    outFile.write(reinterpret_cast<char*>(&fmtSize), 4);
    uint16_t audioFormat = 6;  // G.711 A-law
    outFile.write(reinterpret_cast<char*>(&audioFormat), 2);
    outFile.write(reinterpret_cast<char*>(&header.numChannels), 2);
    outFile.write(reinterpret_cast<char*>(&header.sampleRate), 4);
    outFile.write(reinterpret_cast<char*>(&byteRate), 4);
    outFile.write(reinterpret_cast<char*>(&blockAlign), 2);
    outFile.write(reinterpret_cast<char*>(&bitsPerSample), 2);

    // data chunk
    outFile.write("data", 4);
    outFile.write(reinterpret_cast<char*>(&dataSize), 4);
    outFile.write(reinterpret_cast<char*>(alawData.data()), dataSize);

    outFile.close();

    std::cout << "成功创建 G.711 A-law WAV 文件: " << outputFile << std::endl;
    std::cout << "  格式: G.711 A-law" << std::endl;
    std::cout << "  采样率: " << header.sampleRate << " Hz" << std::endl;
    std::cout << "  声道数: " << header.numChannels << std::endl;
    std::cout << "  位深度: 8 bit" << std::endl;
    std::cout << "  文件大小: " << (fileSize + 8) << " 字节" << std::endl;

    return 0;
}
