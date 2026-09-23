# === SQLite3 ===
# 查找顺序：环境变量 SQLITE3_DIR > 平台默认值 / pkg-config
win32 {
    isEmpty(SQLITE3_DIR): SQLITE3_DIR = "C:/Qt/Tools/mingw810_64/opt"
    INCLUDEPATH += $${SQLITE3_DIR}/include
    LIBS += -L$${SQLITE3_DIR}/lib -lsqlite3
} else {
    packagesExist(sqlite3) {
        PKGCONFIG += sqlite3
    } else {
        LIBS += -lsqlite3
    }
}

HEADERS += \
    $$PWD/UploadCache.h

SOURCES += \
    $$PWD/UploadCache.cpp
