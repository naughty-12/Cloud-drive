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

# OLD tcpserver and packdef.h deprecated
# Use shared/libprotocol/Packdef.h via INCLUDEPATH
LIBS += -L../shared/libprotocol/release -lprotocol
