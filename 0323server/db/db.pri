# === MySQL ===
# Set MYSQL_DIR in your environment or update the path below:
#   set MYSQL_DIR=C:/Program Files/MySQL/MySQL Server 8.0
isEmpty(MYSQL_DIR): MYSQL_DIR = "C:/Program Files/MySQL/MySQL Server 8.0"
INCLUDEPATH += $${MYSQL_DIR}/include
LIBS += -L$${MYSQL_DIR}/lib -llibmysql

# === SQLite3 ===
# Download from https://www.sqlite.org/download.html
# Extract sqlite3.h + sqlite3.dll + sqlite3.def to a known location:
isEmpty(SQLITE3_DIR): SQLITE3_DIR = "C:/Qt/Tools/mingw810_64/opt"
INCLUDEPATH += $${SQLITE3_DIR}/include
LIBS += -L$${SQLITE3_DIR}/lib -lsqlite3

HEADERS += \
    $$PWD/MySqlWrapper.h \
    $$PWD/DbWorker.h \
    $$PWD/SqliteState.h

SOURCES += \
    $$PWD/MySqlWrapper.cpp \
    $$PWD/DbWorker.cpp \
    $$PWD/SqliteState.cpp
