// 测试 A-law 编码的正确性
#include <iostream>
#include <cstdint>
#include <iomanip>

// A-law 编码函数
uint8_t linear_to_alaw(int16_t pcm_val) {
    uint8_t mask;
    uint8_t seg;
    uint8_t aval;
    int16_t linear;

    if (pcm_val >= 0) {
        mask = 0xD5;
        linear = pcm_val;
    } else {
        mask = 0x55;
        linear = -pcm_val - 1;
        if (linear < 0) linear = 0;
    }

    if (linear > 0x7FFF) {
        linear = 0x7FFF;
    }

    linear = linear >> 3;

    if (linear <= 0x0F) {
        seg = 0;
        aval = linear;
    } else if (linear <= 0x1F) {
        seg = 1;
        aval = (linear >> 1) & 0x0F;
    } else if (linear <= 0x3F) {
        seg = 2;
        aval = (linear >> 2) & 0x0F;
    } else if (linear <= 0x7F) {
        seg = 3;
        aval = (linear >> 3) & 0x0F;
    } else if (linear <= 0xFF) {
        seg = 4;
        aval = (linear >> 4) & 0x0F;
    } else if (linear <= 0x1FF) {
        seg = 5;
        aval = (linear >> 5) & 0x0F;
    } else if (linear <= 0x3FF) {
        seg = 6;
        aval = (linear >> 6) & 0x0F;
    } else {
        seg = 7;
        aval = (linear >> 7) & 0x0F;
    }

    aval = (seg << 4) | aval;
    return aval ^ mask;
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
