#pragma once
#include <QStringList>
class QCommandLineParser;
namespace snipaste::app
{
void configureCommandLine(QCommandLineParser &parser);
qint64 commandBlockDeadline(const QStringList &arguments);
} // namespace snipaste::app
