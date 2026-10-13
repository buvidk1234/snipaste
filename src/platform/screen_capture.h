#pragma once

#include <QImage>

class QScreen;

namespace snipaste::platform {

// Capture one monitor at its native pixel size.  Windows uses a small GDI
// DIBSection so the hotkey path avoids QPixmap allocation and conversion;
// other platforms retain Qt's screen capture implementation.
QImage captureScreen(QScreen *screen);

} // namespace snipaste::platform
