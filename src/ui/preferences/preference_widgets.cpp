#include "ui/preferences/preference_widgets.h"
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QStandardPaths>
#include <QToolButton>

namespace snipaste::ui::preferences
{
QLabel *makeHint(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setStyleSheet(QStringLiteral("color: #666666;"));
    return label;
}

QPushButton *makeColorButton(const QColor &color, QWidget *parent)
{
    auto *button = new QPushButton(parent);
    button->setFixedSize(32, 28);
    button->setStyleSheet(
        QStringLiteral("background-color: %1; border: 1px solid #999999;").arg(color.name()));
    return button;
}

QPushButton *makeInfoButton(QWidget *parent)
{
    auto *button = new QPushButton(QStringLiteral("i"), parent);
    button->setFixedSize(24, 24);
    button->setFlat(true);
    button->setStyleSheet(
        QStringLiteral("QPushButton { color: #444444; padding: 0; border: 1px solid #555555; "
                       "border-radius: 12px; font-weight: bold; }"));
    return button;
}

QPushButton *makePencilButton(QWidget *parent)
{
    auto *button = new QPushButton(parent);
    button->setFixedSize(22, 26);
    button->setFlat(true);
    button->setToolTip(QStringLiteral("编辑快捷键"));
    button->setStyleSheet(
        QStringLiteral("QPushButton { border: 1px solid transparent; padding: 0; }"
                       "QPushButton:hover { border-color: #c6c6c6; background: #f4f4f4; }"));
    QPixmap icon(16, 16);
    icon.fill(Qt::transparent);
    QPainter painter(&icon);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(QStringLiteral("#555555")), 1.6, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(3.0, 13.0), QPointF(12.0, 4.0));
    painter.setPen(QPen(QColor(QStringLiteral("#888888")), 1.0));
    painter.drawLine(QPointF(11.0, 3.0), QPointF(13.0, 5.0));
    painter.drawLine(QPointF(2.5, 13.5), QPointF(5.0, 12.8));
    painter.end();
    button->setIcon(QIcon(icon));
    button->setIconSize(QSize(16, 16));
    return button;
}

QLabel *makeHotkeyStatus(QWidget *parent)
{
    auto *status = new QLabel(parent);
    status->setFixedSize(20, 20);
    status->setToolTip(QStringLiteral("快捷键可用"));
    QPixmap icon(20, 20);
    icon.fill(Qt::transparent);
    QPainter painter(&icon);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(QStringLiteral("#31b64b")));
    painter.drawEllipse(QRectF(1.0, 1.0, 18.0, 18.0));
    painter.setPen(QPen(Qt::white, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawLine(QPointF(5.5, 10.0), QPointF(8.5, 13.0));
    painter.drawLine(QPointF(8.5, 13.0), QPointF(14.5, 6.5));
    painter.end();
    status->setPixmap(icon);
    return status;
}

QToolButton *makeCommandPreset(QLineEdit *edit, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setText(QStringLiteral("预设"));
    button->setPopupMode(QToolButton::InstantPopup);
    button->setFixedWidth(70);
    auto *menu = new QMenu(button);
    const QList<QPair<QString, QString>> presets = {
        {QStringLiteral("无"), QString()},
        {QStringLiteral("截屏"), QStringLiteral("snip")},
        {QStringLiteral("贴图"), QStringLiteral("paste")},
        {QStringLiteral("隐藏/显示所有贴图"), QStringLiteral("toggle-images")},
        {QStringLiteral("切换到另一贴图组"), QStringLiteral("switch-group")},
        {QStringLiteral("显示托盘菜单"), QStringLiteral("show-tray-menu")},
        {QStringLiteral("首选项"), QStringLiteral("open-preferences")},
    };
    for (const auto &preset : presets)
    {
        auto *action = menu->addAction(preset.first);
        action->setData(preset.second);
        QObject::connect(action, &QAction::triggered, edit,
                         [edit, value = preset.second] { edit->setText(value); });
    }
    button->setMenu(menu);
    return button;
}

QString defaultOutputFileNamePattern()
{
    return QStringLiteral("Snipaste_$yyyy-MM-dd_HH-mm-ss$.png");
}

QString outputDirectoryFromPath(const QString &path)
{
    if (path.trimmed().isEmpty())
        return {};
    const QString directory = QFileInfo(path).absolutePath();
    return directory == QStringLiteral(".") ? path : directory;
}

QString outputPathForDirectory(const QString &directory, const QString &pattern)
{
    const QString base = directory.trimmed().isEmpty()
                             ? QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)
                             : directory;
    return QDir::toNativeSeparators(QDir(base).filePath(pattern));
}

