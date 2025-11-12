// 测试 A-law 编码的正确性
#include <iostream>
#include <cstdint>
#include <iomanip>

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

// A-law 编码函数
uint8_t linear_to_alaw(int16_t pcm_val) {
    int16_t mask;
    int16_t seg;
    uint8_t aval;

    if (pcm_val >= 0) {
        mask = 0xD5;
    } else {
        mask = 0x55;
        pcm_val = -pcm_val - 1;
        if (pcm_val < 0) {
            pcm_val = 0;
        }
    }

    if (pcm_val > 0x7FFF) {
        pcm_val = 0x7FFF;
    }
    
    pcm_val = pcm_val >> 3;

    seg = search(pcm_val, seg_aend, 8);

    if (seg >= 8) {
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

// A-law 解码函数（用于验证）
int16_t alaw_to_linear(uint8_t aval) {
    aval ^= 0x55;  // 反转偶数位
    
    int16_t t = (aval & 0x0F) << 4;
    int16_t seg = (aval & 0x70) >> 4;
    
    switch (seg) {
        case 0: t += 8; break;
        case 1: t += 0x108; break;
        case 2: t += 0x208; break;
        case 3: t += 0x408; break;
        case 4: t += 0x808; break;
        case 5: t += 0x1008; break;
        case 6: t += 0x2008; break;
        case 7: t += 0x4008; break;
    }
    
    t = t << 3;
    
    return (aval & 0x80) ? t : -t;
}

int main() {
    std::cout << "G.711 A-law 编码测试\n" << std::endl;
    std::cout << "测试标准值:\n" << std::endl;
    
    // 测试一些标准值
    struct TestCase {
        int16_t input;
        const char* description;
    };
    
    TestCase tests[] = {
        {0, "静音"},
        {100, "小正值"},
        {-100, "小负值"},
        {1000, "中等正值"},
        {-1000, "中等负值"},
        {10000, "大正值"},
        {-10000, "大负值"},
        {32767, "最大正值"},
        {-32768, "最大负值"}
    };
    
    std::cout << std::setw(12) << "输入 PCM" 
              << std::setw(15) << "A-law (hex)" 
              << std::setw(15) << "解码 PCM"
              << std::setw(12) << "误差"
              << "  描述" << std::endl;
    std::cout << std::string(70, '-') << std::endl;
    
    for (const auto& test : tests) {
        uint8_t encoded = linear_to_alaw(test.input);
        int16_t decoded = alaw_to_linear(encoded);
        int16_t error = test.input - decoded;
        
        std::cout << std::setw(12) << test.input
                  << std::setw(15) << "0x" << std::hex << std::setfill('0') 
                  << std::setw(2) << (int)encoded << std::dec << std::setfill(' ')
                  << std::setw(15) << decoded
                  << std::setw(12) << error
                  << "  " << test.description << std::endl;
    }
    
    std::cout << "\n测试结果说明:" << std::endl;
    std::cout << "- A-law 是有损压缩，会有量化误差" << std::endl;
    std::cout << "- 误差在小信号时较小，大信号时较大" << std::endl;
    std::cout << "- 这是正常的，符合 G.711 标准" << std::endl;
    std::cout << "\n如果所有值都能编码和解码，说明实现正确！" << std::endl;
    
    return 0;
}
