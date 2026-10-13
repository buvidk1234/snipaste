#pragma once

#include <QObject>
#include <QList>
#include <memory>

class QSystemTrayIcon;
class QMenu;
class QAction;

namespace snipaste::core {
class CommandRegistry;
class ConfigStore;
}

namespace snipaste::ui {

class TrayController final : public QObject
{
    Q_OBJECT

public:
    TrayController(core::CommandRegistry &commands, core::ConfigStore &config, QObject *parent = nullptr);
    ~TrayController() override;
    void show();
    void hide();
    void refreshIcon();
    void setExtendedMenuEnabled(bool enabled);
    QMenu *contextMenu() const;

private:
    core::CommandRegistry &m_commands;
    core::ConfigStore &m_config;
    QSystemTrayIcon *m_tray = nullptr;
    std::unique_ptr<QMenu> m_menu;
    QList<QAction *> m_extendedActions;
};

} // namespace snipaste::ui