QKeySequenceEdit *makeEditableHotkeyRow(const QString &label, const QKeySequence &shortcut, QWidget *parent)
{
    auto *row = new QWidget(parent);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 1, 0, 1);
    layout->setSpacing(5);
    auto *editAction = makePencilButton(row);
    auto *name = new QLabel(label, row);
    name->setMinimumWidth(132);
    auto *edit = new QKeySequenceEdit(shortcut, row);
    edit->setFixedHeight(26);
    edit->setMaximumSequenceLength(1);
    edit->setMinimumWidth(64);
    if (auto *input = edit->findChild<QLineEdit *>())
    {
        input->setAlignment(Qt::AlignCenter);
        input->setPlaceholderText(QStringLiteral("按下快捷键"));
    }
    edit->setToolTip(QStringLiteral("单击后按下新的快捷键；留空可禁用该动作。"));
    auto *clear = new QPushButton(QStringLiteral("×"), row);
    clear->setFixedSize(25, 25);
    clear->setToolTip(QStringLiteral("清除快捷键"));
    clear->setStyleSheet(
        QStringLiteral("QPushButton { color: #666666; background: #eeeeee; border: 1px solid #bdbdbd; "
                       "border-radius: 12px; padding: 0; font-size: 16px; }"
                       "QPushButton:hover { background: #e0e0e0; }"));
    auto *status = makeHotkeyStatus(row);
    status->setObjectName(QStringLiteral("hotkeyStatus"));
    const auto updateStatus = [edit, status, clear] {
        const bool set = !edit->keySequence().isEmpty();
        status->setVisible(set);
        clear->setEnabled(set);
        status->setToolTip(QStringLiteral("已设置；关闭首选项后注册，冲突信息见程序日志。"));
    };
    QObject::connect(edit, &QKeySequenceEdit::keySequenceChanged, row, updateStatus);
    updateStatus();
    clear->setObjectName(QStringLiteral("clearHotkey"));
    layout->addWidget(name);
    layout->insertWidget(0, editAction);
    layout->addWidget(edit, 1);
    layout->addWidget(clear);
    layout->addWidget(status);
    QObject::connect(editAction, &QPushButton::clicked, edit, [edit] { edit->setFocus(); });
    QObject::connect(clear, &QPushButton::clicked, edit, [edit] { edit->clear(); });
    return edit;
}

QWidget *makeHotkeyRow(const QString &label, const QString &shortcut, QWidget *parent)
{
    auto *edit = makeEditableHotkeyRow(label, QKeySequence(shortcut), parent);
    edit->setEnabled(false);
    edit->setToolTip(QStringLiteral("此命令暂使用默认快捷键。"));
    if (auto *input = edit->findChild<QLineEdit *>())
        input->setReadOnly(true);
    return edit->parentWidget();
}

void compactLayoutTree(QLayout *layout)
{
    if (!layout)
        return;
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);
    for (int index = 0; index < layout->count(); ++index)
    {
        QLayoutItem *item = layout->itemAt(index);
        if (item->layout())
        {
            compactLayoutTree(item->layout());
        }
        else if (item->widget() && item->widget()->layout())
        {
            compactLayoutTree(item->widget()->layout());
        }
    }
}

} // namespace snipaste::ui::preferences
