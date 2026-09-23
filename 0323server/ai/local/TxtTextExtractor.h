#ifndef TXTTEXTEXTRACTOR_H
#define TXTTEXTEXTRACTOR_H

/**
 * @file TxtTextExtractor.h
 * @brief 纯文本族解析器: .txt/.md/.json/.log/源码/配置/脚本 等直接返回原文。
 *
 * 处理内容:
 *   - 剥离 UTF-8 BOM (Windows 记事本保存的文本常带);
 *   - 截断到 kMaxExtract (只取前缀做统计, 控制内存与 CPU, 见设计 5.3)。
 */

#include "ITextExtractor.h"

class TxtTextExtractor : public ITextExtractor
{
public:
    bool canHandle(const std::string& fileName) const override;
    std::string extract(const std::string& rawBytes) const override;

    /// 单文件提取上限 64KB (超出按前缀统计)。
    static const size_t kMaxExtract = 64 * 1024;
};

#endif // TXTTEXTEXTRACTOR_H
