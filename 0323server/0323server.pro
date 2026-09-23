QT = core network

CONFIG += c++11 cmdline

INCLUDEPATH += ../shared/libprotocol
INCLUDEPATH += ../shared/log
INCLUDEPATH += ../shared/crypto

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
        main.cpp

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

include(./iocp/iocp.pri)
include(./kernel/kernel.pri)
include(../shared/crypto/crypto.pri)
include(./db/db.pri)
include(./db/replicationworker.pri)
include(./storage/storage.pri)
include(./cluster/cluster.pri)
include(./http/http.pri)
include(./ai/ai.pri)
include(../shared/log/log.pri)

# ---------------------------------------------------------------------------
# 本地检索引擎的标签词典 tag_dict.json（Task 9 遗留决策：词典部署）
# ---------------------------------------------------------------------------
# 运行期由 LocalTagEngine::loadDict 加载；tcpkernel::loadLocalTagDict() 按优先级探测：
#   ① exe 同目录（下面的构建期拷贝）/ 当前工作目录
#   ② 编译期源码绝对路径 LOCAL_TAG_DICT_PATH（开发形态，从任意工作目录启动都能命中）
#   ③ exe 目录下的源码相对回退 ../ai/local/tag_dict.json
# 全部失败 → 仅告警并退化为纯扩展名规则（不崩溃、标签仍非空）。
TAG_DICT_SRC = $$PWD/ai/local/tag_dict.json
DEFINES += LOCAL_TAG_DICT_PATH=\\\"$$clean_path($$TAG_DICT_SRC)\\\"
win32 {
    QMAKE_POST_LINK += -$$QMAKE_COPY \"$$shell_path($$TAG_DICT_SRC)\" \"$$shell_path($$OUT_PWD/release)\"
}

# OLD tcpserver and packdef.h deprecated
# Use shared/libprotocol/Packdef.h via INCLUDEPATH
LIBS += -L../shared/libprotocol/release -lprotocol
