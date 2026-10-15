#include "ui/snip/snip_overlay.h"
#include "ui/annotation/annotation_canvas.h"
#include "platform/screen_capture.h"
#include "core/config/config_store.h"
#include "ui/paste/paste_manager.h"
#include "ui/paste/paste_window.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFrame>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QPointer>
#include <QScreen>
#include <QTemporaryDir>
#include <QDir>
#include <QImageReader>
#include <QIcon>
#include <QFile>
#include <QPaintEvent>
#include <QSslSocket>
#include <QTimer>
#include <QVariantAnimation>
#include <cstdio>
#include <memory>
#include <vector>

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

class PerformanceApplication final : public QApplication
{
public:
    using QApplication::QApplication;
    QWidget *hoverOverlay = nullptr;
    QElapsedTimer hoverClock;
    QJsonArray hoverPaints;

    bool notify(QObject *receiver, QEvent *event) override
    {
        if (receiver != hoverOverlay || event->type() != QEvent::Paint)
            return QApplication::notify(receiver, event);
        const auto *paint = static_cast<QPaintEvent *>(event);
        qint64 area = 0;
        for (const QRect rect : paint->region())
            area += qint64(rect.width()) * rect.height();
        const double start = hoverClock.nsecsElapsed() / 1e6;
        const bool result = QApplication::notify(receiver, event);
        hoverPaints.append(QJsonObject{{"atMs", start},
            {"paintMs", hoverClock.nsecsElapsed() / 1e6 - start}, {"area", double(area)}});
        return result;
    }
};

