#pragma once

#include <QColor>
#include <QImage>
#include <QList>
#include <QString>

class QScreen;

namespace snipaste::ui {

bool ensureMacScreenCaptureAccess(const QString &operation);
QImage captureScreens(const QList<QScreen *> &screens);
QImage captureAllScreens();
QColor colorAtCursor();

} // namespace snipaste::ui
