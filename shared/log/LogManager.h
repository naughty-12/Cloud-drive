#ifndef LOGMANAGER_H
#define LOGMANAGER_H

#include <QString>
#include <QMutex>
#include <QFile>
#include <QTextStream>

class LogManager
{
public:
    /// 为指定应用程序初始化日志系统。
    /// 若 logs/ 目录不存在则创建，并打开按日期命名的日志文件：
    ///   logs/<appName>_YYYYMMDD.log
    /// 必须在启动时调用一次，且先于任何 qDebug/qWarning/qCritical 的使用。
    static void init(const QString& appName);

private:
    /// 通过 qInstallMessageHandler 安装的自定义 Qt 消息处理函数。
    /// 线程安全——所有写入都通过 s_mutex 串行化。
    static void messageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg);

    static QFile        s_logFile;
    static QTextStream  s_stream;
    static QMutex       s_mutex;
};

#endif // LOGMANAGER_H
