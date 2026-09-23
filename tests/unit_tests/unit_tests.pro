# ============================================================================
# unit_tests.pro — 0323 Cloud Disk 单元测试工程 (Qt Test)
# ============================================================================
# 覆盖范围:
#   - BinaryStream       (shared/libprotocol — 二进制读写器)
#   - ProtocolFactory    (shared/libprotocol — 协议工厂, 全部 RQ/RS 往返)
#   - CryptoUtil         (shared/crypto — SHA-256, 文件/稀疏指纹, 密码哈希)
#   - StreamAccessController (0323server/http — 流媒体凭证签发 + 播放会话)
#   - LocalTokenize      (0323server/ai/local — 词元化: ASCII 整词 + 中文 2-gram + 停用词)
#   - TxtTextExtractor   (0323server/ai/local — 纯文本族解析器: 扩展名白名单 + BOM + 截断)
#   - LocalTextIndex     (0323server/ai/local — 内存倒排索引: 词元 → (fileId, 词频))
#   - LocalSearchEngine  (0323server/ai/local — TF-IDF 加权 + 余弦相似度排序)
#   - LocalPreviewEngine (0323server/ai/local — 统计式预览: 关键词/关键句/首段摘要)
#   - LocalTagEngine     (0323server/ai/local — 扩展名规则 + 词典命中标签)
#   - aijson             (0323server/ai — 轻量 JSON 解析: 紧凑/格式化 JSON 空白容忍度)
#
# 构建方式 (推荐走 scripts/run-tests.bat):
#   cd shared/libprotocol && qmake "CONFIG+=release" "CONFIG-=debug" && mingw32-make
#   cd ../../tests/unit_tests && qmake "CONFIG+=release" "CONFIG-=debug" && mingw32-make
#   release\unit_tests.exe
# ============================================================================

QT += core testlib
QT -= gui

CONFIG += console c++11
CONFIG -= app_bundle

TARGET = unit_tests
TEMPLATE = app

INCLUDEPATH += ../../shared/libprotocol
INCLUDEPATH += ../../shared/crypto
INCLUDEPATH += ../../0323server/http
INCLUDEPATH += ../../0323server/ai
INCLUDEPATH += ../../0323server/ai/local

SOURCES += \
    main.cpp \
    tst_binarystream.cpp \
    tst_crypto.cpp \
    tst_protocolfactory.cpp \
    tst_streamaccess.cpp \
    tst_localtokenize.cpp \
    tst_txtextractor.cpp \
    tst_localindex.cpp \
    tst_localsearch.cpp \
    tst_localpreview.cpp \
    tst_localtag.cpp \
    tst_aijson.cpp \
    ../../0323server/http/StreamAccessController.cpp \
    ../../0323server/ai/local/LocalTokenize.cpp \
    ../../0323server/ai/local/LocalTextIndex.cpp \
    ../../0323server/ai/local/LocalSearchEngine.cpp \
    ../../0323server/ai/local/LocalPreviewEngine.cpp \
    ../../0323server/ai/local/LocalTagEngine.cpp \
    ../../0323server/ai/local/TxtTextExtractor.cpp \
    ../../shared/crypto/CryptoUtil.cpp

HEADERS += \
    tst_binarystream.h \
    tst_crypto.h \
    tst_protocolfactory.h \
    tst_streamaccess.h \
    tst_localtokenize.h \
    tst_txtextractor.h \
    tst_localindex.h \
    tst_localsearch.h \
    tst_localpreview.h \
    tst_localtag.h \
    tst_aijson.h

# --- LocalTagEngine 的词典 tag_dict.json -------------------------------------
# 词典随代码存放在 0323server/ai/local/, 但运行时的工作目录不一定是那里, 故构建后
# 拷贝到构建目录 (与 release\ 子目录), 让 tst_localtag 能用相对路径加载;
# 测试同时内置源码绝对路径 (LOCAL_TAG_DICT_PATH, 正斜杠形式) 作为副本缺失时的回退。
# win32 之外无 QMAKE_COPY 语义保证, 只内置绝对路径。
TAG_DICT_REL = ../../0323server/ai/local/tag_dict.json
DEFINES += LOCAL_TAG_DICT_PATH=\\\"$$clean_path($$PWD/$$TAG_DICT_REL)\\\"
win32 {
    QMAKE_POST_LINK += -$$QMAKE_COPY \"$$shell_path($$PWD/$$TAG_DICT_REL)\" \"$$shell_path($$OUT_PWD)\" $$escape_expand(\\n\\t)
    QMAKE_POST_LINK += -$$QMAKE_COPY \"$$shell_path($$PWD/$$TAG_DICT_REL)\" \"$$shell_path($$OUT_PWD/release)\"
}

# libprotocol 静态库 (run-tests.bat 会先重新构建, 保证测试的是最新源码)
LIBS += -L../../shared/libprotocol/release -lprotocol
