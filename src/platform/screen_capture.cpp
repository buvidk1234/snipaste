#include "platform/screen_capture.h"

#include <QScreen>
#include <QPixmap>

#ifdef Q_OS_WIN
#include <qscreen_platform.h>
#include <windows.h>

namespace {

struct CaptureResources {
    HDC desktop = nullptr;
    HDC memory = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;
};

void releaseCaptureResources(void *info)
{
    auto *resources = static_cast<CaptureResources *>(info);
    if (!resources)
        return;
    if (resources->memory && resources->bitmap) {
        SelectObject(resources->memory, resources->previous);
        DeleteObject(resources->bitmap);
    }
    if (resources->memory)
        DeleteDC(resources->memory);
    if (resources->desktop)
        ReleaseDC(nullptr, resources->desktop);
    delete resources;
}

QRect nativeScreenRect(QScreen *screen)
{
    HMONITOR monitor = nullptr;
    if (const auto *native = screen->nativeInterface<QNativeInterface::QWindowsScreen>())
        monitor = native->handle();
    if (!monitor)
        return {};

    MONITORINFO info{sizeof(MONITORINFO)};
    if (!GetMonitorInfoW(monitor, &info))
        return {};

    return QRect(info.rcMonitor.left, info.rcMonitor.top,
                 info.rcMonitor.right - info.rcMonitor.left,
                 info.rcMonitor.bottom - info.rcMonitor.top);
}

QImage captureNativeRect(const QRect &rect)
{
    const int width = rect.width();
    const int height = rect.height();
    if (width <= 0 || height <= 0)
        return {};

    HDC desktop = GetDC(nullptr);
    if (!desktop)
        return {};
    HDC memory = CreateCompatibleDC(desktop);
    if (!memory) {
        ReleaseDC(nullptr, desktop);
        return {};
    }

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = width;
    bitmapInfo.bmiHeader.biHeight = -height;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    void *bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(desktop, &bitmapInfo, DIB_RGB_COLORS,
                                      &bits, nullptr, 0);
    if (!bitmap || !bits) {
        if (bitmap)
            DeleteObject(bitmap);
        DeleteDC(memory);
        ReleaseDC(nullptr, desktop);
        return {};
    }

    HGDIOBJ previous = SelectObject(memory, bitmap);
    const BOOL copied = BitBlt(memory, 0, 0, width, height, desktop,
                               rect.left(), rect.top(),
                               SRCCOPY | CAPTUREBLT);
    GdiFlush();

    if (copied) {
        // BI_RGB supplies BGR plus an unused high byte, not premultiplied
        // alpha. Normalize it to opaque RGB32 while keeping the DIB as the
        // backing store; do not allocate a second desktop-sized image.
        auto *pixels = static_cast<quint32 *>(bits);
        for (qsizetype i = 0, count = qsizetype(width) * height; i < count; ++i)
            pixels[i] |= 0xff000000u;
        auto *resources = new CaptureResources{desktop, memory, bitmap, previous};
        QImage result(static_cast<uchar *>(bits), width, height, width * 4,
                      QImage::Format_RGB32,
                      releaseCaptureResources, resources);
        result.setDevicePixelRatio(1.0);
        return result;
    }

    SelectObject(memory, previous);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, desktop);
    return {};
}

} // namespace
#endif

namespace snipaste::platform {

QImage captureScreen(QScreen *screen)
{
    if (!screen)
        return {};
#ifdef Q_OS_WIN
    QImage native = captureNativeRect(nativeScreenRect(screen));
    if (!native.isNull())
        return native;
#endif
    QImage result = screen->grabWindow(0).toImage();
    result.setDevicePixelRatio(1.0);
    return result;
}

QRect screenCapturePixelRect(const QRect &screenGeometry, const QSize &pixelSize,
                            const QRect &globalLogicalRegion)
{
    if (screenGeometry.isEmpty() || pixelSize.isEmpty() || globalLogicalRegion.isEmpty())
        return {};
    const QRect local = globalLogicalRegion.translated(-screenGeometry.topLeft());
    const qreal scaleX = qreal(pixelSize.width()) / screenGeometry.width();
    const qreal scaleY = qreal(pixelSize.height()) / screenGeometry.height();
    const int left = qRound(local.left() * scaleX);
    const int top = qRound(local.top() * scaleY);
    const int right = qRound((qreal(local.left()) + local.width()) * scaleX);
    const int bottom = qRound((qreal(local.top()) + local.height()) * scaleY);
    return QRect(left, top, qMax(1, right - left), qMax(1, bottom - top))
        .intersected(QRect(QPoint(), pixelSize));
}

QImage captureScreenRegion(QScreen *screen, const QRect &globalLogicalRegion)
{
    if (!screen || globalLogicalRegion.isEmpty())
        return {};
#ifdef Q_OS_WIN
    const QRect monitor = nativeScreenRect(screen);
    if (!monitor.isEmpty()) {
        const QRect pixels = screenCapturePixelRect(screen->geometry(), monitor.size(), globalLogicalRegion);
        if (pixels.isEmpty())
            return {};
        QImage native = captureNativeRect(pixels.translated(monitor.topLeft()));
        if (!native.isNull())
            return native;
    }
#endif
    // Keep Qt's capture path as a fallback; apply the same native-pixel edge
    // mapping even if its backing image has a different device pixel ratio.
    const QImage desktop = captureScreen(screen);
    const QRect pixels = screenCapturePixelRect(screen->geometry(), desktop.size(), globalLogicalRegion);
    return pixels.isEmpty() ? QImage() : desktop.copy(pixels);
}

} // namespace snipaste::platform
