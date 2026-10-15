#pragma once
#include "core/control/hot_corner_state.h"
#include "platform/windows/global_hotkey.h"
#include "platform/windows/super_capture.h"
#include <QElapsedTimer>
#include <QTimer>
#include <memory>
#include <vector>
namespace snipaste::core
{
class ConfigStore;
class CommandRegistry;
} // namespace snipaste::core
namespace snipaste::app
{
class CaptureController;
class InputController final : public QObject
{
  public:
    InputController(core::ConfigStore &config, core::CommandRegistry &commands, CaptureController &capture,
                    QObject *parent = nullptr);
    void applySettings();
    void setHotkeysEnabled(bool enabled);
    bool hotkeysEnabled() const { return m_enabled; }
    void setCornersEnabled(bool enabled);

  private:
    void configureSuperCapture();
    void configureHotCorners();
    void sampleHotCorner();
    void registerHotkey(const QString &key, const QString &command, int id, bool allowSimple = true);
    core::ConfigStore &m_config;
    core::CommandRegistry &m_commands;
    CaptureController &m_capture;
    std::vector<std::unique_ptr<platform::GlobalHotkey>> m_hotkeys;
    platform::SuperCapture m_superCapture;
    QTimer m_cornerTimer;
    core::HotCornerState m_cornerState;
    QElapsedTimer m_clock;
    bool m_enabled = true;
};
} // namespace snipaste::app
