#include "app/input_controller.h"
#include "app/capture_controller.h"
#include "app/runtime_settings.h"
#include "core/command/command_registry.h"
#include "core/config/config_store.h"
#include "platform/desktop_integration.h"
#include "platform/keyboard.h"
#include <QApplication>
#include <QCursor>
#include <QDebug>
#include <QScreen>
#include <QtMath>

namespace snipaste::app
{

InputController::InputController(core::ConfigStore &config, core::CommandRegistry &commands,
                                 CaptureController &capture, QObject *parent)
    : QObject(parent), m_config(config), m_commands(commands), m_capture(capture)
{
    m_clock.start();
    QObject::connect(&m_cornerTimer, &QTimer::timeout, this, &InputController::sampleHotCorner);
    QObject::connect(
        &m_superCapture, &platform::SuperCapture::rectangleCaptured, this, [this](const QRect &region) {
            m_capture.start({.autoCopy = true, .selection = region, .pin = true, .superCapture = true});
        });
}
void InputController::registerHotkey(const QString &key, const QString &command, int id, bool allowSimple)
{
    const auto spec = platform::parseHotkey(key);
    if (!spec.valid || (!allowSimple && spec.simple) || !m_commands.contains(command))
        return;
    auto hotkey = std::make_unique<platform::GlobalHotkey>();
    if (!hotkey->registerKey(id, spec.modifiers, spec.key))
    {
        qWarning() << "global hotkey unavailable" << command << key;
        return;
    }
    hotkey->setEnabled(m_enabled);
    QObject::connect(hotkey.get(), &platform::GlobalHotkey::triggered, this, [this, command](int) {
        if (m_enabled && !isHotkeyIgnored(m_config))
            m_commands.execute(command);
    });
    m_hotkeys.push_back(std::move(hotkey));
}
void InputController::applySettings()
{
    m_hotkeys.clear();
    struct Binding
    {
        const char *key;
        QString fallback;
        const char *command;
        int id;
    };
    const Binding bindings[] = {
        {"hotkeys/snip", QStringLiteral("F1"), "snip.start", 1},
        {"hotkeys/copy", platform::primaryPortableShortcutText(QStringLiteral("F1")), "snip.copy", 3},
        {"hotkeys/paste", QStringLiteral("F3"), "paste.clipboard", 2},
        {"hotkeys/custom", QStringLiteral("Shift+F1"), "snip.custom", 4},
        {"hotkeys/toggleImages", QStringLiteral("Shift+F3"), "paste.toggleAll", 5},
        {"hotkeys/switchGroup", platform::primaryPortableShortcutText(QStringLiteral("F3")),
         "paste.switchGroup", 6},
        {"hotkeys/clickThrough", {}, "paste.toggleClickThrough", 7},
    };
    for (const auto &binding : bindings)
        registerHotkey(m_config.value(QString::fromLatin1(binding.key), binding.fallback).toString(),
                       QString::fromLatin1(binding.command), binding.id);
    const auto custom = configuredProcessList(m_config, QStringLiteral("hotkeys/commands"),
                                              QStringLiteral("hotkeys/customCommands"));
    const bool allowSimple = m_config
                                 .value(QStringLiteral("hotkeys/allow_simple_hotkeys"),
                                        m_config.value(QStringLiteral("allow_simple_hotkeys"), false))
                                 .toBool();
    int id = 100;
    for (const QString &binding : custom)
    {
        const int separator = binding.indexOf(QChar('='));
        if (separator <= 0 || separator >= binding.size() - 1)
            continue;
        QString command = binding.left(separator).trimmed();
        QString key = binding.mid(separator + 1).trimmed();
        if (!platform::parseHotkey(key).valid)
            std::swap(command, key);
        registerHotkey(key, command, id++, allowSimple);
    }
    configureSuperCapture();
    configureHotCorners();
}
void InputController::setHotkeysEnabled(bool enabled)
{
    m_enabled = enabled;
    for (const auto &hotkey : m_hotkeys)
        hotkey->setEnabled(enabled);
}
void InputController::setCornersEnabled(bool enabled)
{
    m_config.setValue(QStringLiteral("control/cornerEnabled"), enabled);
    m_config.save();
    configureHotCorners();
}
void InputController::configureSuperCapture()
{
    const QString modifier =
        m_config.value(QStringLiteral("control/superCaptureModifier"), QStringLiteral("none")).toString();
    Qt::KeyboardModifier selectedModifier = Qt::NoModifier;
    if (modifier.contains(QStringLiteral("Cmd"), Qt::CaseInsensitive) ||
        modifier.contains(QStringLiteral("Meta"), Qt::CaseInsensitive) ||
        modifier.contains(QStringLiteral("Command"), Qt::CaseInsensitive))
        selectedModifier = Qt::MetaModifier;
    else if (modifier.contains(QStringLiteral("Ctrl"), Qt::CaseInsensitive))
        selectedModifier = Qt::ControlModifier;
    else if (modifier.contains(QStringLiteral("Shift"), Qt::CaseInsensitive))
        selectedModifier = Qt::ShiftModifier;
    else if (modifier.contains(QStringLiteral("Alt"), Qt::CaseInsensitive))
        selectedModifier = Qt::AltModifier;
    m_superCapture.setModifier(selectedModifier);
    const QString mouse =
        m_config.value(QStringLiteral("control/superCaptureMouse"), QStringLiteral("left")).toString();
    m_superCapture.setMouseButton(mouse.contains(QStringLiteral("right"), Qt::CaseInsensitive) ||
                                          mouse == QStringLiteral("2")
                                      ? Qt::RightButton
                                      : Qt::LeftButton);
    m_superCapture.setBlockWindowsKeys(
        m_config.value(QStringLiteral("capture/blockWinLeft"), true).toBool(),
        m_config.value(QStringLiteral("capture/blockWinRight"), false).toBool());
    // The reference UI uses “none” as the disabled state. Keep an
    // explicit boolean for users who edit config.ini directly.
    const bool enabled = m_config.value(QStringLiteral("control/superCaptureEnabled"), false).toBool() ||
                         selectedModifier != Qt::NoModifier;
    m_superCapture.setEnabled(enabled);
}
void InputController::configureHotCorners()
{
    m_cornerState.reset();
    m_cornerTimer.setInterval(
        qBound(10, m_config.value(QStringLiteral("control/cornerInterval"), 125).toInt(), 2000));
    if (m_config.value(QStringLiteral("control/cornerEnabled"), false).toBool())
        m_cornerTimer.start();
    else
        m_cornerTimer.stop();
}
void InputController::sampleHotCorner()
{

    const int interval =
        qBound(10, m_config.value(QStringLiteral("control/cornerInterval"), 125).toInt(), 2000);
    if (m_cornerTimer.interval() != interval)
        m_cornerTimer.setInterval(interval);
    const bool enabled = m_config.value(QStringLiteral("control/cornerEnabled"), false).toBool();
    if (!enabled || m_capture.active() || QApplication::activeModalWidget() ||
        QApplication::activePopupWidget() || QGuiApplication::mouseButtons() != Qt::NoButton ||
        isApplicationIgnored(m_config, QStringLiteral("control/cornerIgnoredApplications")))
    {
        m_cornerState.reset();
        return;
    }
    const QPoint cursor = QCursor::pos();
    QScreen *screen = QGuiApplication::screenAt(cursor);
    if (!screen)
    {
        m_cornerState.reset();
        return;
    }
    // Screen corners, including the taskbar area. Convert the physical
    // pixel setting to the logical coordinates returned by QCursor.
    const int size =
        qMax(1, qCeil(qBound(1, m_config.value(QStringLiteral("control/cornerSize"), 10).toInt(), 200) /
                      screen->devicePixelRatio()));
    const int corner = m_cornerState.sample(
        cursor, screen->geometry(), size, m_clock.elapsed(),
        qBound(0, m_config.value(QStringLiteral("control/cornerDelay"), 300).toInt(), 5000), true);
    if (corner < 0)
        return;
    const QStringList keys = {
        QStringLiteral("control/cornerTopLeft"), QStringLiteral("control/cornerTopRight"),
        QStringLiteral("control/cornerBottomLeft"), QStringLiteral("control/cornerBottomRight")};
    const QString command =
        m_config.value(keys.at(corner), corner < 2 ? QStringLiteral("toggle-images") : QString()).toString();
    if (!command.trimmed().isEmpty())
        m_commands.execute(command);
}

} // namespace snipaste::app
