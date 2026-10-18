#include "platform/macos/system.h"

#include <QGuiApplication>
#include <QHash>
#include <QMetaObject>
#include <QObject>
#include <QPointer>

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

namespace snipaste::platform {

namespace {

struct ScrollInputMonitor {
    id monitor = nil;
    QPointer<QObject> receiver;
    QMetaObject::Connection destroyedConnection;
    QRect selection;
    QPoint dragStart;
    bool mouseDown = false;
    bool dragged = false;
};

QHash<QObject *, ScrollInputMonitor *> scrollInputMonitors;

QString qStringFromNSString(NSString *value)
{
    return value ? QString::fromUtf8([value UTF8String]) : QString();
}

bool matches(const QString &candidate, const QStringList &patterns)
{
    const QString folded = candidate.trimmed().toCaseFolded();
    if (folded.isEmpty())
        return false;
    for (const QString &raw : patterns) {
        const QString pattern = raw.trimmed().toCaseFolded();
        if (!pattern.isEmpty() && folded.contains(pattern))
            return true;
    }
    return false;
}

} // namespace

bool macForegroundApplicationMatches(const QStringList &patterns)
{
    if (patterns.isEmpty())
        return false;

    @autoreleasepool {
        NSRunningApplication *application = NSWorkspace.sharedWorkspace.frontmostApplication;
        if (!application)
            return false;
        const QString name = qStringFromNSString(application.localizedName);
        const QString bundleId = qStringFromNSString(application.bundleIdentifier);
        const QString path = qStringFromNSString(application.executableURL.path);
        return matches(name, patterns) || matches(bundleId, patterns) || matches(path, patterns);
    }
}

bool macScreenCaptureAccessGranted()
{
    if (QGuiApplication::platformName() != QStringLiteral("cocoa"))
        return true;
    return CGPreflightScreenCaptureAccess();
}

bool requestMacScreenCaptureAccess()
{
    if (macScreenCaptureAccessGranted())
        return true;
    return CGRequestScreenCaptureAccess();
}

bool postMacPasteShortcut()
{
    // CGEventPost silently drops events for an untrusted process.  Returning
    // false lets the caller retain a useful diagnostic rather than reporting
    // that the shortcut was injected when macOS rejected it.
    if (!AXIsProcessTrusted())
        return false;

    @autoreleasepool {
        // US keyboard virtual key codes: left Command = 55, V = 9.
        CGEventRef commandDown = CGEventCreateKeyboardEvent(nullptr, 55, true);
        CGEventRef pasteDown = CGEventCreateKeyboardEvent(nullptr, 9, true);
        CGEventRef pasteUp = CGEventCreateKeyboardEvent(nullptr, 9, false);
        CGEventRef commandUp = CGEventCreateKeyboardEvent(nullptr, 55, false);
        if (!commandDown || !pasteDown || !pasteUp || !commandUp) {
            if (commandDown) CFRelease(commandDown);
            if (pasteDown) CFRelease(pasteDown);
            if (pasteUp) CFRelease(pasteUp);
            if (commandUp) CFRelease(commandUp);
            return false;
        }

        CGEventSetFlags(pasteDown, kCGEventFlagMaskCommand);
        CGEventSetFlags(pasteUp, kCGEventFlagMaskCommand);
        CGEventPost(kCGHIDEventTap, commandDown);
        CGEventPost(kCGHIDEventTap, pasteDown);
        CGEventPost(kCGHIDEventTap, pasteUp);
        CGEventPost(kCGHIDEventTap, commandUp);
        CFRelease(commandDown);
        CFRelease(pasteDown);
        CFRelease(pasteUp);
        CFRelease(commandUp);
        return true;
    }
}

bool setMacWindowCollectionBehavior(quintptr nativeId, bool enabled)
{
    // The offscreen/minimal Qt platforms used by tests do not return an
    // NSView-backed WId.  Never reinterpret those synthetic ids as pointers.
    if (!nativeId || QGuiApplication::platformName() != QStringLiteral("cocoa"))
        return false;

    @autoreleasepool {
        NSView *view = (__bridge NSView *)(void *)nativeId;
        NSWindow *window = view.window;
        if (!window)
            return false;
        NSWindowCollectionBehavior behavior = window.collectionBehavior;
        const NSWindowCollectionBehavior allSpaces =
            NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary;
        if (enabled)
            behavior |= allSpaces;
        else
            behavior &= ~allSpaces;
        window.collectionBehavior = behavior;
        return true;
    }
}

void startMacScrollInputMonitor(QObject *receiver, const QRect &globalSelection)
{
    stopMacScrollInputMonitor(receiver);
    if (!receiver || globalSelection.isEmpty()
        || QGuiApplication::platformName() != QStringLiteral("cocoa"))
        return;
    auto *state = new ScrollInputMonitor;
    state->receiver = receiver;
    state->selection = globalSelection;
    const NSEventMask mask = NSEventMaskScrollWheel | NSEventMaskLeftMouseDown
        | NSEventMaskLeftMouseDragged | NSEventMaskLeftMouseUp;
    state->monitor = [NSEvent addGlobalMonitorForEventsMatchingMask:mask handler:^(NSEvent *event) {
        if (!state->receiver || !event.CGEvent)
            return;
        const CGPoint nativePoint = CGEventGetLocation(event.CGEvent);
        const QPoint point(qRound(nativePoint.x), qRound(nativePoint.y));
        const bool insideSelection = state->selection.contains(point);
        if (event.type == NSEventTypeLeftMouseDown) {
            state->mouseDown = insideSelection;
            state->dragged = false;
            state->dragStart = point;
        } else if (event.type == NSEventTypeLeftMouseDragged && state->mouseDown) {
            const QPoint delta = point - state->dragStart;
            if (qAbs(delta.x()) >= 4 || qAbs(delta.y()) >= 4)
                state->dragged = true;
        }
        const bool wheel = event.type == NSEventTypeScrollWheel && insideSelection;
        const bool dragRelease = event.type == NSEventTypeLeftMouseUp && state->dragged;
        if (wheel || dragRelease)
            QMetaObject::invokeMethod(state->receiver, "scheduleManualScrollCapture", Qt::QueuedConnection);
        if (event.type == NSEventTypeLeftMouseUp) {
            state->mouseDown = false;
            state->dragged = false;
        }
    }];
    if (!state->monitor) {
        delete state;
        return;
    }
    state->destroyedConnection = QObject::connect(receiver, &QObject::destroyed,
        [receiver] { stopMacScrollInputMonitor(receiver); });
    scrollInputMonitors.insert(receiver, state);
}

void stopMacScrollInputMonitor(QObject *receiver)
{
    auto *state = scrollInputMonitors.take(receiver);
    if (!state)
        return;
    QObject::disconnect(state->destroyedConnection);
    [NSEvent removeMonitor:state->monitor];
    delete state;
}

} // namespace snipaste::platform
