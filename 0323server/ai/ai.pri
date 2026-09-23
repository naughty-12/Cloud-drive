QT += network

HEADERS += \
    $$PWD/APIBridge.h \
    $$PWD/json.hpp \
    $$PWD/AIServiceBase.h \
    $$PWD/AIFilePreview.h \
    $$PWD/AISearchSvc.h \
    $$PWD/AITagService.h

SOURCES += \
    $$PWD/APIBridge.cpp \
    $$PWD/AIFilePreview.cpp \
    $$PWD/AISearchSvc.cpp \
    $$PWD/AITagService.cpp

# 本地检索引擎 (AI 三件套的主路径, 无 Key 完整可用) —— 见 ai/local/local.pri
include($$PWD/local/local.pri)
