#include "app/application_controller.h"
#include "app/command_line.h"
#include "app/logging.h"
#include "app/runtime_settings.h"
#include "core/command/argument_parser.h"
#include "core/config/config_store.h"
#include "platform/windows/single_instance.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QFileInfo>
#include <QMessageBox>
#include <QStandardPaths>
#include <QThread>
#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    // Capture pixels at the monitor's native scale.  Without opting into
    // per-monitor DPI awareness before QApplication is created, Windows can
    // DPI-virtualize screen grabs to logical pixels; the overlay then paints
    // those already reduced pixels back into the selection and the exported
    // screenshot looks softer than the desktop.
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        SetProcessDPIAware();
#endif
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("snipaste"));
    QCoreApplication::setApplicationName(QStringLiteral("snipaste"));
    QCoreApplication::setApplicationVersion(QStringLiteral(SNIPASTE_VERSION));
    QApplication::setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    snipaste::app::configureCommandLine(parser);
    parser.process(snipaste::core::normalizeMultiValueOptions(QCoreApplication::arguments()));

    snipaste::app::initializeLogging();

    snipaste::platform::SingleInstance instance;
    bool instanceAcquired = instance.acquire(QStringLiteral("snipaste-single-instance"));
    if (!instanceAcquired && QCoreApplication::arguments().contains(QStringLiteral("--snipaste-restart")))
    {
        for (int attempt = 0; attempt < 20 && !instanceAcquired; ++attempt)
        {
            QThread::msleep(100);
            instanceAcquired = instance.acquire(QStringLiteral("snipaste-single-instance"));
        }
    }
    if (!instanceAcquired)
    {
        // A second launch is a command invocation, not a reason to display a
        // modal error. Forward its arguments to the resident tray instance.
        const QStringList forwardedArguments = QCoreApplication::arguments().mid(1);
        const int blockIndex = forwardedArguments.indexOf(QStringLiteral("--block"));
        const bool waitForCompletion =
            blockIndex >= 0 ||
            std::any_of(forwardedArguments.cbegin(), forwardedArguments.cend(),
                        [](const QString &value) { return value.startsWith(QStringLiteral("--block=")); });
        int waitMs = 120000;
        if (waitForCompletion)
        {
            QString blockValue;
            if (blockIndex >= 0 && blockIndex + 1 < forwardedArguments.size() &&
                !forwardedArguments.at(blockIndex + 1).startsWith(QChar('-')))
                blockValue = forwardedArguments.at(blockIndex + 1);
            else
            {
                for (const QString &value : forwardedArguments)
                    if (value.startsWith(QStringLiteral("--block=")))
                        blockValue = value.mid(QStringLiteral("--block=").size());
            }
            bool ok = false;
            const double seconds = blockValue.toDouble(&ok);
            if (ok && seconds > 0.0)
                waitMs = qBound(1000, qRound(seconds * 1000.0) + 5000, 24 * 60 * 60 * 1000);
        }
        const bool sent =
            waitForCompletion
                ? instance.sendMessageAndWait(QStringLiteral("snipaste-single-instance"), forwardedArguments,
                                              waitMs)
                : instance.sendMessage(QStringLiteral("snipaste-single-instance"), forwardedArguments);
        if (sent)
        {
            qInfo()
                << "another snipaste instance is already running in tray; forwarded arguments and exiting.";
            return 0;
        }
        qWarning() << "another snipaste instance is already running";
        QMessageBox::information(nullptr, QStringLiteral("snipaste 已在运行"),
                                 QStringLiteral("snipaste 已经在系统托盘中运行。\n"
                                                "请右键托盘图标打开菜单。"));
        return 0;
    }

    const QString configPath = parser.isSet(QStringLiteral("config"))
                                   ? QFileInfo(parser.value(QStringLiteral("config"))).absoluteFilePath()
                                   : QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) +
                                         QStringLiteral("/config.ini");
    snipaste::core::ConfigStore config(configPath);
    config.load();

    snipaste::app::applyApplicationSettings(config);
    snipaste::app::ApplicationController controller(app, config, instance);
    controller.start(QCoreApplication::arguments().mid(1));
    return app.exec();
}
