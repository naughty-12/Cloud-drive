#include "LogManager.h"

#include <QDateTime>
#include <QDir>
#include <QMutexLocker>

// Static member definitions
QFile       LogManager::s_logFile;
QTextStream LogManager::s_stream;
QMutex      LogManager::s_mutex;

void LogManager::init(const QString& appName)
{
    // Ensure logs/ directory exists (relative to working directory)
    QDir dir;
    if (!dir.exists(QStringLiteral("logs"))) {
        dir.mkdir(QStringLiteral("logs"));
    }

    // Open dated log file (append mode — multiple runs on the same day share one file)
    const QString dateStr  = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd"));
    const QString fileName = QStringLiteral("logs/%1_%2.log").arg(appName, dateStr);

    s_logFile.setFileName(fileName);
    s_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    s_stream.setDevice(&s_logFile);

    // Install the custom handler — takes ownership of ALL Qt debug output
    qInstallMessageHandler(messageHandler);
}

void LogManager::messageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    QMutexLocker locker(&s_mutex);

    // Map QtMsgType to a short level string
    const char* levelStr = "INFO";
    switch (type) {
    case QtDebugMsg:    levelStr = "DEBUG"; break;
    case QtWarningMsg:  levelStr = "WARN";  break;
    case QtCriticalMsg: levelStr = "CRIT";  break;
    case QtFatalMsg:    levelStr = "FATAL"; break;
    // QtInfoMsg (Qt 5.5+) — treat as INFO
    default:            levelStr = "INFO";  break;
    }

    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));

    // In release builds QT_MESSAGELOGCONTEXT may be undefined → ctx.file can be nullptr
    const QString fileLine = QStringLiteral("%1:%2")
                                 .arg(ctx.file ? QString::fromUtf8(ctx.file) : QStringLiteral("unknown"))
                                 .arg(ctx.line);

    // Format: [YYYY-MM-DD HH:MM:SS] [LEVEL] [file:line] message
    s_stream << QStringLiteral("[%1] [%2] [%3] %4\n")
                    .arg(timestamp, QString::fromUtf8(levelStr), fileLine, msg);
    s_stream.flush();

    // Preserve Qt's default behaviour for fatal errors
    if (type == QtFatalMsg) {
        abort();
    }
}
