#pragma once

#include <QImage>
#include <QRect>
#include <QtGlobal>

namespace snipate::platform {

// Captures one CGWindowList window by its native window id.  The returned
// image uses physical pixels and has no device-pixel-ratio metadata.
QImage captureMacWindow(quintptr nativeWindow);

// Returns the window bounds in the same global logical coordinate space used
// by Qt and the capture overlay.
QRect macWindowBounds(quintptr nativeWindow);

} // namespace snipate::platform
