#include "ui/snip/snip_overlay.h"
#include "ui/annotation/annotation_dialog.h"
#include "platform/screen_capture.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFrame>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QPointer>
#include <QScreen>
#include <cstdio>
#include <memory>
#include <vector>

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

// Native diagnostic only: sends events to its own widgets, never global
// hotkeys or mouse input. It does not save screenshots or application settings.
static void mouse(QWidget *widget, QEvent::Type type, QPoint point)
{
    const auto button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
    const auto buttons = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton;
    QMouseEvent event(type, QPointF(point), QPointF(widget->mapToGlobal(point)),
                      button, buttons, Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
}

static QJsonObject resources()
{
    QJsonObject result;
#ifdef Q_OS_WIN
    const auto process = GetCurrentProcess();
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory), sizeof(memory));
    result["workingSetBytes"] = double(memory.WorkingSetSize);
    result["privateBytes"] = double(memory.PrivateUsage);
    // Task Manager reports resident private pages, which differs from both
    // total working set and committed private memory. Record it explicitly.
    std::vector<quintptr> pages(size_t(memory.WorkingSetSize / 4096) + 8192);
    if (QueryWorkingSet(process, pages.data(), DWORD(pages.size() * sizeof(quintptr)))) {
        const auto *set = reinterpret_cast<const PSAPI_WORKING_SET_INFORMATION *>(pages.data());
        quint64 residentPrivate = 0;
        for (ULONG_PTR i = 0; i < set->NumberOfEntries; ++i)
            if (!set->WorkingSetInfo[i].Shared)
                residentPrivate += 4096;
        result["privateWorkingSetBytes"] = double(residentPrivate);
    }
    result["gdiObjects"] = int(GetGuiResources(process, GR_GDIOBJECTS));
    IO_COUNTERS io{};
    GetProcessIoCounters(process, &io);
    result["readBytes"] = double(io.ReadTransferCount);
    result["writeBytes"] = double(io.WriteTransferCount);
    FILETIME created{}, exited{}, kernel{}, user{};
    GetProcessTimes(process, &created, &exited, &kernel, &user);
    const auto ticks = [](FILETIME value) {
        return (quint64(value.dwHighDateTime) << 32) | value.dwLowDateTime;
    };
    result["cpuMs"] = double(ticks(kernel) + ticks(user)) / 10000.0;
#endif
    return result;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    const bool prepared = app.arguments().contains(QStringLiteral("--prepared"));
    QJsonObject report;
    report["platform"] = QGuiApplication::platformName();
    report["prepared"] = prepared;
    report["beforeControls"] = resources();
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen)
        return 2;
    std::unique_ptr<snipaste::ui::SnipOverlay> retained;
    QElapsedTimer timer;
    timer.start();
#ifdef SNIPASTE_HAS_PREPARED_CAPTURE
    if (prepared)
        retained.reset(snipaste::ui::SnipOverlay::createPrepared(screen));
#else
    if (prepared)
        return 3;
#endif
    report["prepareMs"] = timer.nsecsElapsed() / 1e6;
    report["afterControls"] = resources();
    QJsonArray sessions;
    for (int i = 0; i < 12; ++i) {
        timer.restart();
        QPointer<snipaste::ui::SnipOverlay> overlay;
#ifdef SNIPASTE_HAS_PREPARED_CAPTURE
        if (prepared) {
            overlay = retained.get();
            overlay->beginCapture(screen);
        } else
#endif
        {
            overlay = new snipaste::ui::SnipOverlay(screen);
        }
        overlay->setExitOnFocusLoss(false);
        overlay->setDetectionOptions(false, false, false);
        overlay->setCaptureSound(false, {}, false);
        overlay->show();
        QApplication::processEvents();
        const double firstPaintMs = timer.nsecsElapsed() / 1e6;
        timer.restart();
        mouse(overlay, QEvent::MouseButtonPress, QPoint(40, 40));
        mouse(overlay, QEvent::MouseMove, QPoint(400, 300));
        mouse(overlay, QEvent::MouseButtonRelease, QPoint(400, 300));
        QApplication::processEvents();
        const double selectionMs = timer.nsecsElapsed() / 1e6;
        auto *toolbar = overlay->findChild<QFrame *>(QStringLiteral("captureToolbar"));
        if (!toolbar || !toolbar->isVisible())
            return 4;
        sessions.append(QJsonObject{{"frameMs", firstPaintMs}, {"selectionMs", selectionMs},
                                     {"widgets", overlay->findChildren<QWidget *>().size()},
                                     {"active", resources()}});
        overlay->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QApplication::processEvents();
    }
    report["sessions"] = sessions;
    report["afterSessions"] = resources();

    // Same native pixels for both revisions, including overlapping effects.
    QImage source(3840, 2160, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < source.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(source.scanLine(y));
        for (int x = 0; x < source.width(); ++x)
            row[x] = qRgb((x * 7 + y) % 256, (y * 13 + x) % 256, (x + y * 3) % 256);
    }
    snipaste::ui::AnnotationCanvas canvas(source);
    canvas.resize(source.size());
    using Tool = snipaste::ui::AnnotationCanvas::Tool;
    canvas.setTool(Tool::Rectangle);
    mouse(&canvas, QEvent::MouseButtonPress, QPoint(80, 80));
    mouse(&canvas, QEvent::MouseButtonRelease, QPoint(500, 400));
    canvas.setEffectShape(snipaste::ui::AnnotationCanvas::EffectShape::Rectangle);
    for (const auto tool : {Tool::Mosaic, Tool::Blur, Tool::Magnifier}) {
        canvas.setTool(tool);
        mouse(&canvas, QEvent::MouseButtonPress, QPoint(110, 110));
        mouse(&canvas, QEvent::MouseButtonRelease, QPoint(380, 280));
    }
    timer.restart();
    const QImage committed = canvas.fullRenderedImage(false);
    report["renderMs"] = timer.nsecsElapsed() / 1e6;
    timer.restart();
    QImage exported;
    for (int i = 0; i < 20; ++i)
        exported = canvas.fullRenderedImage();
    report["exports20Ms"] = timer.nsecsElapsed() / 1e6;
    report["samePixels"] = committed == exported;
    report["pixelSha256"] = QString::fromLatin1(QCryptographicHash::hash(
        QByteArrayView(reinterpret_cast<const char *>(exported.constBits()), exported.sizeInBytes()),
        QCryptographicHash::Sha256).toHex());
    const QImage desktop = snipaste::platform::captureScreen(screen);
    report["desktopPixels"] = QJsonArray{desktop.width(), desktop.height()};
    report["desktopOpaque"] = !desktop.isNull() && desktop.pixelColor(0, 0).alpha() == 255;
    const QByteArray json = QJsonDocument(report).toJson();
    std::fwrite(json.constData(), 1, size_t(json.size()), stdout);
    return 0;
}
