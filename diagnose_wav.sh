#!/bin/bash
# WAV 文件诊断脚本

if [ $# -eq 0 ]; then
    echo "用法: $0 <wav文件>"
    echo "示例: $0 output.wav"
    exit 1
fi

WAV_FILE="$1"

if [ ! -f "$WAV_FILE" ]; then
    echo "错误: 文件不存在: $WAV_FILE"
    exit 1
fi

echo "=========================================="
echo "WAV 文件诊断: $WAV_FILE"
echo "=========================================="
echo

echo "1. 文件类型检测:"
file "$WAV_FILE"
echo

echo "2. 文件大小:"
ls -lh "$WAV_FILE" | awk '{print $5}'
echo

echo "3. 文件头十六进制 (前 48 字节):"
hexdump -C "$WAV_FILE" | head -n 3
echo

echo "4. 关键字段解析:"
# 读取格式码 (偏移 20-21)
FORMAT=$(hexdump -s 20 -n 2 -e '1/2 "%d\n"' "$WAV_FILE")
echo "   格式码: $FORMAT"
case $FORMAT in
    1) echo "   -> PCM (未压缩)" ;;
    6) echo "   -> G.711 A-law ✓" ;;
    7) echo "   -> G.711 μ-law" ;;
    *) echo "   -> 其他格式" ;;
esac

# 读取声道数 (偏移 22-23)
CHANNELS=$(hexdump -s 22 -n 2 -e '1/2 "%d\n"' "$WAV_FILE")
echo "   声道数: $CHANNELS"

# 读取采样率 (偏移 24-27)
SAMPLE_RATE=$(hexdump -s 24 -n 4 -e '1/4 "%d\n"' "$WAV_FILE")
echo "   采样率: $SAMPLE_RATE Hz"

# 读取位深度 (偏移 34-35)
BITS=$(hexdump -s 34 -n 2 -e '1/2 "%d\n"' "$WAV_FILE")
echo "   位深度: $BITS bit"
echo

echo "5. 标准检查:"
ERRORS=0

if [ $FORMAT -ne 6 ]; then
    echo "   ✗ 格式码应该是 6 (A-law)，当前是 $FORMAT"
    ERRORS=$((ERRORS + 1))
else
    echo "   ✓ 格式码正确 (A-law)"
fi

if [ $CHANNELS -ne 1 ]; then
    echo "   ✗ 声道数应该是 1，当前是 $CHANNELS"
    ERRORS=$((ERRORS + 1))
else
    echo "   ✓ 声道数正确 (单声道)"
fi

if [ $SAMPLE_RATE -ne 8000 ]; then
    echo "   ✗ 采样率应该是 8000 Hz，当前是 $SAMPLE_RATE Hz"
    ERRORS=$((ERRORS + 1))
else
    echo "   ✓ 采样率正确 (8000 Hz)"
fi

if [ $BITS -ne 8 ]; then
    echo "   ✗ 位深度应该是 8 bit，当前是 $BITS bit"
    ERRORS=$((ERRORS + 1))
else
    echo "   ✓ 位深度正确 (8 bit)"
fi

echo

if [ $ERRORS -eq 0 ]; then
    echo "=========================================="
    echo "结果: 文件格式完全正确 ✓"
    echo "=========================================="
else
    echo "=========================================="
    echo "结果: 发现 $ERRORS 个问题 ✗"
    echo "=========================================="
fi

echo

# 如果安装了 soxi，显示详细信息
if command -v soxi &> /dev/null; then
    echo "6. SoX 详细信息:"
    soxi "$WAV_FILE"
    echo
fi

# 如果安装了 ffprobe，显示详细信息
if command -v ffprobe &> /dev/null; then
    echo "7. FFmpeg 详细信息:"
    ffprobe -hide_banner "$WAV_FILE" 2>&1 | grep -E "(Stream|Duration|Audio)"
    echo
fi

echo "提示: 如果格式正确但仍有杂音，可能是编码算法问题"
echo "运行 ./test_alaw 测试编码器的正确性"
