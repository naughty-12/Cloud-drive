# === MySQL ===
# 查找顺序：环境变量 MYSQL_DIR > 平台默认值 / pkg-config
#   Windows: set MYSQL_DIR=C:/Program Files/MySQL/MySQL Server 8.0
#   Linux  : 优先 pkg-config(mysqlclient / mariadb)，找不到再回落到 MYSQL_DIR
win32 {
    isEmpty(MYSQL_DIR): MYSQL_DIR = "C:/Program Files/MySQL/MySQL Server 8.0"
    INCLUDEPATH += $${MYSQL_DIR}/include
    LIBS += -L$${MYSQL_DIR}/lib -llibmysql
} else {
    packagesExist(mysqlclient) {
        PKGCONFIG += mysqlclient
    } else:packagesExist(mariadb) {
        PKGCONFIG += mariadb
    } else {
        isEmpty(MYSQL_DIR): MYSQL_DIR = /usr
        INCLUDEPATH += $${MYSQL_DIR}/include/mysql
        LIBS += -L$${MYSQL_DIR}/lib -lmysqlclient
    }
}

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
    $$PWD/MySqlWrapper.h \
    $$PWD/DbWorker.h \
    $$PWD/SqliteState.h

SOURCES += \
    $$PWD/MySqlWrapper.cpp \
    $$PWD/DbWorker.cpp \
    $$PWD/SqliteState.cpp
