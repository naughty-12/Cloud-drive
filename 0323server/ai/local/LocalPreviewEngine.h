#ifndef LOCALPREVIEWENGINE_H
#define LOCALPREVIEWENGINE_H

/**
 * @file LocalPreviewEngine.h
 * @brief 文本统计式预览 —— 关键词 / 关键句 / 摘要, 纯本地计算 (无网络、无数据库、无第三方依赖)。
 *
 * 定位 (见 .dsh-dev/specs/2026-09-07-local-engine-first-design.md 4.1):
 *   智能预览的**主路径**。无 API Key 时也能给出有信息量的预览 (高频关键词 + 关键句 +
 *   首段摘要), 而不是"截断 200 字 / AI disabled"的空壳; LLM 只是可选增强, 成功时覆盖字段。
 *
 * 三项产出:
 *   - keywords     : LocalTokenize 词频 TopN (频次降序, 同频按首次出现), 逗号分隔;
 *   - keySentences : 按 。！？!? 与换行切句, 取含热点词最多的 1~3 句, 换行分隔;
 *   - summary      : 首段 (空行分隔) 折叠空白后, 截断 kSummaryChars 个 UTF-8 字符。
 *
 * 关键取舍:
 *   - 中文关键词是 2-gram 词元 (LocalTokenize 的近似分词), 会有 "是关/系数" 这类跨词
 *     噪声 —— 档位 A 的明示取舍: 召回优先、零依赖, 精度靠词频排序;
 *   - 关键句的"热点词"只认频次 ≥ 2 的词元 (一个都没有时退化为整个 TopN): 否则中文长文里
 *     每个只出现一次的跨词 n-gram 都算热点, 句子评分会退化成"谁排前面谁入选";
 *   - 摘要按 UTF-8 **字符**计数并落在字符边界上 (按 200 字节硬截断会切碎汉字, 客户端乱码);
 *   - 保证非空 (设计 5.4): 内容非空时 keySentences 至少含首句, 不返回空串。
 *
 * 线程安全: 纯函数 + 无可变状态, 可并发调用。
 */

#include <string>

/// 预览结果 —— 字段与协议 25 (AIPreviewRS) / 现有 AIFilePreview::PreviewResult 一一对应。
struct LocalPreviewResult
{
    std::string summary;        ///< 摘要: 首段, 折叠空白, 最多 kSummaryChars 个字符
    std::string keywords;       ///< 关键词: 逗号分隔, 最多 kKeywordTopN 个
    std::string keySentences;   ///< 关键句: 换行分隔, 1~kMaxKeySentences 句
    std::string fileType;       ///< 扩展名推断: "C++ 源码" / "文档" / "配置文件" / "文本"
};

class LocalPreviewEngine
{
public:
    /// 统计式预览。content 建议先经 ITextExtractor 提取 (已剥 BOM、限长 64KB)。
    /// 空内容 → summary/keywords/keySentences 为空串, fileType 仍按文件名给出。
    static LocalPreviewResult preview(const std::string& content, const std::string& fileName);

    static const size_t kKeywordTopN = 8;       ///< 关键词个数上限
    static const size_t kSummaryChars = 200;    ///< 摘要字符数上限 (UTF-8 字符, 非字节)
    static const size_t kMaxKeySentences = 3;   ///< 关键句条数上限
};

#endif // LOCALPREVIEWENGINE_H
