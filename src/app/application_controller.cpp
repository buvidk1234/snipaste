#include "app/application_controller.h"
#include "app/command_line.h"
#include "app/runtime_settings.h"
#include "core/config/config_store.h"
#include "platform/windows/single_instance.h"
#include "ui/preferences/preferences_dialog.h"
#include "ui/whiteboard/whiteboard_window.h"
#include <QApplication>
#include <QCursor>
#include <QDateTime>
#include <QDebug>
#include <QEvent>
#include <QLocalSocket>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>
#include <QWidget>
#include <QWindow>
#include <algorithm>

namespace snipaste::app
{
using snipaste::ui::WhiteboardWindow;

ApplicationController::ApplicationController(QApplication &app, core::ConfigStore &config,
                                             platform::SingleInstance &instance)
    : QObject(&app), m_app(app), m_config(config), m_instance(instance), m_updates(config), m_history(config),
      m_output(config, m_pastes), m_capture(config, m_history, m_output, m_pastes),
      m_input(config, m_commands, m_capture), m_tray(m_commands, config)
{
    m_app.installEventFilter(this);
    registerCaptureCommands();
    registerPasteCommands();
    registerUtilityCommands();
    applySettings();
    m_pasteStatePath =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/pastes");
    m_pastes.loadState(m_pasteStatePath);
    QObject::connect(&m_app, &QCoreApplication::aboutToQuit, this,
                     [this] { m_pastes.saveState(m_pasteStatePath); });
    QObject::connect(
        &m_instance, &platform::SingleInstance::messageReceivedWithSocket, this,
        [this](const QStringList &arguments, QLocalSocket *socket) {
            QStringList args = arguments;
            const bool awaitCompletion = args.removeAll(QStringLiteral("--snipaste-await")) > 0;
            dispatchArguments(args, false);
            if (!awaitCompletion)
                return;
            const bool block = std::any_of(args.cbegin(), args.cend(), [](const QString &arg) {
                return arg == QStringLiteral("--block") || arg.startsWith(QStringLiteral("--block="));
            });
            if (!block || !m_capture.active())
            {
                m_instance.finishMessage(socket);
                return;
            }
            auto *poll = new QTimer(this);
            poll->setInterval(25);
            const auto deadline = commandBlockDeadline(args);
            QObject::connect(poll, &QTimer::timeout, poll,
                             [this, poll, socket = QPointer<QLocalSocket>(socket), deadline] {
                                 if (!socket)
                                 {
                                     poll->stop();
                                     poll->deleteLater();
                                     return;
                                 }
                                 if (!m_capture.active() ||
                                     (deadline > 0 && QDateTime::currentMSecsSinceEpoch() >= deadline))
                                 {
                                     m_instance.finishMessage(socket);
                                     poll->stop();
                                     poll->deleteLater();
                                 }
                             });
            poll->start();
        });
}
ApplicationController::~ApplicationController() = default;

bool ApplicationController::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Create || event->type() == QEvent::Show)
    {
        auto *widget = qobject_cast<QWidget *>(watched);
        if (widget && widget->windowType() == Qt::Dialog)
        {
            if (event->type() == QEvent::Create)
            {
                // Native dialogs read these settings before polish/show events.
                // Only the owning window should be blocked; independent pins
                // must remain movable even if they overlap the dialog.
                widget->setWindowModality(Qt::WindowModal);
                widget->setWindowFlag(Qt::WindowStaysOnTopHint);
            }
            else if (auto *parent = widget->parentWidget())
            {
                // Keep the native owner association when topmost flags change.
                auto *window = widget->windowHandle();
                auto *owner = parent->window()->windowHandle();
                if (window && owner)
                    window->setTransientParent(owner);
            }
        }
    }
    return QObject::eventFilter(watched, event);
}

void ApplicationController::start(const QStringList &arguments)
{
    m_tray.show();
    if (m_config
            .value(QStringLiteral("interface/hideTrayIcon"),
                   m_config.value(QStringLiteral("hide_tray_icon"), false))
            .toBool())
        m_tray.hide();
    QTimer::singleShot(0, this, [this, arguments] { dispatchArguments(arguments); });
    m_updates.start();
    qInfo() << "snipaste started" << QCoreApplication::applicationVersion();
}
void ApplicationController::applySettings()
{
    applyApplicationSettings(m_config);
    m_input.applySettings();
    m_capture.applySettings();
    m_history.setMaximumCount(
        qBound(1, m_config.value(QStringLiteral("capture/maxHistory"), 20).toInt(), 999));
    applyPasteSettings(m_config, m_pastes);
    m_tray.refreshIcon();
}
void ApplicationController::openWhiteboard(QColor color, bool transparent, bool activeScreenOnly)
{
    QList<QScreen *> screens;
    if (activeScreenOnly)
    {
        QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        if (screen)
            screens.append(screen);
    }
    else
    {
        screens = QGuiApplication::screens();
    }
    for (QScreen *screen : screens)
    {
        auto *window = new WhiteboardWindow(screen, color, transparent);
        window->show();
        window->raise();
        window->activateWindow();
        window->setFocus();
    }
}

} // namespace snipaste::app
