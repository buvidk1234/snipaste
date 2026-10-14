#include "app/external_command.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#ifdef Q_OS_WIN
#include <windows.h>
#include <mmsystem.h>
#endif

namespace snipaste::app {

QString expandEnvironmentVariables(const QString &value)
{
    QString expanded = value;
    const QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    static const QRegularExpression percent(QStringLiteral("%([^%]+)%"));
    QRegularExpressionMatch match;
    int offset = 0;
    while ((match = percent.match(expanded, offset)).hasMatch()) {
        const QString replacement = environment.value(match.captured(1), match.captured(0));
        expanded.replace(match.capturedStart(0), match.capturedLength(0), replacement);
        offset = match.capturedStart(0) + replacement.size();
    }
    static const QRegularExpression dollar(QStringLiteral("\\$\\{([^}]+)\\}"));
    offset = 0;
    while ((match = dollar.match(expanded, offset)).hasMatch()) {
        const QString replacement = environment.value(match.captured(1), match.captured(0));
        expanded.replace(match.capturedStart(0), match.capturedLength(0), replacement);
        offset = match.capturedStart(0) + replacement.size();
    }
    return expanded;
}

bool startExternalCommand(const QStringList &command)
{
    if (command.isEmpty())
        return false;
    QStringList expanded;
    expanded.reserve(command.size());
    for (const QString &part : command)
        expanded.append(expandEnvironmentVariables(part));
    const QString program = expanded.takeFirst();
    return !program.isEmpty() && QProcess::startDetached(program, expanded);
}

void playConfiguredSound(const QString &configuredFile)
{
    if (configuredFile.trimmed().isEmpty())
        return;
    QFileInfo configured(configuredFile);
    QString path = configured.isAbsolute()
        ? configured.absoluteFilePath()
        : QDir(QCoreApplication::applicationDirPath()).filePath(configuredFile);
    if (!QFileInfo::exists(path) && configured.isRelative())
        path = QDir::current().filePath(configuredFile);
    if (!QFileInfo::exists(path)) {
        // Keep the default OCR completion feedback useful in installations
        // that do not ship the optional capture sound asset.
        QApplication::beep();
        return;
    }

#ifdef Q_OS_WIN
    PlaySoundW(reinterpret_cast<LPCWSTR>(path.utf16()), nullptr,
               SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
#elif defined(Q_OS_MACOS)
    QProcess::startDetached(QStringLiteral("afplay"), {path});
#else
    if (!QProcess::startDetached(QStringLiteral("paplay"), {path}))
        QProcess::startDetached(QStringLiteral("aplay"), {path});
#endif
}

} // namespace snipaste::app
