# Claude 使用指南

## 需求沟通方式

在与 Claude 沟通需求时，应该通过 **AskUserQuestion** 工具进行交互式问答，而不是让 Claude 自行假设或猜测需求。

### 为什么使用 AskUserQuestion

- **明确需求**：通过结构化的问题确保需求理解准确
- **减少误解**：避免基于假设的开发导致返工
- **提高效率**：一次性收集所有必要信息
- **选项清晰**：提供多个选项让用户选择，而不是开放式提问

### 使用场景

1. **不确定实现方式**：当有多种技术方案可选时
2. **需要用户决策**：需要用户在不同方案之间做选择时
3. **缺少关键信息**：缺少必要的参数、配置或上下文信息时
4. **验证理解**：确认对需求的理解是否正确时

### 使用示例

#### 示例 1：技术方案选择

```typescript
// 用户需求："我想添加音频处理功能"
// Claude 应该询问：

AskUserQuestion({
  questions: [{
    header: "音频处理方式",
    multiSelect: false,
    options: [
      {
        label: "实时流式处理",
        description: "边接收边处理，延迟低，适合实时通话场景"
      },
      {
        label: "批量处理",
        description: "接收完整音频后处理，可做复杂分析"
      },
      {
        label: "混合模式",
        description: "结合两种方式，灵活性高"
      }
    ],
    question: "你希望使用哪种音频处理方式？"
  }]
})
```

#### 示例 2：参数配置确认

```typescript
// 用户需求："优化音频采样率"
// Claude 应该询问：

AskUserQuestion({
  questions: [{
    header: "采样率选择",
    multiSelect: false,
    options: [
      {
        label: "8kHz（推荐）",
        description: "电话语音质量，文件小，适合语音通话"
      },
      {
        label: "16kHz",
        description: "宽带语音质量，清晰度更高"
      },
      {
        label: "48kHz",
        description: "专业音频质量，文件大"
      }
    ],
    question: "你希望使用什么采样率？"
  }]
})
```

#### 示例 3：多个问题组合

```typescript
// 用户需求："添加音频转码功能"
// Claude 可以一次询问多个相关问题：

AskUserQuestion({
  questions: [
    {
      header: "输入格式",
      multiSelect: true,  // 允许多选
      options: [
        {label: "PCM", description: "原始音频格式"},
        {label: "Opus", description: "高效压缩格式"},
        {label: "MP3", description: "通用格式"}
      ],
      question: "需要支持哪些输入格式？"
    },
    {
      header: "输出格式",
      multiSelect: false,
      options: [
        {label: "PCM 16-bit", description: "无损质量"},
        {label: "Opus", description: "压缩传输"}
      ],
      question: "输出格式使用哪种？"
    }
  ]
})
```

### 最佳实践

1. **问题要具体**：避免过于宽泛的问题
2. **选项要明确**：每个选项都应有清晰的描述和适用场景
3. **合理分组**：相关的问题可以一起询问（最多 4 个问题）
4. **提供建议**：可以标注推荐选项（添加 "（推荐）" 后缀）
5. **说明影响**：在描述中说明选择该选项的影响或权衡

### 注意事项

- 每次最多询问 4 个问题
- 每个问题提供 2-4 个选项
- header 应简短（最多 12 字符）
- 始终提供 description 解释每个选项
- 使用 multiSelect 允许多选（当选项不互斥时）

## 总结

通过 AskUserQuestion 进行需求沟通，可以确保：
- ✅ 需求理解准确
- ✅ 技术方案符合预期
- ✅ 减少返工和修改
- ✅ 提高开发效率

记住：**有疑问时，先问清楚，再动手！**
