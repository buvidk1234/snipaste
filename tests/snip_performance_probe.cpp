#include "ui/snip/snip_overlay.h"
#include "ui/annotation/annotation_canvas.h"
#include "platform/screen_capture.h"
#include "core/config/config_store.h"
#include "ui/paste/paste_manager.h"
#include "ui/paste/paste_window.h"

#include <QApplication>
#include <QBackingStore>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFrame>
#include <QGraphicsProxyWidget>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
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
#include <algorithm>
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
    bool recordDragPaints = false;
    QMap<QString, int> dragPaints;

    bool notify(QObject *receiver, QEvent *event) override
    {
        if (recordDragPaints && event->type() == QEvent::Paint)
            ++dragPaints[receiver->objectName().isEmpty()
                ? QString::fromLatin1(receiver->metaObject()->className()) : receiver->objectName()];
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

static int pasteMemoryProbe(QApplication &app)
{
    QJsonArray samples;
    QPointer<snipaste::ui::PasteWindow> inspected;
    const auto settle = [] {
        QEventLoop loop;
        QTimer::singleShot(100, &loop, &QEventLoop::quit);
        loop.exec();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    };
    const auto sample = [&](const char *stage) {
        settle();
        QJsonObject entry = resources();
        entry["stage"] = QString::fromLatin1(stage);
        entry["widgets"] = QApplication::allWidgets().size();
        if (inspected)
        {
            entry["sourceKey"] = double(inspected->sourceImage().cacheKey());
            entry["displayKey"] = double(inspected->displayedImage().cacheKey());
            entry["backingPixels"] = inspected->backingStore()->size().width()
                * inspected->backingStore()->size().height();
            qint64 shadowPixels = 0;
            for (auto *shadow : inspected->findChildren<QWidget *>(QStringLiteral("pasteShadow")))
                if (auto *store = shadow->backingStore())
                    shadowPixels += qint64(store->size().width()) * store->size().height();
            entry["shadowBackingPixels"] = double(shadowPixels);
            if (auto *view = inspected->findChild<QGraphicsView *>(QStringLiteral("pasteEditor")))
                for (auto *item : view->scene()->items())
                    if (auto *proxy = qgraphicsitem_cast<QGraphicsProxyWidget *>(item))
                    {
                        entry["proxyCacheMode"] = int(proxy->cacheMode());
                        auto *canvas = qobject_cast<snipaste::ui::AnnotationCanvas *>(proxy->widget());
                        entry["canvasSourceKey"] = double(canvas->unannotatedImage().cacheKey());
                    }
        }
        samples.append(entry);
    };
    sample("application");
    std::unique_ptr<snipaste::ui::SnipOverlay> capture(
        snipaste::ui::SnipOverlay::createPrepared(app.primaryScreen()));
    sample("capturePrepared");
    snipaste::ui::PasteManager manager;
    manager.setFlashNew(false);
    manager.setActivateNew(false);
    manager.setShadowEnabled(!app.arguments().contains(QStringLiteral("--no-shadow")));
    sample("pastePrepared");
    QImage source(3840, 2160, QImage::Format_ARGB32_Premultiplied);
    source.fill(QColor(57, 62, 72));
    sample("source4k");
    for (int i = 0; i < 3; ++i)
    {
        auto *pin = manager.createPaste(source);
        inspected = pin;
        if (app.arguments().contains(QStringLiteral("--inverted")))
            pin->restoreTransform(0, false, false, false, true, 1.0, false, true);
        sample("pinVisible");
        pin->setToolbarVisible(true);
        sample("editorVisible");
        pin->setToolbarVisible(false);
        sample("editorClosed");
        QKeyEvent close(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(pin, &close);
        sample("pinClosed");
    }
    manager.destroyAll();
    sample("pinsDestroyed");
    source = {};
    sample("sourceReleased");
    for (int i = 0; i < 8; ++i)
    {
        QImage image(1920, 1080, QImage::Format_RGB32);
        image.fill(QColor(57, 62, 72));
        auto *pin = manager.createPaste(image);
        pin->setToolbarVisible(true);
        settle();
        pin->close();
        sample("createEditDestroy");
    }
    const QJsonObject report{{"platform", app.platformName()},
        {"devicePixelRatio", app.primaryScreen()->devicePixelRatio()}, {"samples", samples}};
    const QByteArray json = QJsonDocument(report).toJson();
    std::fwrite(json.constData(), 1, size_t(json.size()), stdout);
    return 0;
}

static int pasteDragProbe(PerformanceApplication &app)
{
    snipaste::ui::PasteManager manager;
    manager.setFlashNew(false);
    manager.setActivateNew(false);
    manager.setShadowEnabled(!app.arguments().contains(QStringLiteral("--no-shadow")));
    manager.prepareEditorCache();
    QImage source(3840, 2160, QImage::Format_ARGB32_Premultiplied);
    source.fill(QColor(57, 62, 72));
    auto *pin = manager.createPaste(source);
    pin->setObjectName(QStringLiteral("dragPin"));
    pin->scaleBy(0.5);
    QJsonArray scenarios;
    bool unchanged = true;
    const int scenarioCount = app.arguments().contains(QStringLiteral("--drag-plain")) ? 1 : 3;
    for (int scenario = 0; scenario < scenarioCount; ++scenario)
    {
        pin->setToolbarVisible(scenario != 0);
        if (scenario == 2)
            pin->restoreTransform(0, false, false, false, true, 0.5, false, true);
        QEventLoop settle;
        QTimer::singleShot(100, &settle, &QEventLoop::quit);
        settle.exec();
        const auto sourceKey = pin->sourceImage().cacheKey();
        const auto displayKey = pin->displayedImage().cacheKey();
        const QPoint press = pin->rect().center();
        const QPoint start = pin->mapToGlobal(press);
        QMouseEvent down(QEvent::MouseButtonPress, press, start,
                         Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(pin, &down);
        // Selection changes the border once; measure the following movement.
        QCoreApplication::sendPostedEvents(nullptr, QEvent::UpdateRequest);
        app.processEvents();
        const double cpuBefore = resources()["cpuMs"].toDouble();
        app.dragPaints.clear();
        app.recordDragPaints = true;
        std::vector<double> durations;
        QPoint last;
        for (int frame = 0; frame < 120; ++frame)
        {
            last = start + QPoint(frame < 60 ? frame * 2 : (120 - frame) * 2, (frame % 30) / 2);
            QMouseEvent move(QEvent::MouseMove, pin->mapFromGlobal(last), last,
                             Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QElapsedTimer timer;
            timer.start();
            QApplication::sendEvent(pin, &move);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::UpdateRequest);
            app.processEvents();
            durations.push_back(timer.nsecsElapsed() / 1e6);
        }
        app.recordDragPaints = false;
        QMouseEvent up(QEvent::MouseButtonRelease, pin->mapFromGlobal(last), last,
                       Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(pin, &up);
        unchanged = unchanged && sourceKey == pin->sourceImage().cacheKey()
            && displayKey == pin->displayedImage().cacheKey();
        std::sort(durations.begin(), durations.end());
        QJsonObject paints;
        for (auto it = app.dragPaints.cbegin(); it != app.dragPaints.cend(); ++it)
            paints[it.key()] = it.value();
        scenarios.append(QJsonObject{{"scenario", scenario}, {"frames", int(durations.size())},
            {"medianMs", durations[durations.size() / 2]},
            {"p95Ms", durations[size_t(durations.size() * 0.95)]}, {"maxMs", durations.back()},
            {"cpuMs", resources()["cpuMs"].toDouble() - cpuBefore}, {"paints", paints}});
    }
    const QByteArray report = QJsonDocument(QJsonObject{{"platform", app.platformName()},
        {"devicePixelRatio", pin->devicePixelRatioF()}, {"sourceUnchanged", unchanged},
        {"scenarios", scenarios}}).toJson();
    std::fwrite(report.constData(), 1, size_t(report.size()), stdout);
    return unchanged ? 0 : 7;
}

int main(int argc, char **argv)
{
    PerformanceApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    if (app.arguments().contains(QStringLiteral("--paste-drag")))
        return pasteDragProbe(app);
    if (app.arguments().contains(QStringLiteral("--paste-memory")))
        return pasteMemoryProbe(app);
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
