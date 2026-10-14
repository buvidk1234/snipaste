#pragma once

#include <QString>
#include <QStringList>

namespace snipaste::app {

QString expandEnvironmentVariables(const QString &value);
bool startExternalCommand(const QStringList &command);
void playConfiguredSound(const QString &configuredFile);

} // namespace snipaste::app
