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
#   当前由使用方自行决定部署位置 (server 侧随可执行文件放到工作目录);
#   单测工程在 unit_tests.pro 里用 QMAKE_POST_LINK 拷贝到运行目录。
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
