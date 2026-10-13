#include "app/logging.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QString>
#include <QTextStream>

namespace {

QFile &logFile()
{
    static QFile file;
    return file;
}

QMutex &logMutex()
{
    static QMutex mutex;
    return mutex;
}

QString messageTypeName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return QStringLiteral("DEBUG");
    case QtInfoMsg:
        return QStringLiteral("INFO");
    case QtWarningMsg:
        return QStringLiteral("WARN");
    case QtCriticalMsg:
        return QStringLiteral("CRITICAL");
    case QtFatalMsg:
        return QStringLiteral("FATAL");
    }
    return QStringLiteral("UNKNOWN");
}

void messageHandler(QtMsgType type, const QMessageLogContext &, const QString &message)
{
    QMutexLocker locker(&logMutex());
    const QString line = QStringLiteral("[%1] [%2] %3\n")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                                  messageTypeName(type),
                                  message);

    if (logFile().isOpen()) {
        QTextStream stream(&logFile());
        stream << line;
        stream.flush();
    }

    if (type == QtFatalMsg)
        abort();
}

} // namespace

namespace snipaste::app {

void initializeLogging()
{
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(directory);
    logFile().setFileName(directory + QStringLiteral("/snipaste.log"));
    const bool logOpened = logFile().open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    qInstallMessageHandler(messageHandler);
    if (logOpened)
        qInfo() << "logging initialized at" << logFile().fileName();
    else
        qWarning() << "failed to open log file" << logFile().fileName();
}

} // namespace snipaste::app
