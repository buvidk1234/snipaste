#pragma once

#include <QPoint>
#include <QList>
#include <QRect>
#include <QSize>
#include <QStringList>
#include <QtGui/qwindowdefs.h>

namespace snipaste::platform {

// Returns a rectangle in overlay-local logical pixels. Native hit testing is
// performed on the underlying window, never on the capture overlay itself.
QRect captureTargetAt(QPoint localPosition, QSize logicalScreenSize,
                      WId overlayWindow, bool elementMode,
                      const QStringList &excludedProcesses = {},
                      const QStringList &unstableProcesses = {});
QList<QRect> captureTargetHierarchyAt(QPoint localPosition, QSize logicalScreenSize,
                                      WId overlayWindow, bool elementMode,
                                      const QStringList &excludedProcesses = {},
                                      const QStringList &unstableProcesses = {});

} // namespace snipaste::platform
