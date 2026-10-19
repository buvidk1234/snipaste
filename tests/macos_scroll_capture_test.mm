#include "platform/windows/capture_target.h"
#include "platform/macos/capture.h"
#include "platform/macos/system.h"
#include "ui/snip/scroll_capture_panel.h"
#include "ui/snip/scroll_capture_preview.h"
#include "ui/snip/snip_overlay.h"

#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDir>
#include <QFrame>
#include <QPainter>
#include <QPointer>
#include <QScreen>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>
#include <QWheelEvent>
#include <QWindow>
#include <ApplicationServices/ApplicationServices.h>
#import <AppKit/AppKit.h>
#include <memory>

namespace snipaste::ui {

class NativeScrollPage final : public QWidget
{
public:
    NativeScrollPage() : m_document(420, 1000, QImage::Format_RGB32)
    {
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
        quint32 random = 71237;
        for (int y = 0; y < m_document.height(); ++y) {
            auto *row = reinterpret_cast<QRgb *>(m_document.scanLine(y));
            for (int x = 0; x < m_document.width(); ++x) {
                random = random * 1664525u + 1013904223u;
                row[x] = qRgb(32 + (random & 191), 32 + ((random >> 8) & 191),
                              32 + ((random >> 16) & 191));
            }
        }
        QPainter painter(&m_document);
        painter.fillRect(QRect(0, 0, 420, 20), QColor(220, 30, 40));
        painter.fillRect(QRect(140, 60, 130, 50), QColor(40, 190, 70));
        painter.fillRect(QRect(0, 350, 420, 30), QColor(25, 70, 220));
    }

    int scrollOffset() const { return m_offset; }
    void scrollDown() { m_offset += 120; repaint(); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.drawImage(QPoint(0, -m_offset), m_document);
    }

    void wheelEvent(QWheelEvent *event) override
    {
        const int delta = event->pixelDelta().isNull() ? event->angleDelta().y()
                                                       : event->pixelDelta().y();
        if (delta < 0)
            scrollDown();
        event->accept();
    }

private:
    QImage m_document;
    int m_offset = 0;
};

class MacScrollCaptureTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cancelCleansWindowsAndAllowsAnotherSession();
    void focusLossKeepsScrollSessionAlive();
    void captureFailureKeepsControlsAndFinishCleansSession();
    void nativeWindowFramesExcludeToolsAndStitchAfterScrolling();

private:
    static bool showTargetWindow(QWidget &target);
    static std::unique_ptr<SnipOverlay> prepareOverlay(QWidget &target);
    static bool startWithoutCapturing(SnipOverlay &overlay);
};

void MacScrollCaptureTest::initTestCase()
{
    if (QGuiApplication::platformName() != QStringLiteral("cocoa"))
        QSKIP("This lifecycle test requires the Cocoa platform plugin.");
    QApplication::setQuitOnLastWindowClosed(false);
    QVERIFY(QGuiApplication::primaryScreen());
}

bool MacScrollCaptureTest::showTargetWindow(QWidget &target)
{
    target.show();
    // QTest has no application.exec(): finish AppKit's initial launch before
    // asking the first owned window to become the foreground capture target.
    QCoreApplication::processEvents();
    target.raise();
    target.activateWindow();
    [NSApp activate];
    NSView *view = (__bridge NSView *)(void *)target.winId();
    if (!QTest::qWaitFor([&target, view] {
            return NSApp.active && view.window.visible && view.window.keyWindow
                && target.windowHandle()->isActive();
        })) {
        qWarning() << "Owned scroll target failed to activate:" << NSApp.active
                   << view.window.visible << view.window.keyWindow
                   << target.windowHandle()->isActive();
        return false;
    }
    return QTest::qWaitForWindowExposed(&target);
}

