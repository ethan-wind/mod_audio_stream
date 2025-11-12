// 验证 A-law 编码的正确性 - 与标准值对比
#include <iostream>
#include <cstdint>
#include <iomanip>
#include <cmath>

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

// A-law 编码
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

// A-law 解码（标准实现）
int16_t alaw_to_linear(uint8_t aval) {
    int16_t t;
    int16_t seg;

    aval ^= 0x55;
    
    t = (aval & 0x0F) << 4;
    seg = (aval & 0x70) >> 4;
    
    switch (seg) {
        case 0:
            t += 8;
            break;
        case 1:
            t += 0x108;
            break;
        default:
            t += 0x108;
            t <<= seg - 1;
    }
    
    return (aval & 0x80) ? t : -t;
}

// 生成正弦波测试
void test_sine_wave() {
    std::cout << "\n=== 正弦波测试 ===" << std::endl;
    std::cout << "生成 440Hz 正弦波，采样率 8000Hz" << std::endl;
    
    const int sample_rate = 8000;
    const int frequency = 440;
    const int num_samples = 100;
    
    double max_error = 0;
    double sum_error = 0;
    
    for (int i = 0; i < num_samples; i++) {
        // 生成正弦波样本
        double t = (double)i / sample_rate;
        double sine = sin(2.0 * M_PI * frequency * t);
        int16_t pcm = (int16_t)(sine * 32767.0);
        
        // 编码和解码
        uint8_t alaw = linear_to_alaw(pcm);
        int16_t decoded = alaw_to_linear(alaw);
        
        // 计算误差
        double error = fabs((double)(pcm - decoded));
        max_error = (error > max_error) ? error : max_error;
        sum_error += error;
        
        if (i < 10) {  // 显示前 10 个样本
            std::cout << "样本 " << std::setw(2) << i 
                      << ": PCM=" << std::setw(6) << pcm
                      << " A-law=0x" << std::hex << std::setw(2) << std::setfill('0') << (int)alaw
                      << std::dec << std::setfill(' ')
                      << " 解码=" << std::setw(6) << decoded
                      << " 误差=" << std::setw(4) << (pcm - decoded) << std::endl;
        }
    }
    
    std::cout << "\n统计信息:" << std::endl;
    std::cout << "  最大误差: " << max_error << std::endl;
    std::cout << "  平均误差: " << (sum_error / num_samples) << std::endl;
}

// 测试已知的标准值
void test_known_values() {
    std::cout << "\n=== 标准值测试 ===" << std::endl;
    
    struct TestCase {
        int16_t input;
        uint8_t expected;  // 预期的 A-law 值（如果知道的话）
        const char* description;
    };
    
    TestCase tests[] = {
        {0, 0xD5, "静音（0）"},
        {8, 0xD4, "最小正值"},
        {-8, 0x54, "最小负值"},
        {1000, 0xE5, "中等正值"},
        {-1000, 0x65, "中等负值"},
        {10000, 0xF9, "大正值"},
        {-10000, 0x79, "大负值"},
        {32767, 0xFF, "最大正值"},
        {-32768, 0x7F, "最大负值（饱和）"}
    };
    
    std::cout << std::setw(12) << "输入 PCM" 
              << std::setw(15) << "A-law (hex)" 
              << std::setw(15) << "预期值"
              << std::setw(10) << "匹配"
              << "  描述" << std::endl;
    std::cout << std::string(70, '-') << std::endl;
    
    int passed = 0;
    for (const auto& test : tests) {
        uint8_t encoded = linear_to_alaw(test.input);
        bool match = (encoded == test.expected);
        if (match) passed++;
        
        std::cout << std::setw(12) << test.input
                  << std::setw(15) << "0x" << std::hex << std::setfill('0') 
                  << std::setw(2) << (int)encoded << std::dec << std::setfill(' ')
                  << std::setw(15) << "0x" << std::hex << std::setfill('0')
                  << std::setw(2) << (int)test.expected << std::dec << std::setfill(' ')
                  << std::setw(10) << (match ? "✓" : "✗")
                  << "  " << test.description << std::endl;
    }
    
    std::cout << "\n通过: " << passed << "/" << (sizeof(tests)/sizeof(tests[0])) << std::endl;
}

// 测试对称性
void test_symmetry() {
    std::cout << "\n=== 对称性测试 ===" << std::endl;
    std::cout << "测试正负值的对称性" << std::endl;
    
    int16_t test_values[] = {100, 500, 1000, 5000, 10000, 20000, 32767};
    
    std::cout << std::setw(10) << "值" 
              << std::setw(15) << "正值 A-law"
              << std::setw(15) << "负值 A-law"
              << std::setw(12) << "对称性" << std::endl;
    std::cout << std::string(52, '-') << std::endl;
    
    for (int16_t val : test_values) {
        uint8_t pos = linear_to_alaw(val);
        uint8_t neg = linear_to_alaw(-val);
        
        // A-law 的对称性：正负值的编码应该只有符号位不同
        bool symmetric = ((pos ^ neg) == 0x80);
        
        std::cout << std::setw(10) << val
                  << std::setw(15) << "0x" << std::hex << std::setfill('0')
                  << std::setw(2) << (int)pos << std::dec << std::setfill(' ')
                  << std::setw(15) << "0x" << std::hex << std::setfill('0')
                  << std::setw(2) << (int)neg << std::dec << std::setfill(' ')
                  << std::setw(12) << (symmetric ? "✓" : "✗") << std::endl;
    }
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "G.711 A-law 编码验证程序" << std::endl;
    std::cout << "========================================" << std::endl;
    
    test_known_values();
    test_symmetry();
    test_sine_wave();
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "验证完成！" << std::endl;
    std::cout << "如果所有测试都通过，说明编码实现正确。" << std::endl;
    std::cout << "========================================" << std::endl;
    
    return 0;
}
