# ============================================================================
# local.pri — ai/local 本地检索引擎源码清单
# ============================================================================
# 定位 (见 .dsh-dev/specs/2026-09-07-local-engine-first-design.md):
#   AI 三件套 (预览 / 搜索 / 标签) 的**主路径** —— 纯本地计算, 无网络、无数据库、
#   无 API Key 也完整可用; LLM (ai/*.h/cpp) 只作为可选增强层。
#
# 内容: 词元化 (LocalTokenize) / 可插拔解析器 (ITextExtractor + TxtTextExtractor) /
#   倒排索引 (LocalTextIndex) / TF-IDF 检索 (LocalSearchEngine) /
#   统计式预览 (LocalPreviewEngine) / 规则词典标签 (LocalTagEngine + tag_dict.json)。
#
# 依赖: 仅 C++11 标准库 + 项目内置 aijson (ai/json.hpp) —— 零第三方依赖。
# 由 ai/ai.pri 末尾 include (用 $$PWD 锚定路径, 不依赖 qmake 的相对路径基准)。
#
# 注意: tag_dict.json 是运行期文件 (LocalTagEngine::loadDict 的默认词典)。
#   部署策略 (2026-09-23 Task 9 定):
#     ① exe 同目录 / 当前工作目录 —— 由 0323server.pro 的 QMAKE_POST_LINK
#        构建期拷贝到 $$OUT_PWD/release (单测工程 unit_tests.pro 同样拷贝到其运行目录);
#     ② 编译期源码绝对路径 LOCAL_TAG_DICT_PATH (0323server.pro / unit_tests.pro 定义);
#     ③ exe 目录下的 ../ai/local/tag_dict.json 源码相对回退;
#     ④ 全部失败 → 仅告警并退化为纯扩展名规则 (不崩溃, 已知扩展名仍有非空标签)。
#   路径探测与告警在 tcpkernel::loadLocalTagDict()。
# ============================================================================

HEADERS += \
    $$PWD/ITextExtractor.h \
    $$PWD/TxtTextExtractor.h \
    $$PWD/LocalTokenize.h \
    $$PWD/LocalTextIndex.h \
    $$PWD/LocalSearchEngine.h \
    $$PWD/LocalPreviewEngine.h \
    $$PWD/LocalTagEngine.h

SOURCES += \
    $$PWD/TxtTextExtractor.cpp \
    $$PWD/LocalTokenize.cpp \
    $$PWD/LocalTextIndex.cpp \
    $$PWD/LocalSearchEngine.cpp \
    $$PWD/LocalPreviewEngine.cpp \
    $$PWD/LocalTagEngine.cpp

# 词典数据文件 (IDE 可见 / 打包清单; 运行期由使用方部署)
DISTFILES += $$PWD/tag_dict.json
