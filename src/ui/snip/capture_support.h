#pragma once

#include <QColor>
#include <QImage>
#include <QList>
#include <QString>

class QScreen;
class QWidget;

namespace snipaste::ui {

bool ensureMacScreenCaptureAccess(const QString &operation);
QImage captureScreens(const QList<QScreen *> &screens);
QImage captureAllScreens();
QColor colorAtCursor();
#ifdef Q_OS_WIN
bool showNativeShare(QWidget *owner, const QImage &image);
#endif

} // namespace snipaste::ui
