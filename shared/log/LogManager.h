#ifndef LOGMANAGER_H
#define LOGMANAGER_H

#include <QString>
#include <QMutex>
#include <QFile>
#include <QTextStream>

class LogManager
{
public:
    /// Initialize the log system for the given application.
    /// Creates logs/ directory if it doesn't exist and opens a dated log file:
    ///   logs/<appName>_YYYYMMDD.log
    /// Must be called once at startup, before any qDebug/qWarning/qCritical usage.
    static void init(const QString& appName);

private:
    /// Custom Qt message handler installed via qInstallMessageHandler.
    /// Thread-safe — all writes are serialized through s_mutex.
    static void messageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg);

    static QFile        s_logFile;
    static QTextStream  s_stream;
    static QMutex       s_mutex;
};

#endif // LOGMANAGER_H
