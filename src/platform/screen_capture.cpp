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

} // namespace
#endif

namespace snipaste::platform {

QImage captureScreen(QScreen *screen)
{
    if (!screen)
        return {};

#ifdef Q_OS_WIN
    HMONITOR monitor = nullptr;
    if (const auto *native = screen->nativeInterface<QNativeInterface::QWindowsScreen>())
        monitor = native->handle();
    if (!monitor)
        return screen->grabWindow(0).toImage();

    MONITORINFO info{sizeof(MONITORINFO)};
    if (!GetMonitorInfoW(monitor, &info))
        return screen->grabWindow(0).toImage();

    const int width = info.rcMonitor.right - info.rcMonitor.left;
    const int height = info.rcMonitor.bottom - info.rcMonitor.top;
    if (width <= 0 || height <= 0)
        return {};

    HDC desktop = GetDC(nullptr);
    if (!desktop)
        return screen->grabWindow(0).toImage();
    HDC memory = CreateCompatibleDC(desktop);
    if (!memory) {
        ReleaseDC(nullptr, desktop);
        return screen->grabWindow(0).toImage();
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
        return screen->grabWindow(0).toImage();
    }

    HGDIOBJ previous = SelectObject(memory, bitmap);
    const BOOL copied = BitBlt(memory, 0, 0, width, height, desktop,
                               info.rcMonitor.left, info.rcMonitor.top,
                               SRCCOPY | CAPTUREBLT);
    GdiFlush();

    if (copied) {
        // Windows' 32-bit DIB layout is BGRA, which is the in-memory layout
        // Qt uses for Format_ARGB32_Premultiplied on little-endian Windows.
        // Keep the DIB alive as the QImage backing store.  The old path copied
        // the complete frame a second time before returning, which was both a
        // measurable part of the hotkey latency and an unnecessary peak-memory
        // spike for a 4K desktop.
        auto *resources = new CaptureResources{desktop, memory, bitmap, previous};
        QImage result(static_cast<uchar *>(bits), width, height, width * 4,
                      QImage::Format_ARGB32_Premultiplied,
                      releaseCaptureResources, resources);
        result.setDevicePixelRatio(1.0);
        return result;
    }

    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, desktop);
    return screen->grabWindow(0).toImage();
#else
    QImage result = screen->grabWindow(0).toImage();
    result.setDevicePixelRatio(1.0);
    return result;
#endif
}

} // namespace snipaste::platform
