#include "platform/windows/capture_target.h"
#include "platform/macos/capture.h"

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>

#include <QGuiApplication>
#include <QtMath>

#include <algorithm>
#include <cstdint>
#include <dlfcn.h>

namespace snipate::platform {

namespace {

struct NativeTarget {
    CGRect bounds{};
    pid_t processId = 0;
    QString processName;
    QString processPath;
    CGWindowID windowId = 0;
};

QString stringFromCFString(CFTypeRef value)
{
    if (!value || CFGetTypeID(value) != CFStringGetTypeID())
        return {};
    const CFStringRef string = static_cast<CFStringRef>(value);
    const CFIndex length = CFStringGetLength(string);
    if (length <= 0)
        return {};
    QString result;
    result.resize(static_cast<qsizetype>(length));
    CFStringGetCharacters(string, CFRangeMake(0, length), reinterpret_cast<UniChar *>(result.data()));
    return result;
}

bool wildcardMatch(const QString &value, const QString &pattern, qsizetype vi = 0,
                   qsizetype pi = 0)
{
    if (pi == pattern.size())
        return vi == value.size();
    if (pattern.at(pi) == QChar('*'))
        return wildcardMatch(value, pattern, vi, pi + 1)
            || (vi < value.size() && wildcardMatch(value, pattern, vi + 1, pi));
    return vi < value.size()
        && (pattern.at(pi) == QChar('?') || pattern.at(pi) == value.at(vi))
        && wildcardMatch(value, pattern, vi + 1, pi + 1);
}

bool processMatches(const NativeTarget &target, const QStringList &patterns)
{
    if (patterns.isEmpty())
        return false;
    const QString name = target.processName.toCaseFolded();
    const QString path = target.processPath.toCaseFolded();
    for (const QString &raw : patterns) {
        const QString pattern = raw.trimmed().toCaseFolded();
        if (pattern.isEmpty())
            continue;
        if (pattern.contains(QChar('*')) || pattern.contains(QChar('?'))) {
            if (wildcardMatch(name, pattern) || wildcardMatch(path, pattern))
                return true;
        } else if (name.contains(pattern) || path.contains(pattern)) {
            return true;
        }
    }
    return false;
}

bool windowBounds(CFDictionaryRef dictionary, CGRect &bounds)
{
    if (!dictionary)
        return false;
    const CFTypeRef value = CFDictionaryGetValue(dictionary, kCGWindowBounds);
    return value && CGRectMakeWithDictionaryRepresentation(static_cast<CFDictionaryRef>(value), &bounds)
        && bounds.size.width > 0.0 && bounds.size.height > 0.0;
}

pid_t processId(CFDictionaryRef dictionary)
{
    const CFTypeRef value = CFDictionaryGetValue(dictionary, kCGWindowOwnerPID);
    if (!value || CFGetTypeID(value) != CFNumberGetTypeID())
        return 0;
    int pid = 0;
    CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberIntType, &pid);
    return static_cast<pid_t>(pid);
}

QString processPathForId(pid_t pid)
{
    NSRunningApplication *application =
        [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
    return application.executableURL.path
        ? QString::fromUtf8(application.executableURL.path.UTF8String)
        : QString();
}

CGPoint cursorPosition()
{
    CGEventRef event = CGEventCreate(nullptr);
    if (!event)
        return CGPointZero;
    const CGPoint result = CGEventGetLocation(event);
    CFRelease(event);
    return result;
}

QPoint nativeOriginForLocalPoint(QPoint localPosition)
{
    const CGPoint cursor = cursorPosition();
    return QPoint(qRound(cursor.x) - localPosition.x(), qRound(cursor.y) - localPosition.y());
}

QRect localRect(CGRect nativeBounds, const QPoint &nativeOrigin, QSize logicalScreenSize)
{
    const QRect result(qRound(nativeBounds.origin.x) - nativeOrigin.x(),
                      qRound(nativeBounds.origin.y) - nativeOrigin.y(),
                      qRound(nativeBounds.size.width),
                      qRound(nativeBounds.size.height));
    return result.intersected(QRect(QPoint(), logicalScreenSize));
}

bool axElementBounds(CGPoint point, CGRect &bounds)
{
    if (!AXIsProcessTrusted())
        return false;
    AXUIElementRef system = AXUIElementCreateSystemWide();
    if (!system)
        return false;
    AXUIElementRef element = nullptr;
    const AXError hit = AXUIElementCopyElementAtPosition(system, static_cast<float>(point.x),
                                                          static_cast<float>(point.y), &element);
    CFRelease(system);
    if (hit != kAXErrorSuccess || !element)
        return false;

    AXValueRef position = nullptr;
    AXValueRef size = nullptr;
    const AXError positionError = AXUIElementCopyAttributeValue(element, kAXPositionAttribute,
                                                                  reinterpret_cast<CFTypeRef *>(&position));
    const AXError sizeError = AXUIElementCopyAttributeValue(element, kAXSizeAttribute,
                                                              reinterpret_cast<CFTypeRef *>(&size));
    CGPoint origin{};
    CGSize dimensions{};
    const bool valid = positionError == kAXErrorSuccess && sizeError == kAXErrorSuccess
        && AXValueGetType(position) == kAXValueCGPointType
        && AXValueGetType(size) == kAXValueCGSizeType
        && AXValueGetValue(position, static_cast<AXValueType>(kAXValueCGPointType), &origin)
        && AXValueGetValue(size, static_cast<AXValueType>(kAXValueCGSizeType), &dimensions)
        && dimensions.width > 0.0 && dimensions.height > 0.0;
    if (valid)
        bounds = CGRectMake(origin.x, origin.y, dimensions.width, dimensions.height);
    if (position)
        CFRelease(position);
    if (size)
        CFRelease(size);
    CFRelease(element);
    return valid;
}

NativeTarget targetAtPoint(CGPoint point, CGWindowID ignoredWindow,
                           const QStringList &excludedProcesses)
{
    NativeTarget result;
    CFArrayRef windows = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly
                                                        | kCGWindowListExcludeDesktopElements,
                                                    kCGNullWindowID);
    if (!windows)
        return result;
    const CFIndex count = CFArrayGetCount(windows);
    for (CFIndex index = 0; index < count; ++index) {
        const auto dictionary = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(windows, index));
        if (!dictionary)
            continue;
        CGRect bounds{};
        if (!windowBounds(dictionary, bounds) || !CGRectContainsPoint(bounds, point))
            continue;
        const pid_t pid = processId(dictionary);
        const CFTypeRef numberValue = CFDictionaryGetValue(dictionary, kCGWindowNumber);
        CGWindowID windowId = kCGNullWindowID;
        if (numberValue)
            CFNumberGetValue(static_cast<CFNumberRef>(numberValue), kCFNumberIntType, &windowId);
        if (windowId == ignoredWindow && ignoredWindow != kCGNullWindowID)
            continue;
        const CFTypeRef alphaValue = CFDictionaryGetValue(dictionary, kCGWindowAlpha);
        CGFloat alpha = 1.0;
        if (alphaValue)
            CFNumberGetValue(static_cast<CFNumberRef>(alphaValue), kCFNumberCGFloatType, &alpha);
        if (alpha <= 0.01)
            continue;
        result.bounds = bounds;
        result.processId = pid;
        result.processName = stringFromCFString(CFDictionaryGetValue(dictionary, kCGWindowOwnerName));
        result.processPath = processPathForId(pid);
        result.windowId = windowId;
        if (processMatches(result, excludedProcesses))
            result = {};
        break;
    }
    CFRelease(windows);
    return result;
}

} // namespace

