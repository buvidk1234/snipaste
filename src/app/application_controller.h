#pragma once
#include "app/capture_controller.h"
#include "app/input_controller.h"
#include "app/screenshot_history_service.h"
#include "app/screenshot_output.h"
#include "core/command/command_registry.h"
#include "ui/paste/paste_manager.h"
#include "ui/tray/tray_controller.h"
#include "ui/update/update_controller.h"
#include <QObject>
#include <memory>
class QApplication;
class QCommandLineParser;
namespace snipaste::ui
{
class PreferencesDialog;
}
namespace snipaste::platform
{
class SingleInstance;
}
namespace snipaste::app
{
// Composition root and command wiring. Capture, input, persistence and
// output each own their own state and lifetimes.
class ApplicationController final : public QObject
{
  public:
    ApplicationController(QApplication &app, core::ConfigStore &config, platform::SingleInstance &instance);
    ~ApplicationController() override;
    void start(const QStringList &arguments);
    bool dispatchArguments(const QStringList &arguments, bool quitAfterFlow = true);

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    void dispatchCaptureArguments(const QCommandLineParser &parser, const QString &command);
    void dispatchPasteArguments(const QCommandLineParser &parser, const QStringList &positionalArguments);
    void applySettings();
    void registerCaptureCommands();
    void registerPasteCommands();
    void registerUtilityCommands();
    void openWhiteboard(QColor color, bool transparent, bool activeScreenOnly);
    QApplication &m_app;
    core::ConfigStore &m_config;
    platform::SingleInstance &m_instance;
    core::CommandRegistry m_commands;
    ui::UpdateController m_updates;
    ui::PasteManager m_pastes;
    ScreenshotHistoryService m_history;
    ScreenshotOutput m_output;
    CaptureController m_capture;
    InputController m_input;
    ui::TrayController m_tray;
    QString m_lastPastedFileClipboard;
    QString m_pasteStatePath;
    std::unique_ptr<ui::PreferencesDialog> m_preferences;
};
} // namespace snipaste::app
