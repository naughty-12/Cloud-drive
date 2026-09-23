#ifndef LOCALTOKENIZE_H
#define LOCALTOKENIZE_H

/**
 * @file LocalTokenize.h
 * @brief 本地检索引擎的词元化 (分词) —— 零依赖、无网络、纯计算。
 *
 * 设计取舍 (见 .dsh-dev/specs/2026-09-07-local-engine-first-design.md 5.1):
 *   - ASCII 连续字母/数字/下划线 → 整词保留 (统一小写), 契合英文/标识符/代码;
 *   - 中文 (UTF-8 3 字节 BMP 字符) → 按 2-gram 滑窗产出词元, 无语义分词精度
 *     但能召回内容命中, 且不引入第三方分词库 (保持零依赖卖点);
 *   - 内置中英停用词表, 避免 "的/了/the/a" 之类高频词污染 TF-IDF 的 IDF。
 *
 * n-gram 层独立成此模块: 将来若接入真分词库, 只改这一个文件, 倒排索引/检索不受影响。
 */

#include <string>
#include <vector>

namespace LocalTokenize {

/// UTF-8 文本 → 词元列表 (已去停用词, 保序, 允许重复以便上游统计词频)。
std::vector<std::string> tokenize(const std::string& utf8Text);

/// 是否为内置停用词 (中英混排表, 精确匹配)。
bool isStopWord(const std::string& token);

} // namespace LocalTokenize

#endif // LOCALTOKENIZE_H