std::unique_ptr<SnipOverlay> MacScrollCaptureTest::prepareOverlay(QWidget &target)
{
    QScreen *screen = QGuiApplication::primaryScreen();
    target.setGeometry(QRect(screen->availableGeometry().topLeft() + QPoint(100, 80), QSize(420, 380)));
    target.setWindowTitle(QStringLiteral("Scroll capture lifecycle target"));
    if (!showTargetWindow(target))
        return {};

    auto overlay = std::unique_ptr<SnipOverlay>(SnipOverlay::createPrepared(screen));
    // Synthetic pixels avoid requesting Screen Recording permission. Native
    // window discovery and the actual session/UI lifecycle still run.
    overlay->m_captureActive = true;
    overlay->m_screenGeometry = screen->geometry();
    overlay->m_background = QImage(screen->size(), QImage::Format_RGB32);
    overlay->m_background.fill(Qt::white);
    overlay->m_cleanBackground = overlay->m_background;
    overlay->m_selection = QRect(target.mapToGlobal(QPoint(40, 40)) - screen->geometry().topLeft(),
                                  QSize(220, 180));
    overlay->m_selectionConfirmed = true;
    overlay->setDetectionOptions(false, false, false);
    overlay->show();
    return overlay;
}

bool MacScrollCaptureTest::startWithoutCapturing(SnipOverlay &overlay)
{
    quintptr target = 0;
    platform::captureTargetAt(overlay.m_selection.center(), overlay.size(), overlay.winId(), false,
                              {}, {}, &target);
    if (!target)
        return false;
    overlay.startScrollCapture();
    // The initial frame is queued. An invalid target makes that callback
    // stop before capture/activation; assertions below inject image results.
    overlay.m_scrollWindow = 0;
    return overlay.m_scrollCaptureActive;
}

void MacScrollCaptureTest::cancelCleansWindowsAndAllowsAnotherSession()
{
    QWidget target;
    auto overlay = prepareOverlay(target);
    QVERIFY(overlay);
    QSignalSpy closed(overlay.get(), &SnipOverlay::sessionClosed);

    for (int session = 0; session < 2; ++session) {
        QVERIFY(startWithoutCapturing(*overlay));
        QVERIFY(overlay->m_scrollCaptureActive);
        QVERIFY(!overlay->isVisible());
        QVERIFY(overlay->m_scrollPanel);
        QVERIFY(overlay->m_scrollPreview);
        QVERIFY(overlay->m_scrollPanel->isVisible());
        QVERIFY(overlay->m_scrollPreview->isVisible());
        QVERIFY(overlay->m_scrollOverlay);
        QVERIFY(overlay->m_scrollOverlay->isVisible());
        QCOMPARE(overlay->m_scrollOverlay->geometry(), overlay->geometry());
        NSView *maskView = (__bridge NSView *)(void *)overlay->m_scrollOverlay->winId();
        QVERIFY(maskView.window.ignoresMouseEvents);
        QVERIFY(!maskView.window.canBecomeKeyWindow);
        const QImage maskPixels = overlay->m_scrollOverlay->grab().toImage();
        const qreal maskScale = maskPixels.devicePixelRatio();
        const QPoint center = overlay->m_selection.center() * maskScale;
        const QPoint edge = QPoint(overlay->m_selection.left(), overlay->m_selection.center().y()) * maskScale;
        QCOMPARE(maskPixels.pixelColor(0, 0), overlay->m_maskColor);
        QCOMPARE(maskPixels.pixelColor(center).alpha(), 0);
        QVERIFY(maskPixels.pixelColor(edge).alpha() > 0);
        QVERIFY(overlay->m_scrollOverlay->testAttribute(Qt::WA_MacAlwaysShowToolWindow));
        QVERIFY(overlay->m_scrollPanel->testAttribute(Qt::WA_MacAlwaysShowToolWindow));
        QVERIFY(overlay->m_scrollPreview->testAttribute(Qt::WA_MacAlwaysShowToolWindow));
        QPointer<ScrollCapturePanel> panel = overlay->m_scrollPanel;
        QPointer<ScrollCapturePreview> preview = overlay->m_scrollPreview;
        QPointer<QWidget> mask = overlay->m_scrollOverlay;
        auto *cancel = panel->findChild<QToolButton *>(QStringLiteral("scrollCaptureCancel"));
        QVERIFY(cancel);
        cancel->click();

        QVERIFY(!overlay->m_scrollCaptureActive);
        QVERIFY(!overlay->m_scrollManualTimer);
        QVERIFY(!overlay->m_scrollPanel);
        QVERIFY(!overlay->m_scrollPreview);
        QVERIFY(!overlay->m_scrollOverlay);
        QVERIFY(overlay->isVisible());
        QVERIFY(overlay->m_toolbar->isVisible());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!panel);
        QVERIFY(!preview);
        QVERIFY(!mask);
        QCOMPARE(closed.count(), 0);
    }
    overlay->close();
    QCOMPARE(closed.count(), 1);
}

