TEMPLATE = lib
CONFIG += staticlib c++11
TARGET = protocol

HEADERS += \
    BinaryStream.h \
    Packdef.h \
    ProtocolFactory.h

SOURCES += \
    BinaryStream.cpp \
    ProtocolFactory.cpp
