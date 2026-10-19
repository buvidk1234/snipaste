#include "platform/macos/system.h"
#include "ui/snip/snip_overlay.h"

#include <QApplication>
#include <QFile>
#include <QProcess>
#include <QScreen>
#include <QSignalSpy>
#include <QSocketNotifier>
#include <QTest>
#include <QTimer>

#import <AppKit/AppKit.h>

#include <cstdio>
#include <memory>
#include <unistd.h>

namespace {

int runActivationHelper(QApplication &application, pid_t parentPid)
{
    QWidget window;
    window.setWindowTitle(QStringLiteral("Capture activation test helper"));
    window.setGeometry(80, 80, 320, 160);
    window.show();

    QFile input;
    if (!input.open(STDIN_FILENO, QIODevice::ReadOnly | QIODevice::Unbuffered))
        return 1;
    QSocketNotifier commands(STDIN_FILENO, QSocketNotifier::Read);
    QObject::connect(&commands, &QSocketNotifier::activated, &application, [&] {
        const QByteArray command = input.readLine().trimmed();
        if (command == "activate") {
            window.raise();
            window.activateWindow();
            [NSApp activate];
        } else if (command == "yield") {
            NSRunningApplication *parent = [NSRunningApplication runningApplicationWithProcessIdentifier:parentPid];
            [NSApp yieldActivationToApplication:parent];
            std::puts("yielded");
            std::fflush(stdout);
        } else if (command == "quit" || input.atEnd()) {
            application.quit();
        }
    });
    QTimer::singleShot(0, &application, [] {
        std::puts("ready");
        std::fflush(stdout);
    });
    return application.exec();
}

} // namespace

class MacCaptureActivationTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void backgroundCaptureActivatesOnceAndClosesOnLaterFocusLoss();
    void cleanupTestCase();

private:
    QProcess m_helper;
};

void MacCaptureActivationTest::initTestCase()
{
    if (QGuiApplication::platformName() != QStringLiteral("cocoa"))
        QSKIP("Native activation requires the Cocoa platform plugin.");
    if (!snipaste::platform::macScreenCaptureAccessGranted())
        QSKIP("Screen Recording is unavailable; this test does not request permission.");
    QApplication::setQuitOnLastWindowClosed(false);
    QVERIFY(QGuiApplication::primaryScreen());
}

void MacCaptureActivationTest::backgroundCaptureActivatesOnceAndClosesOnLaterFocusLoss()
{
    QWidget ownedWindow;
    ownedWindow.setWindowTitle(QStringLiteral("Capture activation test owner"));
    ownedWindow.resize(240, 160);
    ownedWindow.show();
    ownedWindow.raise();
    ownedWindow.activateWindow();
    [NSApp activate];
    QTRY_VERIFY_WITH_TIMEOUT(NSApp.active && ownedWindow.isActiveWindow(), 3000);

    m_helper.start(QCoreApplication::applicationFilePath(),
                   {QStringLiteral("--activation-helper"), QString::number(getpid())});
    QVERIFY(m_helper.waitForStarted(3000));
    QTRY_VERIFY_WITH_TIMEOUT(m_helper.canReadLine(), 3000);
    QCOMPARE(m_helper.readLine().trimmed(), QByteArray("ready"));
    const pid_t helperPid = static_cast<pid_t>(m_helper.processId());
    const auto helper = [helperPid] {
        return [NSRunningApplication runningApplicationWithProcessIdentifier:helperPid];
    };
    QVERIFY(helper());

    std::unique_ptr<snipaste::ui::SnipOverlay> overlay;
    for (int session = 0; session < 2; ++session) {
        // The helper is an owned second application, rather than another
        // window in this process. Verify real app deactivation before capture.
        if (NSApp.active)
            [NSApp yieldActivationToApplication:helper()];
        m_helper.write("activate\n");
        QTRY_VERIFY_WITH_TIMEOUT(helper().active && !NSApp.active, 3000);
        if (!overlay)
            overlay.reset(snipaste::ui::SnipOverlay::createPrepared(QGuiApplication::primaryScreen()));
        overlay->beginCapture(QGuiApplication::primaryScreen());
        overlay->setExitOnFocusLoss(true);
        QSignalSpy closed(overlay.get(), &snipaste::ui::SnipOverlay::sessionClosed);

        // Cooperatively hand activation back without activating this process
        // ourselves: the overlay's single show() must perform that request.
        m_helper.write("yield\n");
        QTRY_VERIFY_WITH_TIMEOUT(m_helper.canReadLine(), 3000);
        QCOMPARE(m_helper.readLine().trimmed(), QByteArray("yielded"));
        QVERIFY(!NSApp.active);
        QEvent initialFocusChange(QEvent::ActivationChange);
        QCoreApplication::sendEvent(overlay.get(), &initialFocusChange);
        overlay->show();
        QTRY_VERIFY_WITH_TIMEOUT(NSApp.active && overlay->isVisible() && overlay->isActiveWindow(), 3000);
        NSView *view = (__bridge NSView *)(void *)overlay->winId();
        QVERIFY(view.window.visible);
        QVERIFY(view.window.keyWindow);
        QCOMPARE(closed.count(), 0);

        // Once native activation succeeded, a real switch to the helper must
        // close this session. The same reusable overlay then opens once again.
        [NSApp yieldActivationToApplication:helper()];
        m_helper.write("activate\n");
        QTRY_VERIFY_WITH_TIMEOUT(helper().active && !NSApp.active, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, 3000);
        QVERIFY(!overlay->isVisible());
    }
}

void MacCaptureActivationTest::cleanupTestCase()
{
    if (m_helper.state() == QProcess::NotRunning)
        return;
    m_helper.write("quit\n");
    if (!m_helper.waitForFinished(1500)) {
        m_helper.terminate();
        m_helper.waitForFinished(1500);
    }
}

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    if (argc == 3 && QByteArray(argv[1]) == "--activation-helper")
        return runActivationHelper(application, QString::fromLocal8Bit(argv[2]).toInt());
    MacCaptureActivationTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "macos_capture_activation_test.moc"
