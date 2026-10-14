#pragma once

#include <QString>

namespace snipaste::ui {

QString updateInstallationDirectory(QString *error = nullptr);
bool launchUpdateInstaller(const QString &archive, const QString &temporaryDirectory,
                           const QString &configPath, QString *statusFile, QString *error);

} // namespace snipaste::ui
