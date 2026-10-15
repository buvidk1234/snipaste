#include "platform/startup_integration.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QSettings>
#include <QTextStream>
#if defined(Q_OS_MACOS)
#include <unistd.h>
#endif
namespace snipaste::platform
{
#if defined(Q_OS_MACOS)
bool setMacLoginStartup(bool enabled)
{
    const QString launchAgentsPath = QDir::home().filePath(QStringLiteral("Library/LaunchAgents"));
    const QString plistPath = QDir(launchAgentsPath).filePath(QStringLiteral("com.snipaste.plist"));
    const QString domain = QStringLiteral("gui/%1").arg(static_cast<qulonglong>(getuid()));

    // Remove an older loaded service before changing its definition.  A
    // missing service is expected when this is the first enable/disable.
    QProcess::execute(QStringLiteral("/bin/launchctl"), {QStringLiteral("bootout"), domain, plistPath});

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
    if (!file.commit())
        return false;

    // Loading immediately makes the preference effective without requiring a
    // logout.  The app's single-instance guard prevents a second resident
    // tray process if the current process is already running.
    return QProcess::execute(QStringLiteral("/bin/launchctl"),
                             {QStringLiteral("bootstrap"), domain, plistPath}) == 0;
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
