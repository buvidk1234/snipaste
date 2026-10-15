#include "ui/update/update_installer.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QSettings>
#include <QTemporaryFile>

// Keep the resource initializer in the global namespace when linking a static library.
static void initializeUpdateScripts()
{
    Q_INIT_RESOURCE(update_scripts);
}

namespace snipaste::ui {

QString updateInstallationDirectory(QString *error)
{
    QString destination;
    QString writableDirectory;
#ifdef Q_OS_WIN
    destination = QCoreApplication::applicationDirPath();
    writableDirectory = destination;
#elif defined(Q_OS_MACOS)
    const QString executable = QCoreApplication::applicationFilePath();
    const QString suffix = QStringLiteral("/Contents/MacOS/snipaste");
    if (!executable.endsWith(suffix)) {
        if (error)
            *error = QStringLiteral("请使用发布的 snipaste.app 进行自动安装。");
        return {};
    }
    destination = executable.chopped(suffix.size());
    writableDirectory = QFileInfo(destination).absolutePath();
#else
    if (error)
        *error = QStringLiteral("当前平台不支持自动安装。");
    return {};
#endif
    QTemporaryFile probe(writableDirectory + QStringLiteral("/.snipaste-update-XXXXXX"));
    if (!probe.open()) {
        if (error)
            *error = QStringLiteral("软件目录不可写，请下载新版后手动安装。");
        return {};
    }
    return destination;
}

bool launchUpdateInstaller(const QString &archive, const QString &temporaryDirectory,
                           const QString &configPath, QString *statusFile, QString *error)
{
    const QString destination = updateInstallationDirectory(error);
    if (destination.isEmpty())
        return false;
    initializeUpdateScripts();
#ifdef Q_OS_WIN
    const QString scriptName = QStringLiteral("install-windows.ps1");
#else
    const QString scriptName = QStringLiteral("install-macos.sh");
#endif
    QFile input(archive);
    if (!input.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("无法读取已下载的安装包。");
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&input)) {
        *error = QStringLiteral("无法重新校验安装包。");
        return false;
    }
    const QString script = QDir(temporaryDirectory).filePath(scriptName);
    if (!QFile::copy(QStringLiteral(":/update/") + scriptName, script)) {
        *error = QStringLiteral("无法准备更新安装程序。");
        return false;
    }
    QJsonObject settings{
        {QStringLiteral("archive"), archive},
        {QStringLiteral("destination"), destination},
        {QStringLiteral("sha256"), QString::fromLatin1(hash.result().toHex())},
        {QStringLiteral("processId"), QCoreApplication::applicationPid()},
        {QStringLiteral("config"), configPath},
        {QStringLiteral("restart"), true},
        {QStringLiteral("arguments"), QJsonArray{QStringLiteral("--snipaste-restart"),
                                               QStringLiteral("--config"), configPath}},
    };
#ifdef Q_OS_WIN
    const QSettings registration(QStringLiteral("HKEY_CURRENT_USER\\Software\\snipaste-open-source"),
                                 QSettings::NativeFormat);
    settings.insert(QStringLiteral("portable"),
                    QDir::cleanPath(registration.value(QStringLiteral("InstallDir")).toString())
                        .compare(QDir::cleanPath(destination), Qt::CaseInsensitive) != 0);
#endif
    const QString manifest = QDir(temporaryDirectory).filePath(QStringLiteral("install.json"));
    QSaveFile file(manifest);
    const QByteArray data = QJsonDocument(settings).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        *error = QStringLiteral("无法保存更新安装配置。");
        return false;
    }
    QProcess installer;
#ifdef Q_OS_WIN
    const QString systemRoot = qEnvironmentVariable("SystemRoot", QStringLiteral("C:/Windows"));
    installer.setProgram(systemRoot + QStringLiteral("/System32/WindowsPowerShell/v1.0/powershell.exe"));
    installer.setArguments({QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                            QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                            QStringLiteral("-WindowStyle"), QStringLiteral("Hidden"),
                            QStringLiteral("-File"), script, QStringLiteral("-Manifest"), manifest});
    installer.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) {
        arguments->flags |= 0x08000000; // CREATE_NO_WINDOW
    });
#else
    installer.setProgram(QStringLiteral("/bin/bash"));
    installer.setArguments({script, manifest});
#endif
    installer.setWorkingDirectory(temporaryDirectory);
    if (!installer.startDetached()) {
        *error = QStringLiteral("无法启动更新安装程序。");
        return false;
    }
    *statusFile = QDir(temporaryDirectory).filePath(QStringLiteral("status.json"));
    return true;
}

} // namespace snipaste::ui
