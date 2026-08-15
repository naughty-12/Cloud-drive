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
