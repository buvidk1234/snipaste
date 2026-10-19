#include "platform/windows/capture_target.h"
#include "platform/macos/capture.h"

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreVideo/CVPixelBuffer.h>
#import <Foundation/Foundation.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include <QColorSpace>
#include <QGuiApplication>
#include <QtMath>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <future>
#include <libproc.h>
#include <memory>

namespace snipaste::platform {

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
    char path[PROC_PIDPATHINFO_MAXSIZE]{};
    return proc_pidpath(pid, path, sizeof(path)) > 0 ? QString::fromUtf8(path) : QString();
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

QList<CGRect> axElementBounds(CGPoint point, pid_t processId)
{
    if (!AXIsProcessTrusted())
        return {};
    AXUIElementRef application = AXUIElementCreateApplication(processId);
    if (!application)
        return {};
    // An application-scoped hit test bypasses the capture overlay. Avoid
    // changing the system-wide AX messaging timeout shared by other callers.
    AXUIElementSetMessagingTimeout(application, 0.08f);
    AXUIElementRef element = nullptr;
    const AXError hit = AXUIElementCopyElementAtPosition(application, static_cast<float>(point.x),
                                                          static_cast<float>(point.y), &element);
    CFRelease(application);
    if (hit != kAXErrorSuccess || !element)
        return {};

    const void *attributes[]{kAXPositionAttribute, kAXSizeAttribute, kAXParentAttribute};
    CFArrayRef names = CFArrayCreate(kCFAllocatorDefault, attributes, 3, &kCFTypeArrayCallBacks);
    QList<CGRect> bounds;
    const CFAbsoluteTime deadline = CFAbsoluteTimeGetCurrent() + 0.12;
    for (int depth = 0; depth < 8 && element && CFAbsoluteTimeGetCurrent() < deadline; ++depth) {
        AXUIElementSetMessagingTimeout(element, 0.08f);
        pid_t owner = 0;
        if (AXUIElementGetPid(element, &owner) != kAXErrorSuccess || owner != processId)
            break;
        CFArrayRef values = nullptr;
        AXUIElementRef parent = nullptr;
        if (AXUIElementCopyMultipleAttributeValues(element, names, 0, &values) == kAXErrorSuccess
            && values && CFArrayGetCount(values) == 3) {
            const auto position = static_cast<AXValueRef>(CFArrayGetValueAtIndex(values, 0));
            const auto size = static_cast<AXValueRef>(CFArrayGetValueAtIndex(values, 1));
            CGPoint origin{};
            CGSize dimensions{};
            if (CFGetTypeID(position) == AXValueGetTypeID()
                && CFGetTypeID(size) == AXValueGetTypeID()
                && AXValueGetType(position) == kAXValueCGPointType
                && AXValueGetType(size) == kAXValueCGSizeType
                && AXValueGetValue(position, static_cast<AXValueType>(kAXValueCGPointType), &origin)
                && AXValueGetValue(size, static_cast<AXValueType>(kAXValueCGSizeType), &dimensions)
                && dimensions.width > 0.0 && dimensions.height > 0.0)
                bounds.append(CGRectMake(origin.x, origin.y, dimensions.width, dimensions.height));
            const CFTypeRef parentValue = CFArrayGetValueAtIndex(values, 2);
            if (CFGetTypeID(parentValue) == AXUIElementGetTypeID()
                && !CFEqual(parentValue, element))
                parent = static_cast<AXUIElementRef>(CFRetain(parentValue));
        }
        if (values)
            CFRelease(values);
        CFRelease(element);
        element = parent;
    }
    if (element)
        CFRelease(element);
    CFRelease(names);
    return bounds;
}

NativeTarget targetAtPoint(CGPoint point, CGWindowID ignoredWindow,
                           const QStringList &excludedProcesses, bool needsProcessPath)
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
        const CFTypeRef layerValue = CFDictionaryGetValue(dictionary, kCGWindowLayer);
        int layer = 0;
        if (layerValue)
            CFNumberGetValue(static_cast<CFNumberRef>(layerValue), kCFNumberIntType, &layer);
        // System overlays such as Notification Center can cover the display
        // with fully transparent pixels while reporting an opaque window.
        // Keep ordinary, floating and modal application windows below the Dock.
        if (layer >= CGWindowLevelForKey(kCGDockWindowLevelKey))
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
        if (needsProcessPath || !excludedProcesses.isEmpty())
            result.processPath = processPathForId(pid);
        result.windowId = windowId;
        if (processMatches(result, excludedProcesses))
            result = {};
        break;
    }
    CFRelease(windows);
    return result;
}

