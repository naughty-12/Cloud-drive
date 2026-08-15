# === SQLite3 ===
isEmpty(SQLITE3_DIR): SQLITE3_DIR = "C:/Qt/Tools/mingw810_64/opt"
INCLUDEPATH += $${SQLITE3_DIR}/include
LIBS += -L$${SQLITE3_DIR}/lib -lsqlite3

HEADERS += \
    $$PWD/UploadCache.h

SOURCES += \
    $$PWD/UploadCache.cpp
