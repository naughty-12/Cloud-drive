QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

# 平台相关链接库（WinSock2 仅 Windows；POSIX 侧 socket 在 libc 里）
win32: LIBS += -lws2_32
unix:  LIBS += -lpthread

INCLUDEPATH += ../shared/libprotocol
INCLUDEPATH += ../shared/log
INCLUDEPATH += ../shared/crypto

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    login1.cpp \
    main.cpp \
    TagCloud.cpp \
    widget.cpp

HEADERS += \
    login1.h \
    TagCloud.h \
    widget.h

FORMS += \
    login1.ui \
    widget.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

include(./kernel/kernel.pri)
include(./tcpclient/tcpclient.pri)
include(../shared/crypto/crypto.pri)
include(./cache/cache.pri)
include(../shared/log/log.pri)

# libprotocol static library
LIBS += -L../shared/libprotocol/release -lprotocol

RESOURCES += \
    login1.qrc
