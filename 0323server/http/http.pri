
# WinSock2 只在 Windows 上需要；POSIX 侧的 socket 在 libc 里
win32: LIBS += -lws2_32
unix:  LIBS += -lpthread

HEADERS += \
    $$PWD/HttpServer.h \
    $$PWD/StreamAccessController.h

SOURCES += \
    $$PWD/HttpServer.cpp \
    $$PWD/StreamAccessController.cpp
