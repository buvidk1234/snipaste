#pragma once

#include <QPoint>
#include <QRect>
#include <QStringList>
#include <QtGlobal>

class QObject;

namespace snipaste::platform {

// Returns whether the frontmost macOS application matches one of the user
// supplied executable, bundle identifier, or application name fragments.
bool macForegroundApplicationMatches(const QStringList &patterns);

// Checks and, when possible, requests macOS Screen Recording permission.
bool macScreenCaptureAccessGranted();
bool requestMacScreenCaptureAccess();

// Posts Cmd+V through the HID event stream.  macOS requires the application to
// be trusted under System Settings > Privacy & Security > Accessibility.
bool postMacPasteShortcut();

// Makes a native Qt top-level window join every Mission Control Space.  The
// native-id argument is the WId returned by QWidget::winId().
bool setMacWindowCollectionBehavior(quintptr nativeId, bool enabled);

// Notify scheduleManualScrollCapture() after wheel input or a scrollbar drag
// inside the selected global logical rectangle. These observers pass input on.
void startMacScrollInputMonitor(QObject *receiver, const QRect &globalSelection);
void stopMacScrollInputMonitor(QObject *receiver);

} // namespace snipaste::platform
