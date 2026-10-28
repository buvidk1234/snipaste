#include "app/application_controller.h"
#include "app/command_line.h"
#include "app/runtime_settings.h"
#include "core/command/argument_parser.h"
#include "core/config/config_store.h"
#include "core/history/screenshot_history.h"
#include "platform/windows/single_instance.h"
#include "platform/windows/capture_target.h"
#include "ui/paste/paste_window.h"
#include "ui/preferences/preferences_dialog.h"
#include "ui/snip/snip_overlay.h"

#include <QApplication>
#include <QAction>
#include <QColorDialog>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QElapsedTimer>
#include <QInputDialog>
#include <QMessageBox>
#include <QMenu>
#include <QProcess>
#include <QScreen>
#include <QStandardPaths>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QWindow>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace snipaste;

namespace
{
void configureTest(core::ConfigStore &config, const QString &directory)
{
    config.load();
    config.setValue(QStringLiteral("general/history_dir"), directory + QStringLiteral("/history"));
    config.setValue(QStringLiteral("capture/autoSave"), false);
    config.setValue(QStringLiteral("paste/flashNew"), false);
    config.setValue(QStringLiteral("paste/activateNew"), false);
    config.setValue(QStringLiteral("paste/virtualDesktop"), false);
    config.setValue(QStringLiteral("update/checkAtStartup"), false);
    for (const char *name :
         {"snip", "copy", "paste", "custom", "toggleImages", "switchGroup", "clickThrough"})
        config.setValue(QStringLiteral("hotkeys/") + QString::fromLatin1(name), QString());
}

QList<ui::PasteWindow *> pins()
{
    QList<ui::PasteWindow *> result;
    for (auto *widget : QApplication::topLevelWidgets())
        if (auto *pin = qobject_cast<ui::PasteWindow *>(widget))
            result.append(pin);
    return result;
}
} // namespace

