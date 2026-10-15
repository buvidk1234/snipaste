#include "app/application_controller.h"
#include "app/command_line.h"
#include "app/runtime_settings.h"
#include "core/command/argument_parser.h"
#include "core/config/config_store.h"
#include "core/history/screenshot_history.h"
#include "platform/windows/single_instance.h"
#include "ui/paste/paste_window.h"
#include "ui/snip/snip_overlay.h"

#include <QApplication>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

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