NativeTarget targetForWindow(quintptr nativeWindow)
{
    NativeTarget result;
    if (!nativeWindow)
        return result;
    CFArrayRef windows = CGWindowListCopyWindowInfo(kCGWindowListOptionIncludingWindow,
                                                     static_cast<CGWindowID>(nativeWindow));
    if (!windows)
        return result;
    if (CFArrayGetCount(windows) > 0) {
        const auto dictionary = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(windows, 0));
        if (windowBounds(dictionary, result.bounds)) {
            result.windowId = static_cast<CGWindowID>(nativeWindow);
            result.processId = processId(dictionary);
        }
    }
    CFRelease(windows);
    return result;
}

} // namespace

QImage imageFromMacCGImage(CGImageRef image)
{
    if (!image)
        return {};
    const size_t width = CGImageGetWidth(image);
    const size_t height = CGImageGetHeight(image);
    QImage result(static_cast<int>(width), static_cast<int>(height), QImage::Format_RGBA8888_Premultiplied);
    if (result.isNull())
        return {};
    CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef context = CGBitmapContextCreate(result.bits(), width, height, 8,
        static_cast<size_t>(result.bytesPerLine()), colorSpace,
        static_cast<uint32_t>(kCGImageAlphaPremultipliedLast)
            | static_cast<uint32_t>(kCGBitmapByteOrder32Big));
    CGColorSpaceRelease(colorSpace);
    if (!context)
        return {};
    // Both CGImage data and QImage scanlines start with the top row. A Y-axis
    // transform here would invert the screenshot.
    CGContextSetBlendMode(context, kCGBlendModeCopy);
    CGContextDrawImage(context, CGRectMake(0.0, 0.0, width, height), image);
    CGContextRelease(context);
    result.setColorSpace(QColorSpace::SRgb);
    result.setDevicePixelRatio(1.0);
    return result;
}

CaptureTargetQuery prepareCaptureTargetQuery(QPoint localPosition, QSize logicalScreenSize,
                                             WId overlayWindow, bool elementMode,
                                             const QStringList &excludedProcesses,
                                             const QStringList &unstableProcesses)
{
    CaptureTargetQuery query;
    query.localPosition = localPosition;
    query.logicalScreenSize = logicalScreenSize;
    query.nativeScreenSize = logicalScreenSize;
    query.elementMode = elementMode;
    query.excludedProcesses = excludedProcesses;
    query.unstableProcesses = unstableProcesses;
    if (logicalScreenSize.isEmpty())
        return query;

    query.nativeOrigin = nativeOriginForLocalPoint(localPosition);
    if (overlayWindow && QGuiApplication::platformName() == QStringLiteral("cocoa")) {
        NSView *overlayView = (__bridge NSView *)(void *)overlayWindow;
        if (overlayView.window) {
            query.overlayWindow = static_cast<quintptr>(overlayView.window.windowNumber);
            const NSRect local = [overlayView convertRect:overlayView.bounds toView:nil];
            const NSRect global = [overlayView.window convertRectToScreen:local];
            NSScreen *primary = NSScreen.screens.firstObject;
            if (primary) {
                query.nativeOrigin = QPoint(qRound(NSMinX(global)),
                                              qRound(NSMaxY(primary.frame) - NSMaxY(global)));
            }
        }
    }
    query.valid = true;
    return query;
}