QList<QRect> captureTargetHierarchyAt(QPoint localPosition, QSize logicalScreenSize,
                                      WId overlayWindow, bool elementMode,
                                      const QStringList &excludedProcesses,
                                      const QStringList &unstableProcesses,
                                      quintptr *nativeWindow)
{
    if (nativeWindow)
        *nativeWindow = 0;
    if (logicalScreenSize.isEmpty())
        return {};

    const QPoint nativeOrigin = nativeOriginForLocalPoint(localPosition);
    const QPoint nativePoint = nativeOrigin + localPosition;
    CGWindowID ignoredWindow = kCGNullWindowID;
    if (overlayWindow && QGuiApplication::platformName() == QStringLiteral("cocoa")) {
        NSView *overlayView = (__bridge NSView *)(void *)overlayWindow;
        if (overlayView.window)
            ignoredWindow = static_cast<CGWindowID>(overlayView.window.windowNumber);
    }
    const NativeTarget target = targetAtPoint(CGPointMake(nativePoint.x(), nativePoint.y()),
                                              ignoredWindow, excludedProcesses);
    if (nativeWindow)
        *nativeWindow = static_cast<quintptr>(target.windowId);
    const QRect screenRect(QPoint(), logicalScreenSize);
    if (target.bounds.size.width <= 0.0 || target.bounds.size.height <= 0.0)
        return {screenRect};

    QList<QRect> targets;
    const QRect window = localRect(target.bounds, nativeOrigin, logicalScreenSize);
    if (!window.isEmpty() && window.contains(localPosition))
        targets.append(window);

    if (elementMode && !processMatches(target, unstableProcesses)) {
        CGRect elementBounds{};
        if (axElementBounds(CGPointMake(nativePoint.x(), nativePoint.y()), elementBounds)) {
            // The overlay is still visible while the pointer is inspected.
            // Only accept an accessibility element contained by the selected
            // application window; this prevents the overlay's own controls
            // from becoming the detected target.
            if (CGRectContainsRect(target.bounds, elementBounds)) {
                const QRect element = localRect(elementBounds, nativeOrigin, logicalScreenSize);
                if (!element.isEmpty() && element.contains(localPosition) && !targets.contains(element))
                    targets.prepend(element);
            }
        }
    }

    if (!targets.contains(screenRect))
        targets.append(screenRect);
    std::sort(targets.begin(), targets.end(), [](const QRect &left, const QRect &right) {
        return qint64(left.width()) * left.height() < qint64(right.width()) * right.height();
    });
    return targets;
}

