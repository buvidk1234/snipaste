#pragma once

#include <QDateTime>
#include <QList>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>

namespace snipaste::core {

// Command line geometry and output helpers live in core so the application
// entry point only wires commands to services.  These functions are pure
// value transformations and are safe to use from tests or another frontend.
QList<int> parseIntegerList(QString value);
QRect parseRectSpec(const QString &value);
QSize parseSizeSpec(const QString &value);
QPoint parsePointSpec(const QString &value);
QStringList normalizeMultiValueOptions(QStringList arguments);
QStringList splitOutputSpec(const QString &spec);
QString expandOutputFilePattern(const QString &pattern, const QDateTime &dateTime);

} // namespace snipaste::core
