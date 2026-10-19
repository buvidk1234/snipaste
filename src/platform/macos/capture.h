#pragma once

#include <QImage>
#include <QRect>
#include <QtGlobal>

namespace snipaste::platform {

#if defined(Q_OS_MACOS)
// QImage declares CGImageRef without pulling Apple framework headers into C++.
QImage imageFromMacCGImage(CGImageRef image);
#endif

// Captures one ScreenCaptureKit window by its native window id. The returned
// image uses physical pixels and has no device-pixel-ratio metadata.
QImage captureMacWindow(quintptr nativeWindow);

// Returns the window bounds in the same global logical coordinate space used
// by Qt and the capture overlay.
QRect macWindowBounds(quintptr nativeWindow);
bool macWindowExists(quintptr nativeWindow);
bool activateMacWindow(quintptr nativeWindow);
bool postMacScrollStep(quintptr nativeWindow, const QPoint &globalPoint);

} // namespace snipaste::platform