QRect captureTargetAt(QPoint localPosition, QSize logicalScreenSize, WId overlayWindow,
                      bool elementMode, const QStringList &excludedProcesses,
                      const QStringList &unstableProcesses,
                      quintptr *nativeWindow)
{
    const QList<QRect> hierarchy = captureTargetHierarchyAt(localPosition, logicalScreenSize,
                                                              overlayWindow, elementMode,
                                                              excludedProcesses, unstableProcesses,
                                                              nativeWindow);
    return hierarchy.isEmpty() ? QRect() : hierarchy.first();
}

QImage captureMacWindow(quintptr nativeWindow)
{
    if (!nativeWindow)
        return {};

    using CreateWindowImage = CGImageRef (*)(CGRect, CGWindowListOption, CGWindowID,
                                             CGWindowImageOption);
    const auto createWindowImage = reinterpret_cast<CreateWindowImage>(
        dlsym(RTLD_DEFAULT, "CGWindowListCreateImage"));
    if (!createWindowImage)
        return {};
    CGImageRef windowImage = createWindowImage(CGRectNull,
                                                kCGWindowListOptionIncludingWindow,
                                                static_cast<CGWindowID>(nativeWindow),
                                                kCGWindowImageBoundsIgnoreFraming);
    if (!windowImage)
        return {};

    const size_t width = CGImageGetWidth(windowImage);
    const size_t height = CGImageGetHeight(windowImage);
    QImage result(static_cast<int>(width), static_cast<int>(height), QImage::Format_RGBA8888_Premultiplied);
    if (result.isNull()) {
        CGImageRelease(windowImage);
        return {};
    }

    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = CGBitmapContextCreate(result.bits(), width, height, 8,
                                                  static_cast<size_t>(result.bytesPerLine()),
                                                  colorSpace,
                                                  static_cast<uint32_t>(kCGImageAlphaPremultipliedLast)
                                                      | static_cast<uint32_t>(kCGBitmapByteOrder32Big));
    if (!context) {
        CGColorSpaceRelease(colorSpace);
        CGImageRelease(windowImage);
        return {};
    }
    CGContextTranslateCTM(context, 0.0, static_cast<CGFloat>(height));
    CGContextScaleCTM(context, 1.0, -1.0);
    CGContextDrawImage(context, CGRectMake(0.0, 0.0, width, height), windowImage);
    CGContextRelease(context);
    CGColorSpaceRelease(colorSpace);
    CGImageRelease(windowImage);
    result.setDevicePixelRatio(1.0);
    return result;
}

QRect macWindowBounds(quintptr nativeWindow)
{
    if (!nativeWindow)
        return {};
    CFArrayRef windows = CGWindowListCopyWindowInfo(kCGWindowListOptionIncludingWindow,
                                                     static_cast<CGWindowID>(nativeWindow));
    if (!windows)
        return {};
    QRect result;
    const CFIndex count = CFArrayGetCount(windows);
    for (CFIndex index = 0; index < count; ++index) {
        const auto dictionary = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(windows, index));
        CGRect bounds{};
        if (windowBounds(dictionary, bounds)) {
            result = QRect(qRound(bounds.origin.x), qRound(bounds.origin.y),
                           qRound(bounds.size.width), qRound(bounds.size.height));
            break;
        }
    }
    CFRelease(windows);
    return result;
}

} // namespace snipate::platform