QList<QRect> captureTargetHierarchy(const CaptureTargetQuery &query, quintptr *nativeWindow)
{
    if (nativeWindow)
        *nativeWindow = 0;
    if (!query.valid || query.logicalScreenSize.isEmpty())
        return {};
    const QPoint localPosition = query.localPosition;
    const QSize logicalScreenSize = query.logicalScreenSize;
    const QPoint nativeOrigin = query.nativeOrigin;
    const QPoint nativePoint = nativeOrigin + localPosition;
    const NativeTarget target = targetAtPoint(CGPointMake(nativePoint.x(), nativePoint.y()),
                                              static_cast<CGWindowID>(query.overlayWindow),
                                              query.excludedProcesses,
                                              query.elementMode && !query.unstableProcesses.isEmpty());
    if (nativeWindow)
        *nativeWindow = static_cast<quintptr>(target.windowId);
    const QRect screenRect(QPoint(), logicalScreenSize);
    if (target.bounds.size.width <= 0.0 || target.bounds.size.height <= 0.0)
        return {screenRect};

    QList<QRect> targets;
    const QRect window = localRect(target.bounds, nativeOrigin, logicalScreenSize);
    if (!window.isEmpty() && window.contains(localPosition))
        targets.append(window);

    if (query.elementMode && query.accessibility && !processMatches(target, query.unstableProcesses)) {
        const QList<CGRect> hierarchy = axElementBounds(CGPointMake(nativePoint.x(), nativePoint.y()),
                                                         target.processId);
        for (const CGRect &elementBounds : hierarchy) {
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

QList<QRect> captureTargetHierarchyAt(QPoint localPosition, QSize logicalScreenSize,
                                      WId overlayWindow, bool elementMode,
                                      const QStringList &excludedProcesses,
                                      const QStringList &unstableProcesses,
                                      quintptr *nativeWindow)
{
    return captureTargetHierarchy(prepareCaptureTargetQuery(localPosition, logicalScreenSize,
                                       overlayWindow, elementMode, excludedProcesses,
                                       unstableProcesses), nativeWindow);
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
    if (!nativeWindow || QGuiApplication::platformName() != QStringLiteral("cocoa")
        || !CGPreflightScreenCaptureAccess())
        return {};

    @autoreleasepool {
        // The completion blocks retain their result state, including when a
        // slow system capture finishes after the synchronous caller times out.
        const auto result = std::make_shared<std::promise<QImage>>();
        auto future = result->get_future();
        [SCShareableContent getShareableContentExcludingDesktopWindows:YES
            onScreenWindowsOnly:YES completionHandler:^(SCShareableContent *content, NSError *) {
                @autoreleasepool {
                    SCWindow *target = nil;
                    for (SCWindow *window in content.windows) {
                        if (window.windowID == static_cast<CGWindowID>(nativeWindow)) {
                            target = window;
                            break;
                        }
                    }
                    if (!target) {
                        result->set_value({});
                        return;
                    }
                    SCContentFilter *filter = [[SCContentFilter alloc] initWithDesktopIndependentWindow:target];
                    SCStreamConfiguration *configuration = [[SCStreamConfiguration alloc] init];
                    configuration.width = qMax(1, qRound(filter.contentRect.size.width * filter.pointPixelScale));
                    configuration.height = qMax(1, qRound(filter.contentRect.size.height * filter.pointPixelScale));
                    // Avoid the default subsampled video format and display
                    // color space when producing a lossless sRGB screenshot.
                    configuration.pixelFormat = kCVPixelFormatType_32BGRA;
                    configuration.colorSpaceName = kCGColorSpaceSRGB;
                    configuration.showsCursor = NO;
                    configuration.ignoreShadowsSingleWindow = YES;
                    [SCScreenshotManager captureImageWithFilter:filter configuration:configuration
                        completionHandler:^(CGImageRef image, NSError *) {
                            result->set_value(imageFromMacCGImage(image));
                        }];
#if !__has_feature(objc_arc)
                    [configuration release];
                    [filter release];
#endif
                }
            }];
        if (future.wait_for(std::chrono::seconds(3)) != std::future_status::ready)
            return {};
        return future.get();
    }
}

QRect macWindowBounds(quintptr nativeWindow)
{
    const CGRect bounds = targetForWindow(nativeWindow).bounds;
    return QRect(qRound(bounds.origin.x), qRound(bounds.origin.y),
                   qRound(bounds.size.width), qRound(bounds.size.height));
}

bool macWindowExists(quintptr nativeWindow)
{
    return targetForWindow(nativeWindow).windowId != 0;
}

bool activateMacWindow(quintptr nativeWindow)
{
    const NativeTarget target = targetForWindow(nativeWindow);
    if (!target.windowId || target.processId <= 0)
        return false;
    @autoreleasepool {
        NSRunningApplication *application = [NSRunningApplication runningApplicationWithProcessIdentifier:target.processId];
        if (!application)
            return false;
        const bool activated = [application activateWithOptions:0];
        if (!AXIsProcessTrusted())
            return activated;
        AXUIElementRef appElement = AXUIElementCreateApplication(target.processId);
        AXUIElementSetMessagingTimeout(appElement, 0.08f);
        CFTypeRef windows = nullptr;
        if (AXUIElementCopyAttributeValue(appElement, kAXWindowsAttribute, &windows) == kAXErrorSuccess
            && windows && CFGetTypeID(windows) == CFArrayGetTypeID()) {
            const auto array = static_cast<CFArrayRef>(windows);
            for (CFIndex index = 0; index < CFArrayGetCount(array); ++index) {
                const auto window = static_cast<AXUIElementRef>(CFArrayGetValueAtIndex(array, index));
                CFTypeRef positionValue = nullptr;
                CFTypeRef sizeValue = nullptr;
                AXUIElementSetMessagingTimeout(window, 0.08f);
                AXUIElementCopyAttributeValue(window, kAXPositionAttribute, &positionValue);
                AXUIElementCopyAttributeValue(window, kAXSizeAttribute, &sizeValue);
                CGPoint position{};
                CGSize size{};
                const bool matches = positionValue && sizeValue
                    && CFGetTypeID(positionValue) == AXValueGetTypeID()
                    && CFGetTypeID(sizeValue) == AXValueGetTypeID()
                    && AXValueGetValue(static_cast<AXValueRef>(positionValue), static_cast<AXValueType>(kAXValueCGPointType), &position)
                    && AXValueGetValue(static_cast<AXValueRef>(sizeValue), static_cast<AXValueType>(kAXValueCGSizeType), &size)
                    && qAbs(position.x - target.bounds.origin.x) < 1.0
                    && qAbs(position.y - target.bounds.origin.y) < 1.0
                    && qAbs(size.width - target.bounds.size.width) < 1.0
                    && qAbs(size.height - target.bounds.size.height) < 1.0;
                if (positionValue)
                    CFRelease(positionValue);
                if (sizeValue)
                    CFRelease(sizeValue);
                if (matches) {
                    AXUIElementSetAttributeValue(window, kAXMainAttribute, kCFBooleanTrue);
                    AXUIElementPerformAction(window, kAXRaiseAction);
                    break;
                }
            }
        }
        if (windows)
            CFRelease(windows);
        CFRelease(appElement);
        return activated;
    }
}

bool postMacScrollStep(quintptr nativeWindow, const QPoint &globalPoint)
{
    if (!AXIsProcessTrusted() || !activateMacWindow(nativeWindow))
        return false;
    CGEventRef event = CGEventCreateScrollWheelEvent(nullptr, kCGScrollEventUnitPixel, 1, -120);
    if (!event)
        return false;
    const CGPoint point = CGPointMake(globalPoint.x(), globalPoint.y());
    CGEventSetLocation(event, point);
    // New wheel events have a zero timestamp, which AppKit can discard.
    // Quartz timestamps use nanoseconds since system startup.
    CGEventSetTimestamp(event, static_cast<CGEventTimestamp>(NSProcessInfo.processInfo.systemUptime * 1.0e9));
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
    return true;
}

} // namespace snipaste::platform