class ApplicationControllerTest final : public QObject
{
    Q_OBJECT
  private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("snipaste-tests"));
        QCoreApplication::setApplicationName(QStringLiteral("application-controller-tests"));
    }
    void startupAndForwardedCommandsShareBehavior()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        core::ConfigStore config(directory.filePath(QStringLiteral("config.ini")));
        configureTest(config, directory.path());
        platform::SingleInstance instance;
        auto &application = *static_cast<QApplication *>(qApp);
        {
            app::ApplicationController controller(application, config, instance);
            const QStringList arguments{QStringLiteral("paste"), QStringLiteral("--plain"),
                                        QStringLiteral("shared routing"), QStringLiteral("--pos"),
                                        QStringLiteral("37,59")};
            QVERIFY(controller.dispatchArguments(arguments, false));
            QCOMPARE(pins().size(), 1);
            const QImage direct = pins().first()->sourceImage();
            QCOMPARE(pins().first()->pos(), QPoint(37, 59));
            instance.messageReceivedWithSocket(arguments, nullptr);
            QCOMPARE(pins().size(), 2);
            for (const auto *pin : pins())
            {
                QCOMPARE(pin->sourceImage(), direct);
                QCOMPARE(pin->pos(), QPoint(37, 59));
            }
            QVERIFY(controller.dispatchArguments({QStringLiteral("--paste"), QStringLiteral("--plain"),
                                                  QStringLiteral("shared routing"), QStringLiteral("--pos"),
                                                  QStringLiteral("37,59")},
                                                 false));
            QCOMPARE(pins().size(), 3);
            for (const auto *pin : pins())
                QCOMPARE(pin->sourceImage(), direct);
            QVERIFY(controller.dispatchArguments({QStringLiteral("--hide-images")}, false));
            for (const auto *pin : pins())
                QVERIFY(!pin->isVisible());
            instance.messageReceivedWithSocket({QStringLiteral("show-images")}, nullptr);
            for (const auto *pin : pins())
                QVERIFY(pin->isVisible());
            QVERIFY(!controller.dispatchArguments({QStringLiteral("--unknown-option")}, false));
            QCOMPARE(pins().size(), 3);
        }
        QVERIFY(pins().isEmpty());
    }
    void historyClearBeforeLazyLoadIsPersistent()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        core::ConfigStore config(directory.filePath(QStringLiteral("config.ini")));
        configureTest(config, directory.path());
        const auto path = directory.filePath(QStringLiteral("history"));
        QImage image(40, 30, QImage::Format_RGB32);
        image.fill(Qt::green);
        core::ScreenshotHistory old;
        old.add(image);
        QVERIFY(old.saveToDirectory(path));
        {
            app::ScreenshotHistoryService history(config);
            history.clear();
            QVERIFY(history.history().images().isEmpty());
        }
        core::ScreenshotHistory reloaded;
        reloaded.loadFromDirectory(path);
        QVERIFY(reloaded.images().isEmpty());
    }
    void dialogDefaultsDoNotBlockTheApplication()
    {
        QTemporaryDir directory;
        core::ConfigStore config(directory.filePath(QStringLiteral("config.ini")));
        configureTest(config, directory.path());
        platform::SingleInstance instance;
        app::ApplicationController controller(*static_cast<QApplication *>(qApp), config, instance);

        // Check before show(): native file/color dialogs consume these settings
        // before the QWidget receives polish or show events.
        QDialog dialog;
        QMessageBox message;
        QInputDialog input;
        QFileDialog file;
        QColorDialog color;
        for (const auto *window : QList<QDialog *>{&dialog, &message, &input, &file, &color})
        {
#ifdef Q_OS_MACOS
            QCOMPARE(window->windowModality(), Qt::ApplicationModal);
#else
            QCOMPARE(window->windowModality(), Qt::WindowModal);
#endif
            QVERIFY(window->windowFlags().testFlag(Qt::WindowStaysOnTopHint));
        }
        QWidget owner;
        QDialog ownedDialog(&owner);
        QMessageBox ownedMessage(&owner);
        QInputDialog ownedInput(&owner);
        QFileDialog ownedFile(&owner);
        QColorDialog ownedColor(&owner);
        for (const auto *window : QList<QDialog *>{&ownedDialog, &ownedMessage, &ownedInput, &ownedFile, &ownedColor})
            QCOMPARE(window->windowModality(), Qt::WindowModal);
        ui::PreferencesDialog preferences(config);
        QCOMPARE(preferences.windowModality(), Qt::NonModal);
    }
    void standaloneNativeWarningCanRunModally()
    {
#ifndef Q_OS_MACOS
        QSKIP("Native warning regression requires Cocoa.");
#else
        if (QGuiApplication::platformName() != QStringLiteral("cocoa"))
            QSKIP("Native warning regression requires Cocoa.");
        QTemporaryDir directory;
        core::ConfigStore config(directory.filePath(QStringLiteral("config.ini")));
        configureTest(config, directory.path());
        platform::SingleInstance instance;
        app::ApplicationController controller(*static_cast<QApplication *>(qApp), config, instance);
        QMessageBox message(QMessageBox::Warning, QStringLiteral("Native modal regression"),
                            QStringLiteral("This test closes its own warning automatically."), QMessageBox::Ok);
        QCOMPARE(message.windowModality(), Qt::ApplicationModal);
        QVERIFY(!message.testOption(QMessageBox::Option::DontUseNativeDialog));
        bool closed = false;
        QTimer::singleShot(50, &message, [&] {
            closed = true;
            message.done(QMessageBox::Ok);
        });
        QCOMPARE(message.exec(), int(QMessageBox::Ok));
        QVERIFY(closed);
#endif
    }
    void dialogsKeepUnrelatedPinsInteractive_data()
    {
        QTest::addColumn<bool>("parented");
        QTest::newRow("standalone-dialog") << false;
        QTest::newRow("pin-dialog") << true;
    }
    void dialogsKeepUnrelatedPinsInteractive()
    {
        QFETCH(bool, parented);
        QTemporaryDir directory;
        core::ConfigStore config(directory.filePath(QStringLiteral("config.ini")));
        configureTest(config, directory.path());
        platform::SingleInstance instance;
        app::ApplicationController controller(*static_cast<QApplication *>(qApp), config, instance);
        QImage image(240, 160, QImage::Format_RGB32);
        image.fill(Qt::yellow);
        ui::PasteWindow owner(image);
        owner.showAtPosition(QPoint(100, 100));
        ui::PasteWindow pin(image);
        pin.showAtPosition(QPoint(400, 100));
        QVERIFY(QTest::qWaitForWindowExposed(&owner));
        QVERIFY(QTest::qWaitForWindowExposed(&pin));
        const QPoint ownerPosition = owner.pos();
        const QPoint pinPosition = pin.pos();
        const QPoint hit(40, 40);
        const QPoint delta(30, 30);
        auto drag = [&](ui::PasteWindow &window) {
            // QWidget overloads bypass Qt's modal input filtering.
            const QPoint end = window.mapToGlobal(hit) + delta;
            QTest::mousePress(window.windowHandle(), Qt::LeftButton, Qt::NoModifier, hit);
            QTest::mouseMove(window.windowHandle(), hit + delta);
            QTest::mouseRelease(window.windowHandle(), Qt::LeftButton, Qt::NoModifier,
                                window.mapFromGlobal(end));
        };
        QDialog dialog(parented ? &owner : nullptr);
        dialog.resize(160, 100);
        dialog.move(pinPosition + QPoint(20, 20));
        bool exercised = false;
        bool abovePin = true;
        QTimer::singleShot(0, &dialog, [&] {
            exercised = true;
            if (QGuiApplication::platformName() == QStringLiteral("windows"))
                abovePin = QApplication::widgetAt(dialog.mapToGlobal(dialog.rect().center())) == &dialog;
            drag(pin);
            drag(owner);
            dialog.accept();
        });
        QCOMPARE(dialog.exec(), int(QDialog::Accepted));
        QVERIFY(exercised);
        QVERIFY(abovePin);
#ifdef Q_OS_MACOS
        QCOMPARE(pin.pos(), parented ? pinPosition + delta : pinPosition);
        QCOMPARE(owner.pos(), ownerPosition);
#else
        QCOMPARE(pin.pos(), pinPosition + delta);
        QCOMPARE(owner.pos(), parented ? ownerPosition : ownerPosition + delta);
#endif
        const QPoint restoredPosition = owner.pos();
        drag(owner);
        QCOMPARE(owner.pos(), restoredPosition + delta);
    }
    void preferencesOpenWhenLaunchedHidden()
    {
#ifndef Q_OS_WIN
        QSKIP("Windows startup window visibility regression.");
#else
        if (QGuiApplication::platformName() != QStringLiteral("windows"))
            QSKIP("Requires the Windows desktop platform.");
        QProcess child;
        child.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) {
            arguments->startupInfo->dwFlags |= STARTF_USESHOWWINDOW;
            arguments->startupInfo->wShowWindow = SW_HIDE;
        });
        child.start(QCoreApplication::applicationFilePath(),
                    {QStringLiteral("preferencesOpenFromFirstTrayMenu"), QStringLiteral("-o"),
                     QStringLiteral("-,txt")});
        QVERIFY(child.waitForStarted());
        QVERIFY(child.waitForFinished(15000));
        const QByteArray output = child.readAllStandardOutput() + child.readAllStandardError();
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QVERIFY2(child.exitCode() == 0, output.constData());
#endif
    }
    void preferencesOpenFromFirstTrayMenu()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        core::ConfigStore config(directory.filePath(QStringLiteral("config.ini")));
        configureTest(config, directory.path());
        platform::SingleInstance instance;
        app::ApplicationController controller(*static_cast<QApplication *>(qApp), config, instance);
        QVERIFY(controller.dispatchArguments({QStringLiteral("show-tray-menu")}, false));
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        QVERIFY(menu);
        QAction *openPreferences = nullptr;
        for (auto *action : menu->actions())
            if (action->text() == QStringLiteral("首选项…"))
                openPreferences = action;
        QVERIFY(openPreferences);
        QVERIFY(QTest::qWaitForWindowExposed(menu));
        QTest::mouseClick(menu->windowHandle(), Qt::LeftButton, Qt::NoModifier,
                          menu->actionGeometry(openPreferences).center());
        QVERIFY(!menu->isVisible());
        QPointer<ui::PreferencesDialog> preferences;
        for (auto *widget : QApplication::topLevelWidgets())
            if (auto *dialog = dynamic_cast<ui::PreferencesDialog *>(widget))
                preferences = dialog;
        QVERIFY(preferences);
        QVERIFY(QTest::qWaitForWindowExposed(preferences));
        if (QGuiApplication::platformName() == QStringLiteral("windows"))
            QVERIFY(QTest::qWaitForWindowActive(preferences));
        QTest::qWait(100);
        QVERIFY(preferences->isVisible());
        QVERIFY(!QApplication::activeModalWidget());
    }
    void preferencesAllowCaptureWithoutBlockingInput()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        core::ConfigStore config(directory.filePath(QStringLiteral("config.ini")));
        configureTest(config, directory.path());
        config.setValue(QStringLiteral("capture/autoDetectWindow"), false);
        config.setValue(QStringLiteral("capture/autoDetectElements"), false);
        config.setValue(QStringLiteral("capture/soundEnabled"), false);
        platform::SingleInstance instance;
        QPointer<ui::PreferencesDialog> preferences;
        {
            app::ApplicationController controller(*static_cast<QApplication *>(qApp), config, instance);
            // Unwind a regressed exec() instead of hanging the test in its nested loop.
            bool enteredNestedLoop = false;
            QTimer nestedLoopGuard;
            QObject::connect(&nestedLoopGuard, &QTimer::timeout, &nestedLoopGuard, [&] {
                enteredNestedLoop = true;
                for (auto *widget : QApplication::topLevelWidgets())
                    if (auto *dialog = dynamic_cast<ui::PreferencesDialog *>(widget))
                        dialog->close();
            });
            nestedLoopGuard.start(0);
            const bool opened = controller.dispatchArguments({QStringLiteral("open-preferences")}, false);
            nestedLoopGuard.stop();
            QVERIFY(opened);
            QVERIFY2(!enteredNestedLoop, "Preferences must not run a nested modal event loop");
            for (auto *widget : QApplication::topLevelWidgets())
                if (auto *dialog = dynamic_cast<ui::PreferencesDialog *>(widget))
                    preferences = dialog;
            QVERIFY(preferences);
            QVERIFY(preferences->isVisible());
            QCOMPARE(preferences->windowModality(), Qt::NonModal);
            QVERIFY(!QApplication::activeModalWidget());
            auto *border = preferences->findChild<QSpinBox *>(QStringLiteral("captureBorderWidth"));
            QVERIFY(border);
            border->setValue(7);
            QVERIFY(controller.dispatchArguments({QStringLiteral("open-preferences")}, false));
            int visiblePreferences = 0;
            for (auto *widget : QApplication::topLevelWidgets())
                if (dynamic_cast<ui::PreferencesDialog *>(widget) && widget->isVisible())
                    ++visiblePreferences;
            QCOMPARE(visiblePreferences, 1);
            QCOMPARE(border->value(), 7);
            for (int cycle = 0; cycle < 2; ++cycle)
            {
                QVERIFY(controller.dispatchArguments({QStringLiteral("snip")}, false));
                ui::SnipOverlay *overlay = nullptr;
                for (auto *widget : QApplication::topLevelWidgets())
                    if (auto *candidate = qobject_cast<ui::SnipOverlay *>(widget); candidate && candidate->isVisible())
                        overlay = candidate;
                QVERIFY(overlay);
                QVERIFY(!QApplication::activeModalWidget());
                QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, QPoint(40, 40));
                QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, QPoint(240, 180));
                QVERIFY(!overlay->captureGeometry().region.isEmpty());
                QTest::keyClick(overlay, Qt::Key_Escape);
                QVERIFY(!overlay->isVisible());
                QVERIFY(preferences->isVisible());
                QCOMPARE(border->value(), 7);
            }
            preferences->close();
            QVERIFY(!preferences->isVisible());
            QCOMPARE(config.value(QStringLiteral("capture/borderWidth")).toInt(), 7);
            QTRY_VERIFY(!preferences);
            QVERIFY(controller.dispatchArguments({QStringLiteral("open-preferences")}, false));
            for (auto *widget : QApplication::topLevelWidgets())
                if (auto *dialog = dynamic_cast<ui::PreferencesDialog *>(widget); dialog && dialog->isVisible())
                    preferences = dialog;
            QVERIFY(preferences);
            QVERIFY(preferences->isVisible());
            QCOMPARE(preferences->findChild<QSpinBox *>(QStringLiteral("captureBorderWidth"))->value(), 7);
            preferences->close();
            QVERIFY(controller.dispatchArguments({QStringLiteral("open-preferences")}, false));
            QVERIFY(!preferences);
            for (auto *widget : QApplication::topLevelWidgets())
                if (auto *dialog = dynamic_cast<ui::PreferencesDialog *>(widget); dialog && dialog->isVisible())
                    preferences = dialog;
            QVERIFY(preferences);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
            QVERIFY(preferences);
            QVERIFY(preferences->isVisible());
        }
        QVERIFY(!preferences);
    }
    void ownPreferencesElementDetectionRemainsResponsive()
    {
#ifndef Q_OS_WIN
        QSKIP("Windows native accessibility regression.");
#else
        if (QGuiApplication::platformName() != QStringLiteral("windows"))
            QSKIP("Requires the Windows desktop platform.");
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        core::ConfigStore config(directory.filePath(QStringLiteral("config.ini")));
        configureTest(config, directory.path());
        ui::PreferencesDialog preferences(config);
        preferences.setModal(false);
        preferences.show();
        QVERIFY(QTest::qWaitForWindowExposed(&preferences));
        const QRect screen = preferences.screen()->geometry();
        QWidget anchor;
        anchor.setGeometry(screen);
        const QPoint point = preferences.mapToGlobal(QPoint(80, 80)) - screen.topLeft();
        quintptr nativeWindow = 0;
        QElapsedTimer timer;
        timer.start();
        const auto targets = platform::captureTargetHierarchyAt(
            point, screen.size(), anchor.winId(), true, {}, {}, &nativeWindow);
        QVERIFY2(timer.elapsed() < 1000, "Detecting our own preferences must not wait for accessibility RPC");
        QCOMPARE(nativeWindow, quintptr(preferences.winId()));
        QVERIFY(!targets.isEmpty());
        QVERIFY(targets.first().contains(point));
#endif
    }
    void fullScreenOutputRunsOnceWithoutExtraPin()
    {
#ifdef Q_OS_MACOS
        QSKIP("A native screen capture requires macOS permission and an interactive session.");
#endif
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        core::ConfigStore config(directory.filePath(QStringLiteral("config.ini")));
        configureTest(config, directory.path());
        config.setValue(QStringLiteral("output/saveDirectory"), directory.path());
        platform::SingleInstance instance;
        {
            app::ApplicationController controller(*static_cast<QApplication *>(qApp), config, instance);
            QVERIFY(controller.dispatchArguments({QStringLiteral("snip"), QStringLiteral("--full-screen"),
                                                  QStringLiteral("--output"),
                                                  QStringLiteral("capture.png;no-auto-save;success")},
                                                 false));
            QVERIFY(pins().isEmpty());
            QVERIFY(QFile::exists(directory.filePath(QStringLiteral("capture.png"))));
            QVERIFY(!QFile::exists(directory.filePath(QStringLiteral("success.png"))));
        }
        core::ScreenshotHistory history;
        history.loadFromDirectory(directory.filePath(QStringLiteral("history")));
        QCOMPARE(history.images().size(), 1);
    }
    void delayedCaptureAndOwnerDestruction()
    {
        QTemporaryDir directory;
        core::ConfigStore config(directory.filePath(QStringLiteral("config.ini")));
        configureTest(config, directory.path());
        ui::PasteManager pastes;
        app::ScreenshotHistoryService history(config);
        app::ScreenshotOutput output(config, pastes);
        {
            app::CaptureController capture(config, history, output, pastes);
            capture.schedule({}, 2000);
            QVERIFY(capture.active());
            QVERIFY(!capture.overlay());
        }
        QCoreApplication::processEvents();
        for (auto *widget : QApplication::topLevelWidgets())
            QVERIFY(!qobject_cast<ui::SnipOverlay *>(widget));
    }
};

QTEST_MAIN(ApplicationControllerTest)
#include "application_controller_test.moc"
