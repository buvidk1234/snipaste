#pragma once

#include <QPoint>
#include <QStringList>
#include <QtGlobal>

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

} // namespace snipaste::platform
