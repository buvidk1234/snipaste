#include "platform/windows/super_capture.h"

#import <AppKit/AppKit.h>

#include <QMetaObject>

namespace snipaste::platform {

namespace {

NSEventMask monitorMask()
{
    return NSEventMaskLeftMouseDown | NSEventMaskLeftMouseUp
        | NSEventMaskLeftMouseDragged | NSEventMaskRightMouseDown
        | NSEventMaskRightMouseUp | NSEventMaskRightMouseDragged
        | NSEventMaskOtherMouseDown | NSEventMaskOtherMouseUp
        | NSEventMaskOtherMouseDragged | NSEventMaskMouseMoved
        | NSEventMaskFlagsChanged;
}

Qt::KeyboardModifiers qtModifiers(NSEvent *event)
{
    const NSEventModifierFlags flags = event ? event.modifierFlags : 0;
    Qt::KeyboardModifiers result = Qt::NoModifier;
    if (flags & NSEventModifierFlagControl)
        result |= Qt::ControlModifier;
    if (flags & NSEventModifierFlagShift)
        result |= Qt::ShiftModifier;
    if (flags & NSEventModifierFlagOption)
        result |= Qt::AltModifier;
    if (flags & NSEventModifierFlagCommand)
        result |= Qt::MetaModifier;
    return result;
}

QPoint globalPosition(NSEvent *event)
{
    if (!event || !event.CGEvent)
        return {};
    const CGPoint point = CGEventGetLocation(event.CGEvent);
    return QPoint(qRound(point.x), qRound(point.y));
}

bool isButtonDown(NSEventType type, Qt::MouseButton button)
{
    if (button == Qt::RightButton)
        return type == NSEventTypeRightMouseDown;
    return type == NSEventTypeLeftMouseDown;
}

bool isButtonUp(NSEventType type, Qt::MouseButton button)
{
    if (button == Qt::RightButton)
        return type == NSEventTypeRightMouseUp;
    return type == NSEventTypeLeftMouseUp;
}

bool isMouseMove(NSEventType type)
{
    return type == NSEventTypeMouseMoved
        || type == NSEventTypeLeftMouseDragged
        || type == NSEventTypeRightMouseDragged
        || type == NSEventTypeOtherMouseDragged;
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

void SuperCapture::installMonitor()
{
    if (m_globalMonitor || m_localMonitor)
        return;

    const NSEventMask mask = monitorMask();
    m_globalMonitor = (__bridge void *)[NSEvent addGlobalMonitorForEventsMatchingMask:mask
                                                                            handler:^(NSEvent *event) {
        handleNativeMouseEvent((__bridge void *)event);
    }];
    m_localMonitor = (__bridge void *)[NSEvent addLocalMonitorForEventsMatchingMask:mask
                                                                          handler:^NSEvent *(NSEvent *event) {
        const bool handled = handleNativeMouseEvent((__bridge void *)event);
        return handled ? nil : event;
    }];
}

void SuperCapture::uninstallMonitor()
{
    if (m_globalMonitor) {
        [NSEvent removeMonitor:(__bridge id)m_globalMonitor];
        m_globalMonitor = nullptr;
    }
    if (m_localMonitor) {
        [NSEvent removeMonitor:(__bridge id)m_localMonitor];
        m_localMonitor = nullptr;
    }
}

bool SuperCapture::handleNativeMouseEvent(void *eventPointer)
{
    NSEvent *event = (__bridge NSEvent *)eventPointer;
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

    const NSEventType type = event.type;
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
    if (rect.width() >= 2 && rect.height() >= 2)
        emit rectangleCaptured(rect);
}

void SuperCapture::cancelCapture()
{
    m_dragging = false;
    m_start = {};
}

} // namespace snipaste::platform
