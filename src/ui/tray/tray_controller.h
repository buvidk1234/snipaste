#pragma once

#include <QObject>
#include <memory>

class QSystemTrayIcon;
class QMenu;

namespace snipate::core {
class CommandRegistry;
class ConfigStore;
}

namespace snipate::ui {

class TrayController final : public QObject
{
    Q_OBJECT

public:
    TrayController(core::CommandRegistry &commands, core::ConfigStore &config, QObject *parent = nullptr);
    ~TrayController() override;
    void show();
    void hide();
    QMenu *contextMenu() const;

private:
    core::CommandRegistry &m_commands;
    core::ConfigStore &m_config;
    QSystemTrayIcon *m_tray = nullptr;
    std::unique_ptr<QMenu> m_menu;
};

} // namespace snipate::ui