static int hoverAnimationProbe(PerformanceApplication &app)
{
    auto *screen = app.primaryScreen();
    std::unique_ptr<snipaste::ui::SnipOverlay> overlay(snipaste::ui::SnipOverlay::createPrepared(screen));
    overlay->setWindowFlag(Qt::WindowTransparentForInput);
    overlay->setWindowFlag(Qt::WindowDoesNotAcceptFocus);
    overlay->setAttribute(Qt::WA_ShowWithoutActivating);
    overlay->beginCapture(screen);
    overlay->setExitOnFocusLoss(false);
    const QRect first(40, 60, screen->size().width() / 3, screen->size().height() / 3);
    const QRect second(screen->size().width() / 3, 100,
                       screen->size().width() * 3 / 5, screen->size().height() * 3 / 4);
    QRect target = first;
    overlay->setTargetDetector([&](QPoint, bool) { return target; });
    overlay->show();
    app.processEvents();
    QMouseEvent initialMove(QEvent::MouseMove, first.center(), overlay->mapToGlobal(first.center()),
                            Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(overlay.get(), &initialMove);
    auto *animation = overlay->findChild<QVariantAnimation *>(QStringLiteral("hoverSelectionAnimation"));
    animation->setCurrentTime(animation->duration());
    const auto wait = [](int ms) {
        QEventLoop loop;
        QTimer::singleShot(ms, &loop, &QEventLoop::quit);
        loop.exec();
    };
    wait(50);
    QJsonArray transitions;
    bool complete = true;
    for (int i = 0; i < 12; ++i) {
        if (!overlay->isVisible()) {
            complete = false;
            break;
        }
        target = i % 2 == 0 ? second : first;
        app.hoverPaints = {};
        app.hoverClock.start();
        app.hoverOverlay = overlay.get();
        QMouseEvent move(QEvent::MouseMove, target.center(), overlay->mapToGlobal(target.center()),
                         Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(overlay.get(), &move);
        wait(130);
        app.hoverOverlay = nullptr;
        transitions.append(app.hoverPaints);
        complete = complete && overlay->captureGeometry().region.isEmpty()
            && animation->endValue().toRectF() == QRectF(target) && !app.hoverPaints.isEmpty();
    }
    overlay->close();
    const QJsonObject report{{"complete", complete}, {"platform", app.platformName()}, {"refreshHz", screen->refreshRate()},
        {"logicalSize", QJsonArray{screen->size().width(), screen->size().height()}},
        {"devicePixelRatio", screen->devicePixelRatio()}, {"transitions", transitions}};
    const QByteArray json = QJsonDocument(report).toJson();
    std::fwrite(json.constData(), 1, size_t(json.size()), stdout);
    return complete ? 0 : 6;
}

// Native diagnostic only: sends events to its own widgets, never global
// hotkeys or mouse input. Disk measurements use an isolated temporary directory.
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
    result["peakWorkingSetBytes"] = double(memory.PeakWorkingSetSize);
    result["peakCommitBytes"] = double(memory.PeakPagefileUsage);
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

static QJsonObject imageFormats(QTemporaryDir &temporary)
{
    QImage formatImage(32, 24, QImage::Format_RGB32);
    formatImage.fill(Qt::magenta);
    bool formatsOk = true;
    for (const char *format : {"PNG", "JPEG", "BMP"}) {
        const QString path = temporary.filePath(QString::fromLatin1(format));
        formatsOk = formatImage.save(path, format) && QImage(path).size() == formatImage.size() && formatsOk;
    }
    QFile svg(temporary.filePath(QStringLiteral("test.svg")));
    bool svgOk = svg.open(QIODevice::WriteOnly);
    if (svgOk) {
        svg.write("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"20\"><rect width=\"20\" height=\"20\" fill=\"red\"/></svg>");
        svg.close();
        svgOk = !QIcon(svg.fileName()).pixmap(20, 20).isNull();
    }
    return {{"packageImageFormatsOk", formatsOk}, {"packageSvgOk", svgOk}};
}

int main(int argc, char **argv)
{
    PerformanceApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    if (app.arguments().contains(QStringLiteral("--hover-animation")))
        return hoverAnimationProbe(app);
    if (app.arguments().contains(QStringLiteral("--package-formats-only"))) {
        QTemporaryDir temporary;
        if (!temporary.isValid())
            return 2;
        QJsonObject report = imageFormats(temporary);
        report["platform"] = QGuiApplication::platformName();
        report["sslSupported"] = QSslSocket::supportsSsl();
        const QByteArray json = QJsonDocument(report).toJson();
        std::fwrite(json.constData(), 1, size_t(json.size()), stdout);
        return report["packageImageFormatsOk"].toBool() && report["packageSvgOk"].toBool()
            && report["sslSupported"].toBool() ? 0 : 5;
    }
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
    QTemporaryDir temporary;
    if (!temporary.isValid())
        return 2;
    snipaste::core::ConfigStore config(temporary.filePath(QStringLiteral("probe.ini")));
    for (int i = 0; i < 1000; ++i)
        config.setValue(QStringLiteral("Saved/key%1").arg(i), i);
    timer.restart();
    int defaults = 0;
    for (int i = 0; i < 10000; ++i)
        defaults += config.value(QStringLiteral("missing/default"), 1).toInt();
    report["defaultLookups10000Ms"] = timer.nsecsElapsed() / 1e6;
    report["defaultValuesCorrect"] = defaults == 10000;

    snipaste::ui::PasteManager manager;
    manager.setFlashNew(false);
    manager.setActivateNew(false);
    manager.rememberCapture(source, QRect(QPoint(20, 20), source.size()));
    timer.restart();
    for (int i = 0; i < 8; ++i) {
        auto *pin = manager.createPaste(source);
        pin->hide();
    }
    report["sameCapturePins8Ms"] = timer.nsecsElapsed() / 1e6;
    const QString pasteDirectory = temporary.filePath(QStringLiteral("pastes"));
    manager.saveState(pasteDirectory);
    const QJsonObject beforeSave = resources();
    timer.restart();
    for (int i = 0; i < 3; ++i)
        manager.saveState(pasteDirectory);
    report["unchangedPinsSave3Ms"] = timer.nsecsElapsed() / 1e6;
    const QJsonObject afterSave = resources();
    report["unchangedPinsWriteBytes"] = afterSave.value("writeBytes").toDouble()
        - beforeSave.value("writeBytes").toDouble();
    report["persistedPinImages"] = QDir(pasteDirectory).entryList({QStringLiteral("paste-*.png")}, QDir::Files).size();
    manager.destroyAll();
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    report["resourcesAfterTools"] = resources();

    const QJsonObject formats = imageFormats(temporary);
    for (auto it = formats.begin(); it != formats.end(); ++it)
        report[it.key()] = it.value();
    const QByteArray json = QJsonDocument(report).toJson();
    std::fwrite(json.constData(), 1, size_t(json.size()), stdout);
    return 0;
}
