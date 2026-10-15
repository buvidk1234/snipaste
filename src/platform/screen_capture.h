#pragma once

#include <QImage>

class QScreen;

namespace snipaste::platform {

// Capture one monitor at its native pixel size.  Windows uses a small GDI
// DIBSection so the hotkey path avoids QPixmap allocation and conversion;
// other platforms retain Qt's screen capture implementation.
QImage captureScreen(QScreen *screen);

// Map global logical edges to native pixels before clipping, including screens
// with negative origins and fractional scaling. The result is screen-local.
QRect screenCapturePixelRect(const QRect &screenGeometry, const QSize &pixelSize,
                            const QRect &globalLogicalRegion);
QImage captureScreenRegion(QScreen *screen, const QRect &globalLogicalRegion);

} // namespace snipaste::platform
