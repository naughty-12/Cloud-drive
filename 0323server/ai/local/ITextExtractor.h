#ifndef ITEXTEXTRACTOR_H
#define ITEXTEXTRACTOR_H

/**
 * @file ITextExtractor.h
 * @brief 文件 → 纯文本 的可插拔解析器接口。
 *
 * 定位 (见 .dsh-dev/specs/2026-09-07-local-engine-first-design.md 5.2):
 *   搜索 / 预览 / 标签 三个功能只依赖此接口取文本, 不关心文件格式;
 *   新增格式 = 新增一个实现类并注册一行, 三功能同时受益 (开闭原则)。
 *
 * 当前实现: TxtTextExtractor (纯文本族)。
 * 将来可扩展: docx / pdf / 图片 OCR —— 未实现时 canHandle 返回 false,
 *   上层自动退回"文件名 + 扩展名"的元数据兜底, 功能仍完整可用。
 *
 * 约定: 实现必须是纯本地计算 (不得联网 / 不得抛异常), 失败时返回空串,
 *   由调用方判定并回退。
 */

#include <string>

class ITextExtractor
{
public:
    virtual ~ITextExtractor() {}

    /// 能否处理该文件名 (按扩展名判定; 传入的可以是完整路径)。
    virtual bool canHandle(const std::string& fileName) const = 0;

    /// 从原始字节提取纯文本 (实现内部负责 BOM 剥离 / 截断等资源控制)。
    virtual std::string extract(const std::string& rawBytes) const = 0;
};

#endif // ITEXTEXTRACTOR_H
