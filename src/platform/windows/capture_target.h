#pragma once

#include <QPoint>
#include <QList>
#include <QRect>
#include <QSize>
#include <QStringList>
#include <QtGui/qwindowdefs.h>

namespace snipaste::platform {

// Prepare on the GUI thread, then pass this value snapshot to a worker. The
// native query never dereferences the overlay or reads Qt/AppKit screen state.
struct CaptureTargetQuery {
    QPoint localPosition;
    QSize logicalScreenSize;
    QPoint nativeOrigin;
    QSize nativeScreenSize;
    quintptr overlayWindow = 0;
    bool elementMode = false;
    // Disable RPC while retaining cheap native child-window hit testing.
    bool accessibility = true;
    bool valid = false;
    QStringList excludedProcesses;
    QStringList unstableProcesses;
};

CaptureTargetQuery prepareCaptureTargetQuery(QPoint localPosition, QSize logicalScreenSize,
                                             WId overlayWindow, bool elementMode,
                                             const QStringList &excludedProcesses = {},
                                             const QStringList &unstableProcesses = {});
QList<QRect> captureTargetHierarchy(const CaptureTargetQuery &query,
                                     quintptr *nativeWindow = nullptr);

// Returns a rectangle in overlay-local logical pixels. Native hit testing is
// performed on the underlying window, never on the capture overlay itself.
QRect captureTargetAt(QPoint localPosition, QSize logicalScreenSize,
                      WId overlayWindow, bool elementMode,
                      const QStringList &excludedProcesses = {},
                      const QStringList &unstableProcesses = {},
                      quintptr *nativeWindow = nullptr);
QList<QRect> captureTargetHierarchyAt(QPoint localPosition, QSize logicalScreenSize,
                                      WId overlayWindow, bool elementMode,
                                      const QStringList &excludedProcesses = {},
                                      const QStringList &unstableProcesses = {},
                                      quintptr *nativeWindow = nullptr);

} // namespace snipaste::platform
