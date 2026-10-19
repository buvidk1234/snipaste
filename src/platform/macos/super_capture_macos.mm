#include "platform/windows/super_capture.h"

#import <ApplicationServices/ApplicationServices.h>

#include <QDebug>
#include <QMetaObject>

namespace snipaste::platform {

namespace {

CGEventMask monitorMask()
{
    return CGEventMaskBit(kCGEventLeftMouseDown) | CGEventMaskBit(kCGEventLeftMouseUp)
        | CGEventMaskBit(kCGEventLeftMouseDragged) | CGEventMaskBit(kCGEventRightMouseDown)
        | CGEventMaskBit(kCGEventRightMouseUp) | CGEventMaskBit(kCGEventRightMouseDragged)
        | CGEventMaskBit(kCGEventMouseMoved) | CGEventMaskBit(kCGEventFlagsChanged);
}

Qt::KeyboardModifiers qtModifiers(CGEventRef event)
{
    const CGEventFlags flags = CGEventGetFlags(event);
    Qt::KeyboardModifiers result = Qt::NoModifier;
    if (flags & kCGEventFlagMaskControl)
        result |= Qt::MetaModifier;
    if (flags & kCGEventFlagMaskShift)
        result |= Qt::ShiftModifier;
    if (flags & kCGEventFlagMaskAlternate)
        result |= Qt::AltModifier;
    if (flags & kCGEventFlagMaskCommand)
        result |= Qt::ControlModifier;
    return result;
}

QPoint globalPosition(CGEventRef event)
{
    const CGPoint point = CGEventGetLocation(event);
    return QPoint(qRound(point.x), qRound(point.y));
}

bool isButtonDown(CGEventType type, Qt::MouseButton button)
{
    if (button == Qt::RightButton)
        return type == kCGEventRightMouseDown;
    return type == kCGEventLeftMouseDown;
}

bool isButtonUp(CGEventType type, Qt::MouseButton button)
{
    if (button == Qt::RightButton)
        return type == kCGEventRightMouseUp;
    return type == kCGEventLeftMouseUp;
}

bool isMouseMove(CGEventType type)
{
    return type == kCGEventMouseMoved
        || type == kCGEventLeftMouseDragged
        || type == kCGEventRightMouseDragged;
}

} // namespace

SuperCapture::SuperCapture(QObject *parent)
    : QObject(parent)
{
}

SuperCapture::~SuperCapture()
{
    setEnabled(false);
}

void SuperCapture::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    if (enabled)
        installMonitor();
    else {
        cancelCapture();
        uninstallMonitor();
    }
}

void SuperCapture::setModifier(Qt::KeyboardModifier modifier)
{
    m_modifier = modifier;
}

void SuperCapture::setMouseButton(Qt::MouseButton button)
{
    if (button == Qt::LeftButton || button == Qt::RightButton)
        m_button = button;
}

void SuperCapture::setBlockWindowsKeys(bool left, bool right)
{
    m_blockWinLeft = left;
    m_blockWinRight = right;
}

void SuperCapture::installMonitor()
{
    if (m_eventTap)
        return;
    if (!AXIsProcessTrusted()) {
        qWarning() << "macOS super capture requires Accessibility permission";
        m_enabled = false;
        return;
    }
    // A global NSEvent monitor only observes events. A session event tap can
    // consume the capture gesture before the target application handles it.
    CFMachPortRef tap = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap,
        kCGEventTapOptionDefault, monitorMask(),
        [](CGEventTapProxy, CGEventType type, CGEventRef event, void *context) -> CGEventRef {
            auto *capture = static_cast<SuperCapture *>(context);
            if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
                capture->cancelCapture();
                CGEventTapEnable(static_cast<CFMachPortRef>(capture->m_eventTap), true);
                return event;
            }
            return capture->handleNativeMouseEvent(event) ? nullptr : event;
        }, this);
    if (!tap) {
        qWarning() << "failed to install macOS super capture event tap";
        m_enabled = false;
        return;
    }
    CFRunLoopSourceRef source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap, 0);
    if (!source) {
        CFRelease(tap);
        m_enabled = false;
        return;
    }
    m_eventTap = tap;
    m_eventTapSource = source;
    CFRunLoopAddSource(CFRunLoopGetMain(), source, kCFRunLoopCommonModes);
}

void SuperCapture::uninstallMonitor()
{
    if (m_eventTapSource) {
        auto source = static_cast<CFRunLoopSourceRef>(m_eventTapSource);
        CFRunLoopRemoveSource(CFRunLoopGetMain(), source, kCFRunLoopCommonModes);
        CFRelease(source);
        m_eventTapSource = nullptr;
    }
    if (m_eventTap) {
        auto tap = static_cast<CFMachPortRef>(m_eventTap);
        CFMachPortInvalidate(tap);
        CFRelease(tap);
        m_eventTap = nullptr;
    }
}

bool SuperCapture::handleNativeMouseEvent(void *eventPointer)
{
    CGEventRef event = static_cast<CGEventRef>(eventPointer);
    if (!event || !m_enabled)
        return false;

    const Qt::KeyboardModifiers modifiers = qtModifiers(event);
    const bool modifierPressed = m_modifier == Qt::NoModifier
        || modifiers.testFlag(m_modifier);
    if (!modifierPressed) {
        if (m_dragging)
            cancelCapture();
        return false;
    }

    const CGEventType type = CGEventGetType(event);
    if (isButtonDown(type, m_button)) {
        m_dragging = true;
        m_start = globalPosition(event);
        return true;
    }
    if (isButtonUp(type, m_button) && m_dragging) {
        finishCapture(globalPosition(event));
        return true;
    }
    return m_dragging && isMouseMove(type);
}

void SuperCapture::finishCapture(const QPoint &globalPosition)
{
    const QRect rect = QRect(m_start, globalPosition).normalized();
    m_dragging = false;
    if (rect.width() >= 2 && rect.height() >= 2) {
        // Leave the event tap immediately; taking a screenshot here can make
        // macOS disable it for exceeding the event processing deadline.
        QMetaObject::invokeMethod(this, [this, rect] {
            if (m_enabled)
                emit rectangleCaptured(rect);
        }, Qt::QueuedConnection);
    }
}

void SuperCapture::cancelCapture()
{
    m_dragging = false;
    m_start = {};
}

} // namespace snipaste::platform