void MacScrollCaptureTest::focusLossKeepsScrollSessionAlive()
{
    QWidget target;
    auto overlay = prepareOverlay(target);
    QVERIFY(overlay);
    QSignalSpy closed(overlay.get(), &SnipOverlay::sessionClosed);
    QVERIFY(startWithoutCapturing(*overlay));
    QVERIFY(overlay->m_scrollCaptureActive);

    // Activate the real overlay window before moving focus to the target.
    // The explicit change event also exercises the scrolling-session guard.
    overlay->show();
    QTRY_VERIFY_WITH_TIMEOUT(overlay->windowHandle()->isActive(), 1000);
    target.activateWindow();
    QTRY_VERIFY_WITH_TIMEOUT(target.windowHandle()->isActive(), 1000);
    QVERIFY(!overlay->windowHandle()->isActive());
    QEvent deactivate(QEvent::ActivationChange);
    QApplication::sendEvent(overlay.get(), &deactivate);
    QTest::qWait(20);
    QCOMPARE(closed.count(), 0);
    QVERIFY(overlay->m_scrollCaptureActive);
    overlay->applyScrollCaptureWindowRegion();
    QVERIFY(!overlay->isVisible());
    QVERIFY(overlay->m_scrollPanel->isVisible());
    QVERIFY(overlay->m_scrollPreview->isVisible());
    QVERIFY(overlay->m_scrollOverlay->isVisible());
    overlay->cancelScrollCapture();
    overlay->close();
}

void MacScrollCaptureTest::captureFailureKeepsControlsAndFinishCleansSession()
{
    QWidget target;
    auto overlay = prepareOverlay(target);
    QVERIFY(overlay);
    QSignalSpy closed(overlay.get(), &SnipOverlay::sessionClosed);
    QSignalSpy captured(overlay.get(), &SnipOverlay::screenshotCaptured);
    QVERIFY(startWithoutCapturing(*overlay));
    QVERIFY(overlay->m_scrollCaptureActive);

    overlay->m_scrollCaptureBusy = true;
    overlay->m_scrollPanel->setBusy(true);
    overlay->completeScrollCaptureFrame({}, true);
    QVERIFY(!overlay->m_scrollCaptureBusy);
    QVERIFY(overlay->m_scrollPanel->isVisible());
    QVERIFY(overlay->m_scrollPreview->isVisible());
    QVERIFY(overlay->m_scrollOverlay->isVisible());
    auto *finish = overlay->m_scrollPanel->findChild<QToolButton *>(QStringLiteral("scrollCaptureFinish"));
    QVERIFY(finish);
    QVERIFY(finish->isEnabled());

    QImage frame(220, 180, QImage::Format_RGB32);
    frame.fill(QColor(30, 90, 160));
    overlay->completeScrollCaptureFrame(frame, true);
    finish->click();
    QCOMPARE(captured.count(), 1);
    QCOMPARE(qvariant_cast<QImage>(captured.first().first()), frame);
    QCOMPARE(QApplication::clipboard()->image().convertToFormat(QImage::Format_RGB32), frame);
    QCOMPARE(closed.count(), 1);
    QVERIFY(!overlay->m_scrollCaptureActive);
    QVERIFY(!overlay->m_scrollPanel);
    QVERIFY(!overlay->m_scrollPreview);
    QVERIFY(!overlay->m_scrollOverlay);
}

