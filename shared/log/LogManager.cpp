#include "LogManager.h"

#include <QDateTime>
#include <QDir>
#include <QMutexLocker>

// 静态成员定义
QFile       LogManager::s_logFile;
QTextStream LogManager::s_stream;
QMutex      LogManager::s_mutex;

void LogManager::init(const QString& appName)
{
    // 确保 logs/ 目录存在（相对于工作目录）
    QDir dir;
    if (!dir.exists(QStringLiteral("logs"))) {
        dir.mkdir(QStringLiteral("logs"));
    }

    // 打开按日期命名的日志文件（追加模式——同一天多次运行共用同一文件）
    const QString dateStr  = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd"));
    const QString fileName = QStringLiteral("logs/%1_%2.log").arg(appName, dateStr);

    s_logFile.setFileName(fileName);
    s_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    s_stream.setDevice(&s_logFile);

    // 安装自定义处理函数——接管所有 Qt 调试输出
    qInstallMessageHandler(messageHandler);
}

void LogManager::messageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    QMutexLocker locker(&s_mutex);

    // 将 QtMsgType 映射为简短的级别字符串
    const char* levelStr = "INFO";
    switch (type) {
    case QtDebugMsg:    levelStr = "DEBUG"; break;
    case QtWarningMsg:  levelStr = "WARN";  break;
    case QtCriticalMsg: levelStr = "CRIT";  break;
    case QtFatalMsg:    levelStr = "FATAL"; break;
    // QtInfoMsg（Qt 5.5+）——按 INFO 处理
    default:            levelStr = "INFO";  break;
    }

    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));

    // 在 release 构建中 QT_MESSAGELOGCONTEXT 可能未定义，ctx.file 可能为 nullptr
    const QString fileLine = QStringLiteral("%1:%2")
                                 .arg(ctx.file ? QString::fromUtf8(ctx.file) : QStringLiteral("unknown"))
                                 .arg(ctx.line);

    // 格式：[YYYY-MM-DD HH:MM:SS] [LEVEL] [file:line] message
    s_stream << QStringLiteral("[%1] [%2] [%3] %4\n")
                    .arg(timestamp, QString::fromUtf8(levelStr), fileLine, msg);
    s_stream.flush();

    // 保留 Qt 对致命错误的默认行为
    if (type == QtFatalMsg) {
        abort();
    }
}
