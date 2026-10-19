#include "platform/startup_integration.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QTextStream>
namespace snipaste::platform
{
#if defined(Q_OS_MACOS)
bool setMacLoginStartup(bool enabled)
{
    const QString launchAgentsPath = QDir::home().filePath(QStringLiteral("Library/LaunchAgents"));
    const QString plistPath = QDir(launchAgentsPath).filePath(QStringLiteral("com.snipaste.plist"));
    if (!enabled)
        return !QFile::exists(plistPath) || QFile::remove(plistPath);

    if (!QDir().mkpath(launchAgentsPath))
        return false;
    QSaveFile file(plistPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream stream(&file);
    stream << QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n")
           << QStringLiteral("<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" ")
           << QStringLiteral("\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n")
           << QStringLiteral("<plist version=\"1.0\"><dict>\n")
           << QStringLiteral("<key>Label</key><string>com.snipaste</string>\n")
           << QStringLiteral("<key>ProgramArguments</key><array><string>")
           << QFileInfo(QCoreApplication::applicationFilePath()).absoluteFilePath().toHtmlEscaped()
           << QStringLiteral("</string></array>\n") << QStringLiteral("<key>RunAtLoad</key><true/>\n")
           << QStringLiteral("</dict></plist>\n");
    stream.flush();
    if (stream.status() != QTextStream::Ok)
        return false;
    // launchd reads this file at the next login. Unloading a currently loaded
    // agent here would terminate this app if it was started by launchd.
    return file.commit();
}
#endif
bool setLoginStartup(bool enabled)
{
#ifdef Q_OS_WIN
    QSettings settings(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                       QSettings::NativeFormat);
    if (enabled)
        settings.setValue(QStringLiteral("snipaste"),
                          QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
    else
        settings.remove(QStringLiteral("snipaste"));
    settings.sync();
    return settings.status() == QSettings::NoError;
#elif defined(Q_OS_MACOS)
    return setMacLoginStartup(enabled);
#else
    Q_UNUSED(enabled)
    return true;
#endif
}
} // namespace snipaste::platform