void MacScrollCaptureTest::nativeWindowFramesExcludeToolsAndStitchAfterScrolling()
{
    if (!platform::macScreenCaptureAccessGranted())
        QSKIP("Screen Recording is unavailable; native capture test does not request permission.");

    NativeScrollPage target;
    QScreen *screen = QGuiApplication::primaryScreen();
    target.setGeometry(QRect(screen->availableGeometry().topLeft() + QPoint(100, 80), QSize(420, 380)));
    NSView *targetView = (__bridge NSView *)(void *)target.winId();
    QVERIFY(targetView.window);
    // Define the source RGB space before Qt creates its raster backing store.
    targetView.window.colorSpace = NSColorSpace.sRGBColorSpace;
    QVERIFY(showTargetWindow(target));
    target.repaint();
    QTest::qWait(80);
    const quintptr targetWindow = static_cast<quintptr>(targetView.window.windowNumber);
    QVERIFY(targetWindow);
    QCOMPARE(platform::macWindowBounds(targetWindow), target.frameGeometry());
    const qreal scale = target.devicePixelRatioF();
    const auto colorNear = [](QColor actual, QColor expected) {
        return qAbs(actual.red() - expected.red()) <= 12
            && qAbs(actual.green() - expected.green()) <= 12
            && qAbs(actual.blue() - expected.blue()) <= 12;
    };
    QElapsedTimer captureElapsed;
    captureElapsed.start();
    const QImage beforeOverlay = platform::captureMacWindow(targetWindow);
    qInfo() << "Native SCK target window before overlay:" << captureElapsed.elapsed() << "ms";
    QVERIFY(!beforeOverlay.isNull());
    const QString artifacts = qEnvironmentVariable("SNIPASTE_TEST_ARTIFACTS");
    if (!artifacts.isEmpty()) {
        QVERIFY(QDir().mkpath(artifacts));
        QVERIFY(beforeOverlay.save(QDir(artifacts).filePath(QStringLiteral("native-scroll-before-overlay.png"))));
    }
    QCOMPARE(beforeOverlay.size(), QSize(qRound(target.width() * scale), qRound(target.height() * scale)));
    QVERIFY(colorNear(beforeOverlay.pixelColor(qRound(200 * scale), qRound(10 * scale)), QColor(220, 30, 40)));
    QVERIFY(colorNear(beforeOverlay.pixelColor(qRound(200 * scale), qRound(80 * scale)), QColor(40, 190, 70)));
    QVERIFY(colorNear(beforeOverlay.pixelColor(qRound(200 * scale), qRound(365 * scale)), QColor(25, 70, 220)));
    auto overlay = prepareOverlay(target);
    QVERIFY(overlay);
    overlay->m_selection = QRect(target.mapToGlobal(QPoint()) - overlay->m_screenGeometry.topLeft(),
                                  target.size());
    captureElapsed.restart();
    overlay->startScrollCapture();
    QVERIFY(overlay->m_scrollCaptureActive);
    QVERIFY(overlay->m_scrollWindow);
    QCOMPARE(overlay->m_scrollWindow, targetWindow);
    overlay->captureScrollFrame();
    QVERIFY(overlay->m_scrollCaptureBusy);
    QVERIFY(!overlay->m_scrollSettleElapsed.isValid());

    const QPoint panelPosition = overlay->m_scrollPanel->pos();
    const QPoint previewPosition = overlay->m_scrollPreview->pos();
    // Put our floating tools over source content. Capturing the target's own
    // pixels must still preserve the green marker beneath the preview.
    overlay->m_scrollPreview->move(target.pos() + QPoint(40, 40));
    overlay->m_scrollPanel->move(target.pos() + QPoint(0, 200));
    QTRY_COMPARE_WITH_TIMEOUT(overlay->m_scrollStitcher.frameCount(), 1, 15000);
    qInfo() << "Native SCK first settled frame:" << captureElapsed.elapsed() << "ms";
    const QImage first = overlay->m_scrollStitcher.image();
    if (!artifacts.isEmpty()) {
        QVERIFY(QDir().mkpath(artifacts));
        QVERIFY(first.save(QDir(artifacts).filePath(QStringLiteral("native-scroll-first.png"))));
    }
    QCOMPARE(first.size(), QSize(qRound(target.width() * scale), qRound(target.height() * scale)));
    QCOMPARE(first.devicePixelRatio(), 1.0);
    QVERIFY(colorNear(first.pixelColor(qRound(200 * scale), qRound(10 * scale)), QColor(220, 30, 40)));
    QVERIFY(colorNear(first.pixelColor(qRound(200 * scale), qRound(80 * scale)), QColor(40, 190, 70)));
    QVERIFY(colorNear(first.pixelColor(qRound(200 * scale), qRound(365 * scale)), QColor(25, 70, 220)));
    // The visible selection frame must not enter the captured target pixels.
    QVERIFY(colorNear(first.pixelColor(0, qRound(10 * scale)), QColor(220, 30, 40)));
    QVERIFY(overlay->m_scrollPanel->isVisible());
    QVERIFY(overlay->m_scrollPreview->isVisible());
    QVERIFY(overlay->m_scrollOverlay->isVisible());

    overlay->m_scrollPanel->move(panelPosition);
    overlay->m_scrollPreview->move(previewPosition);
    NSView *panelView = (__bridge NSView *)(void *)overlay->m_scrollPanel->winId();
    NSView *previewView = (__bridge NSView *)(void *)overlay->m_scrollPreview->winId();
    QVERIFY(panelView.window);
    QVERIFY(previewView.window);
    // Cocoa commits native window moves asynchronously. Keep the old preview
    // position from intercepting the wheel intended for the target's center.
    QTRY_COMPARE_WITH_TIMEOUT(platform::macWindowBounds(panelView.window.windowNumber).topLeft(),
                              panelPosition, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(platform::macWindowBounds(previewView.window.windowNumber).topLeft(),
                              previewPosition, 1000);
    if (!artifacts.isEmpty()) {
        // Composite only our synthetic page and rendered controls, so this
        // visual QA artifact never contains the user's desktop or windows.
        QImage ui(overlay->size(), QImage::Format_ARGB32_Premultiplied);
        ui.fill(QColor(225, 228, 232));
        QPainter painter(&ui);
        const QPoint origin = overlay->geometry().topLeft();
        painter.drawPixmap(target.pos() - origin, target.grab());
        painter.drawPixmap(QPoint(), overlay->m_scrollOverlay->grab());
        painter.drawPixmap(overlay->m_scrollPreview->pos() - origin, overlay->m_scrollPreview->grab());
        painter.drawPixmap(overlay->m_scrollPanel->pos() - origin, overlay->m_scrollPanel->grab());
        painter.end();
        const QRect area = target.frameGeometry().united(overlay->m_scrollPreview->geometry())
            .united(overlay->m_scrollPanel->geometry()).adjusted(-24, -60, 24, 24)
            .translated(-origin).intersected(overlay->rect());
        QVERIFY(ui.copy(area).save(QDir(artifacts).filePath(QStringLiteral("macos-scroll-ui.png"))));
    }
    captureElapsed.restart();
    if (AXIsProcessTrusted()) {
        const QPoint toolbarPoint = overlay->m_scrollPanel->mapToGlobal(overlay->m_scrollPanel->rect().center());
        QCursor::setPos(toolbarPoint);
        QTRY_COMPARE_WITH_TIMEOUT(QCursor::pos(), toolbarPoint, 1000);
        QVERIFY(platform::postMacScrollStep(overlay->m_scrollWindow,
                                            target.mapToGlobal(target.rect().center())));
        QTRY_VERIFY_WITH_TIMEOUT(target.scrollOffset() > 0, 2000);
        qInfo() << "Native scroll injection reached the test window.";
    } else {
        qInfo() << "Accessibility unavailable; native input injection skipped; scrolling the owned widget.";
        target.scrollDown();
    }
    // AppKit global monitors intentionally observe other applications. This
    // target belongs to the test process, so schedule its settled capture here.
    overlay->scheduleManualScrollCapture();
    QTRY_COMPARE_WITH_TIMEOUT(overlay->m_scrollStitcher.frameCount(), 2, 12000);
    qInfo() << "Native SCK second settled frame:" << captureElapsed.elapsed() << "ms";
    QCOMPARE(overlay->m_scrollStitcher.size(),
             QSize(first.width(), first.height() + qRound(target.scrollOffset() * scale)));
    QCOMPARE(overlay->m_scrollStitcher.image().copy(0, 0, first.width(), first.height()), first);
    overlay->cancelScrollCapture();
    overlay->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(!overlay->m_scrollPanel);
    QVERIFY(!overlay->m_scrollPreview);
    QVERIFY(!overlay->m_scrollOverlay);
}

} // namespace snipaste::ui

QTEST_MAIN(snipaste::ui::MacScrollCaptureTest)
#include "macos_scroll_capture_test.moc"
