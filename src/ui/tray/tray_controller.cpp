#include "ui/tray/tray_controller.h"

#include "core/command/command_registry.h"
#include "core/config/config_store.h"
#include "platform/keyboard.h"

#include <QAction>
#include <QApplication>
#include <QCursor>
#include <QKeySequence>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QSystemTrayIcon>

namespace snipate::ui {

namespace {

QIcon makeTrayIcon()
{
    QIcon icon;
    for (const int size : {16, 24, 32, 48}) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF body(size * 0.10, size * 0.10, size * 0.78, size * 0.78);
        painter.setPen(QPen(QColor("#7d8790"), qMax(1, size / 16)));
        painter.setBrush(QColor("#ffffff"));
        painter.drawRoundedRect(body, size * 0.12, size * 0.12);

        // Four-color Snipaste-like mark. The green badge indicates that the
        // background resident helper is active, matching the reference tray
        // affordance without relying on an external icon file.
        const QRectF mark(size * 0.21, size * 0.19, size * 0.52, size * 0.52);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#2f82d2"));
        painter.drawRect(QRectF(mark.left(), mark.top(), mark.width() / 2, mark.height() / 2));
        painter.setBrush(QColor("#e36354"));
        painter.drawRect(QRectF(mark.center().x(), mark.top(), mark.width() / 2, mark.height() / 2));
        painter.setBrush(QColor("#f0bf45"));
        painter.drawRect(QRectF(mark.left(), mark.center().y(), mark.width() / 2, mark.height() / 2));
        painter.setBrush(QColor("#58b85e"));
        painter.drawRect(QRectF(mark.center().x(), mark.center().y(), mark.width() / 2, mark.height() / 2));

        painter.setBrush(QColor("#36b34a"));
        painter.drawEllipse(QRectF(size * 0.56, size * 0.58, size * 0.30, size * 0.30));
        painter.setPen(QPen(Qt::white, qMax(1, size / 12), Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(size * 0.63, size * 0.73), QPointF(size * 0.69, size * 0.78));
        painter.drawLine(QPointF(size * 0.69, size * 0.78), QPointF(size * 0.79, size * 0.66));
        icon.addPixmap(pixmap);
    }
    return icon;
}

} // namespace

TrayController::TrayController(core::CommandRegistry &commands, core::ConfigStore &config, QObject *parent)
    : QObject(parent)
    , m_commands(commands)
    , m_config(config)
    , m_tray(new QSystemTrayIcon(this))
{
    m_tray->setIcon(makeTrayIcon());
    m_tray->setToolTip(QStringLiteral("snipate"));

    m_menu = std::make_unique<QMenu>();
    auto *menu = m_menu.get();
    menu->setObjectName(QStringLiteral("trayContextMenu"));
    m_commands.registerCommand({QStringLiteral("tray.showMenu"), QStringLiteral("显示托盘菜单"),
                               [this] { m_menu->popup(QCursor::pos()); }});
    menu->setStyleSheet(QStringLiteral(
        "QMenu { background: #ffffff; color: #202124; border: 1px solid #aeb8c2; "
        "border-radius: 7px; padding: 2px 0; font-family: 'Segoe UI'; font-size: 9pt; }"
        "QMenu::item { padding: 3px 18px 3px 10px; margin: 0 2px; border-radius: 2px; }"
        "QMenu::item:selected { background: #86c1ea; color: #111111; }"
        "QMenu::separator { height: 1px; background: #d7d7d7; margin: 3px 6px; }"
        "QMenu::right-arrow { image: none; border: 4px solid transparent; "
        "border-left-color: #666666; width: 0; height: 0; margin-right: 2px; }"));
    const auto addCommand = [this](QMenu *target,
                                   const QString &label,
                                         const QString &command,
                                         const QKeySequence &shortcut = {}) {
        auto *action = target->addAction(label);
        if (!shortcut.isEmpty()) {
            action->setShortcut(shortcut);
            action->setShortcutVisibleInContextMenu(true);
        }
        QObject::connect(action, &QAction::triggered, this, [this, command] {
            m_commands.execute(command);
        });
        return action;
    };

    // Keep the primary menu identical in density and order to Snipaste's
    // reference menu. Less common operations live under “更多功能”.
    addCommand(menu, QStringLiteral("截屏"), QStringLiteral("snip.start"), QKeySequence(Qt::Key_F1));
    addCommand(menu, QStringLiteral("截屏并自动复制"),
               QStringLiteral("snip.copy"),
               snipate::platform::primaryKeySequence(Qt::Key_F1));
    addCommand(menu, QStringLiteral("自定义截屏"),
               QStringLiteral("snip.custom"),
               QKeySequence(Qt::SHIFT | Qt::Key_F1));
    addCommand(menu, QStringLiteral("贴图"), QStringLiteral("paste.clipboard"), QKeySequence(Qt::Key_F3));
    addCommand(menu, QStringLiteral("隐藏/显示所有贴图"),
               QStringLiteral("paste.toggleAll"),
               QKeySequence(Qt::SHIFT | Qt::Key_F3));
    addCommand(menu, QStringLiteral("切换到另一贴图组"),
               QStringLiteral("paste.switchGroup"),
               snipate::platform::primaryKeySequence(Qt::Key_F3));
    addCommand(menu, QStringLiteral("清空截屏历史"), QStringLiteral("history.clear"));
    menu->addSeparator();

    addCommand(menu, QStringLiteral("禁用快捷键"), QStringLiteral("hotkeys.toggle"));
    menu->addSeparator();

    auto *pasteMenu = menu->addMenu(QStringLiteral("贴图"));
    addCommand(pasteMenu, QStringLiteral("从剪贴板贴图"), QStringLiteral("paste.clipboard"),
               QKeySequence(Qt::Key_F3));
    addCommand(pasteMenu, QStringLiteral("隐藏/显示所有贴图"), QStringLiteral("paste.toggleAll"),
               QKeySequence(Qt::SHIFT | Qt::Key_F3));
    addCommand(pasteMenu, QStringLiteral("切换到另一贴图组"), QStringLiteral("paste.switchGroup"),
               snipate::platform::primaryKeySequence(Qt::Key_F3));
    pasteMenu->addSeparator();
    addCommand(pasteMenu, QStringLiteral("管理贴图组"), QStringLiteral("paste.groupManager"));
    addCommand(pasteMenu, QStringLiteral("Solo 模式"), QStringLiteral("paste.solo"));
    addCommand(pasteMenu, QStringLiteral("清空当前贴图组"), QStringLiteral("paste.emptyGroup"));
    addCommand(pasteMenu, QStringLiteral("销毁所有贴图"), QStringLiteral("paste.destroyAll"));

    menu->addSeparator();

    addCommand(menu, QStringLiteral("首选项…"), QStringLiteral("preferences.open"));
    addCommand(menu, QStringLiteral("帮助"), QStringLiteral("help.open"));
    addCommand(menu, QStringLiteral("检查更新…"), QStringLiteral("update.checkNow"));
    menu->addSeparator();
    addCommand(menu, QStringLiteral("重新启动"), QStringLiteral("app.restart"));
    auto *quitAction = menu->addAction(QStringLiteral("退出"));
    QObject::connect(quitAction, &QAction::triggered, this, [this] {
        const auto answer = QMessageBox::question(nullptr,
                                                   QStringLiteral("退出 snipate"),
                                                   QStringLiteral("确定要退出 snipate 吗？"),
                                                   QMessageBox::Yes | QMessageBox::No,
                                                   QMessageBox::No);
        if (answer == QMessageBox::Yes)
            m_commands.execute(QStringLiteral("app.quit"));
    });

    const auto executeTrayAction = [this](const QString &key, const QString &fallback) {
        const QString action = m_config.value(key, fallback).toString().trimmed();
        if (action.isEmpty())
            return;
        if (!m_commands.execute(action))
            m_tray->showMessage(QStringLiteral("托盘操作未执行"), QStringLiteral("未知命令：%1").arg(action));
    };
    QObject::connect(m_tray,
                     &QSystemTrayIcon::activated,
                     this,
                     [executeTrayAction](QSystemTrayIcon::ActivationReason reason) {
                         if (reason == QSystemTrayIcon::Trigger)
                             executeTrayAction(QStringLiteral("control/trayLeft"), QStringLiteral("snip"));
                         else if (reason == QSystemTrayIcon::MiddleClick)
                             executeTrayAction(QStringLiteral("control/trayMiddle"), QStringLiteral("paste"));
                         else if (reason == QSystemTrayIcon::Context)
                             executeTrayAction(QStringLiteral("control/trayRight"), QStringLiteral("show-tray-menu"));
                     });

    // Handle Context ourselves: Qt's automatic menu would otherwise open
    // even when the user bound the right button to a different command.
}

TrayController::~TrayController() = default;

void TrayController::show()
{
    m_tray->show();
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        m_tray->showMessage(QStringLiteral("snipate"), QStringLiteral("系统托盘不可用"));
        return;
    }
    m_tray->showMessage(QStringLiteral("snipate 已启动"),
                        QStringLiteral("程序已驻留系统托盘：F1 截图，F3 贴图；右键托盘图标打开菜单。"),
                        QSystemTrayIcon::Information,
                        3500);
}

void TrayController::hide()
{
    m_tray->hide();
}

QMenu *TrayController::contextMenu() const
{
    return m_menu.get();
}

} // namespace snipate::ui
