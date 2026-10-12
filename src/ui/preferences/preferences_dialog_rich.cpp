#include "ui/preferences/preferences_dialog.h"

#include "core/config/config_store.h"

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QApplication>
#include <QColorDialog>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFormLayout>
#include <QFont>
#include <QFrame>
#include <QGroupBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMenu>
#include <QPushButton>
#include <QPainter>
#include <QPixmap>
#include <QRadioButton>
#include <QDir>
#include <QRegularExpression>
#include <QKeySequence>
#include <QKeySequenceEdit>
#include <QScrollArea>
#include <QTableWidget>
#include <QHeaderView>
#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QSizePolicy>
#include <QScreen>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace snipate::ui {

namespace {

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
    button->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #999999;")
                              .arg(color.name()));
    return button;
}

QPushButton *makeInfoButton(QWidget *parent)
{
    auto *button = new QPushButton(QStringLiteral("i"), parent);
    button->setFixedSize(24, 24);
    button->setFlat(true);
    button->setStyleSheet(QStringLiteral(
        "QPushButton { color: #444444; padding: 0; border: 1px solid #555555; "
        "border-radius: 12px; font-weight: bold; }"));
    return button;
}

QPushButton *makePencilButton(QWidget *parent)
{
    auto *button = new QPushButton(parent);
    button->setFixedSize(22, 26);
    button->setFlat(true);
    button->setToolTip(QStringLiteral("编辑快捷键"));
    button->setStyleSheet(QStringLiteral(
        "QPushButton { border: 1px solid transparent; padding: 0; }"
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
    for (const auto &preset : presets) {
        auto *action = menu->addAction(preset.first);
        action->setData(preset.second);
        QObject::connect(action, &QAction::triggered, edit, [edit, value = preset.second] { edit->setText(value); });
    }
    button->setMenu(menu);
    return button;
}

QString defaultOutputFileNamePattern()
{
    return QStringLiteral("Snipaste_$yyyy-MM-dd_HH-mm-ss$.png");
}

QString expandOutputPattern(const QString &pattern, const QDateTime &dateTime)
{
    QString expanded = pattern;
    QRegularExpression token(QStringLiteral("\\$([^$]+)\\$"));
    QRegularExpressionMatch match = token.match(expanded);
    while (match.hasMatch()) {
        expanded.replace(match.capturedStart(), match.capturedLength(),
                         dateTime.toString(match.captured(1)));
        match = token.match(expanded);
    }
    return expanded;
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

QKeySequenceEdit *makeEditableHotkeyRow(const QString &label,
                                        const QKeySequence &shortcut,
                                        QWidget *parent)
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
    if (auto *input = edit->findChild<QLineEdit *>()) {
        input->setAlignment(Qt::AlignCenter);
        input->setPlaceholderText(QStringLiteral("按下快捷键"));
    }
    edit->setToolTip(QStringLiteral("单击后按下新的快捷键；留空可禁用该动作。"));
    auto *clear = new QPushButton(QStringLiteral("×"), row);
    clear->setFixedSize(25, 25);
    clear->setToolTip(QStringLiteral("清除快捷键"));
    clear->setStyleSheet(QStringLiteral(
        "QPushButton { color: #666666; background: #eeeeee; border: 1px solid #bdbdbd; "
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
    QObject::connect(editAction, &QPushButton::clicked, edit, [edit] {
        edit->setFocus();
    });
    QObject::connect(clear, &QPushButton::clicked, edit, [edit] {
        edit->clear();
    });
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
    for (int index = 0; index < layout->count(); ++index) {
        QLayoutItem *item = layout->itemAt(index);
        if (item->layout()) {
            compactLayoutTree(item->layout());
        } else if (item->widget() && item->widget()->layout()) {
            compactLayoutTree(item->widget()->layout());
        }
    }
}

} // namespace

PreferencesDialog::PreferencesDialog(core::ConfigStore &config, QWidget *parent)
    : QDialog(parent)
    , m_config(config)
{
    setWindowTitle(QStringLiteral("Snipaste 首选项 [专业版]"));
    setModal(true);
    setAttribute(Qt::WA_QuitOnClose, false);
    // Snipaste keeps a fixed, narrow preference window. The height follows
    // the selected page, while the width remains constant across all tabs.
    // Keep the professional preferences window fixed at its reference width
    // in Qt logical pixels. Qt scales the complete window for Windows DPI;
    // the toolbar page has enough horizontal room for the complete palette
    // action row while the width remains fixed across all pages.
    // Keep the professional reference window fixed horizontally.  The
    // toolbar page needs the extra width for its palette action row.
    setFixedWidth(508);
    setMinimumHeight(0);
    setMaximumHeight(QWIDGETSIZE_MAX);
    setWindowFlag(Qt::WindowStaysOnTopHint, true);
    QFont compactFont = QApplication::font();
    compactFont.setPointSize(9);
    setFont(compactFont);
    setStyleSheet(QStringLiteral(
        "QDialog { background: #f3f2ef; }"
        "QTabWidget::pane { background: #ffffff; border: 1px solid #d5d1c9; }"
        "QTabBar::tab { background: #e7e4de; border: 1px solid #d5d1c9; "
        "padding: 4px 8px; min-width: 32px; }"
        "QTabBar::tab:selected { background: #ffffff; border-bottom-color: #ffffff; }"
        "QGroupBox { margin-top: 8px; padding: 5px; border: 1px solid #d8d5cf; }"
        "QPushButton { min-height: 24px; padding: 3px 10px; }"
        "QLineEdit, QComboBox, QKeySequenceEdit { min-height: 22px; }"
        // Keep QSpinBox on the platform style.  Styling the spin box itself
        // makes Qt replace the Windows up/down glyphs with large, filled
        // triangles, which is visibly different from the compact reference UI.
        "QComboBox { padding-left: 3px; padding-right: 3px; }"
        "QCheckBox { spacing: 5px; }"));

    auto *tabs = new QTabWidget(this);

    tabs->setObjectName(QStringLiteral("preferencesTabs"));
    auto *generalPage = createGeneralPage(tabs);
    tabs->addTab(generalPage, QStringLiteral("常规"));
    auto *interfacePage = createInterfacePage(tabs);
    auto *interfaceTabs = interfacePage->findChild<QTabWidget *>();
    tabs->addTab(interfacePage, QStringLiteral("界面"));

    auto *capturePage = createCapturePage(tabs);
    auto *captureTabs = capturePage->findChild<QTabWidget *>(QStringLiteral("captureTabs"));
    tabs->addTab(capturePage, QStringLiteral("截屏"));

    auto *pastePage = new QWidget(tabs);
    auto *pasteLayout = new QVBoxLayout(pastePage);
    auto *pasteTabs = new QTabWidget(pastePage);
    pasteTabs->setObjectName(QStringLiteral("pasteTabs"));

    auto *pasteDisplay = new QWidget(pasteTabs);
    auto *pasteDisplayForm = new QFormLayout(pasteDisplay);
    pasteDisplayForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    pasteDisplayForm->setHorizontalSpacing(10);
    pasteDisplayForm->setVerticalSpacing(7);
    auto *shadowRow = new QWidget(pasteDisplay);
    auto *shadowLayout = new QHBoxLayout(shadowRow);
    shadowLayout->setContentsMargins(0, 0, 0, 0);
    m_pasteShadowCheck = new QCheckBox(QStringLiteral("窗口阴影"), shadowRow);
    m_pasteShadowCheck->setObjectName(QStringLiteral("pasteShadowCheck"));
    m_pasteShadowCheck->setChecked(m_config.value(QStringLiteral("paste/shadow"), true).toBool());
    shadowLayout->addWidget(m_pasteShadowCheck);
    auto *shadowColor = makeColorButton(QColor(QStringLiteral("#808080")), shadowRow);
    shadowColor->setObjectName(QStringLiteral("pasteShadowColor"));
    shadowLayout->addWidget(shadowColor);
    shadowLayout->addStretch();
    pasteDisplayForm->addRow(shadowRow);
    m_pasteHelpKeysCheck = new QCheckBox(QStringLiteral("在右键菜单显示助记键"), pasteDisplay);
    m_pasteHelpKeysCheck->setObjectName(QStringLiteral("pasteHelpKeysCheck"));
    m_pasteHelpKeysCheck->setChecked(m_config.value(QStringLiteral("paste/helpKeys"), false).toBool());
    pasteDisplayForm->addRow(m_pasteHelpKeysCheck);
    auto *backgroundMode = new QComboBox(pasteDisplay);
    backgroundMode->setObjectName(QStringLiteral("pasteBackgroundMode"));
    backgroundMode->setFixedWidth(120);
    backgroundMode->addItems({QStringLiteral("伪透明"), QStringLiteral("透明"), QStringLiteral("白色"), QStringLiteral("黑色")});
    backgroundMode->setCurrentText(m_config.value(QStringLiteral("paste/backgroundMode"), QStringLiteral("伪透明")).toString());
    pasteDisplayForm->addRow(QStringLiteral("默认背景模式:"), backgroundMode);
    m_pasteOpacitySpin = new QSpinBox(pasteDisplay);
    m_pasteOpacitySpin->setFixedWidth(92);
    m_pasteOpacitySpin->setRange(10, 100);
    m_pasteOpacitySpin->setSuffix(QStringLiteral(" %"));
    const double rawPasteOpacity = m_config.value(QStringLiteral("paste/opacity"), 100).toDouble();
    m_pasteOpacitySpin->setValue(qBound(10,
                                        qRound(rawPasteOpacity > 1.0
                                                   ? rawPasteOpacity
                                                   : rawPasteOpacity * 100.0),
                                        100));
    pasteDisplayForm->addRow(QStringLiteral("默认不透明度:"), m_pasteOpacitySpin);
    m_pasteMaxSizeSpin = new QSpinBox(pasteDisplay);
    m_pasteMaxSizeSpin->setFixedWidth(112);
    m_pasteMaxSizeSpin->setRange(256, 20000);
    m_pasteMaxSizeSpin->setSuffix(QStringLiteral(" 像素"));
    m_pasteMaxSizeSpin->setValue(m_config.value(QStringLiteral("paste/maxSize"), 12000).toInt());
    pasteDisplayForm->addRow(QStringLiteral("最大窗口尺寸:"), m_pasteMaxSizeSpin);
    auto *thumbRow = new QWidget(pasteDisplay);
    auto *thumbLayout = new QHBoxLayout(thumbRow);
    thumbLayout->setContentsMargins(0, 0, 0, 0);
    m_pasteThumbWidthSpin = new QSpinBox(thumbRow);
    m_pasteThumbHeightSpin = new QSpinBox(thumbRow);
    for (QSpinBox *spin : {m_pasteThumbWidthSpin, m_pasteThumbHeightSpin}) {
        spin->setRange(16, 512);
        spin->setFixedWidth(74);
    }
    m_pasteThumbWidthSpin->setValue(m_config.value(QStringLiteral("paste/thumbWidth"), 62).toInt());
    m_pasteThumbHeightSpin->setValue(m_config.value(QStringLiteral("paste/thumbHeight"), 62).toInt());
    thumbLayout->addWidget(m_pasteThumbWidthSpin);
    thumbLayout->addWidget(new QLabel(QStringLiteral("×"), thumbRow));
    thumbLayout->addWidget(m_pasteThumbHeightSpin);
    thumbLayout->addStretch();
    pasteDisplayForm->addRow(QStringLiteral("快速缩略图大小:"), thumbRow);
    auto *pasteDisplayRestore = new QPushButton(QStringLiteral("恢复默认"), pasteDisplay);
    pasteDisplayRestore->setFixedWidth(86);
    auto *pasteDisplayFooter = new QHBoxLayout;
    pasteDisplayFooter->addStretch();
    pasteDisplayFooter->addWidget(pasteDisplayRestore);
    pasteDisplayForm->addRow(pasteDisplayFooter);
    QObject::connect(pasteDisplayRestore, &QPushButton::clicked, this, [this, backgroundMode] {
        m_pasteShadowCheck->setChecked(true);
        m_pasteHelpKeysCheck->setChecked(false);
        m_pasteOpacitySpin->setValue(100);
        m_pasteMaxSizeSpin->setValue(12000);
        m_pasteThumbWidthSpin->setValue(62);
        m_pasteThumbHeightSpin->setValue(62);
        backgroundMode->setCurrentText(QStringLiteral("伪透明"));
    });
    m_pageSavers.append([this, backgroundMode] {
        m_config.setValue(QStringLiteral("paste/backgroundMode"), backgroundMode->currentText());
    });
    pasteTabs->addTab(pasteDisplay, QStringLiteral("显示"));

    auto *pasteBehavior = new QWidget(pasteTabs);
    auto *pasteBehaviorLayout = new QVBoxLayout(pasteBehavior);
    pasteBehaviorLayout->setContentsMargins(6, 6, 6, 6);
    auto addPasteBehaviorCheck = [this, pasteBehaviorLayout, pasteBehavior](QCheckBox *&slot,
                                                                              const QString &text,
                                                                              const QString &key,
                                                                              bool fallback) {
        slot = new QCheckBox(text, pasteBehavior);
        slot->setChecked(m_config.value(key, fallback).toBool());
        pasteBehaviorLayout->addWidget(slot);
    };
    addPasteBehaviorCheck(m_pasteUseCapturePositionCheck, QStringLiteral("使用截屏时的位置"), QStringLiteral("paste/useCapturePosition"), true);
    addPasteBehaviorCheck(m_pasteWheelCenterCheck, QStringLiteral("鼠标滚轮缩放时以指针为中心"), QStringLiteral("paste/wheelCenter"), false);
    addPasteBehaviorCheck(m_pasteSmoothScaleCheck, QStringLiteral("平滑缩放"), QStringLiteral("paste/smoothScale"), true);
    QCheckBox *pasteVirtualDesktopCheck = nullptr;
    addPasteBehaviorCheck(pasteVirtualDesktopCheck, QStringLiteral("固定到所有虚拟桌面"), QStringLiteral("paste/virtualDesktop"), true);
    pasteVirtualDesktopCheck->setObjectName(QStringLiteral("pasteVirtualDesktopCheck"));
    addPasteBehaviorCheck(m_pasteResetTransformCheck, QStringLiteral("缩略图变换‘重置’操作"), QStringLiteral("paste/resetTransform"), true);
    addPasteBehaviorCheck(m_pasteFlashTransparencyCheck, QStringLiteral("边框闪烁时使窗口回归半透明"), QStringLiteral("paste/flashTransparency"), true);
    addPasteBehaviorCheck(m_pasteFlashOnGroupSwitchCheck, QStringLiteral("切换到另一分组时自动闪烁贴图"), QStringLiteral("paste/flashGroupSwitch"), true);
    addPasteBehaviorCheck(m_pasteHideAllOnPasteKeyCheck, QStringLiteral("在贴图闪烁时按下贴图键，隐藏所有贴图"), QStringLiteral("paste/hideAllOnPasteKey"), false);
    addPasteBehaviorCheck(m_pasteKeepOnDragCheck, QStringLiteral("拖放操作时保留贴图"), QStringLiteral("paste/keepOnDrag"), false);
    QCheckBox *pasteHideOnFileDialogCheck = nullptr;
    addPasteBehaviorCheck(pasteHideOnFileDialogCheck, QStringLiteral("文件对话框打开时隐藏所有贴图"), QStringLiteral("paste/hideOnFileDialog"), false);
    pasteHideOnFileDialogCheck->setObjectName(QStringLiteral("pasteHideOnFileDialogCheck"));
    addPasteBehaviorCheck(m_pasteCloseConfirmCheck, QStringLiteral("关闭多个贴图时弹出确认对话框"), QStringLiteral("paste/closeConfirm"), true);
    addPasteBehaviorCheck(m_pasteDestroyConfirmCheck, QStringLiteral("销毁多个贴图时弹出确认对话框"), QStringLiteral("paste/destroyConfirm"), true);
    auto *newPasteRow = new QWidget(pasteBehavior);
    auto *newPasteLayout = new QHBoxLayout(newPasteRow);
    newPasteLayout->setContentsMargins(0, 0, 0, 0);
    newPasteLayout->addWidget(new QLabel(QStringLiteral("新的图片被贴出时:"), newPasteRow));
    m_pasteFlashNewCheck = new QCheckBox(QStringLiteral("闪烁"), newPasteRow);
    m_pasteFlashNewCheck->setChecked(m_config.value(QStringLiteral("paste/flashNew"), true).toBool());
    m_pasteActivateNewCheck = new QCheckBox(QStringLiteral("激活"), newPasteRow);
    m_pasteActivateNewCheck->setChecked(m_config.value(QStringLiteral("paste/activateNew"), true).toBool());
    newPasteLayout->addWidget(m_pasteFlashNewCheck);
    newPasteLayout->addWidget(m_pasteActivateNewCheck);
    newPasteLayout->addStretch();
    pasteBehaviorLayout->addWidget(newPasteRow);
    auto *restoreCountRow = new QWidget(pasteBehavior);
    auto *restoreCountLayout = new QHBoxLayout(restoreCountRow);
    restoreCountLayout->setContentsMargins(0, 0, 0, 0);
    restoreCountLayout->addWidget(new QLabel(QStringLiteral("可被恢复的贴图数:"), restoreCountRow));
    m_pasteRestoreCountSpin = new QSpinBox(restoreCountRow);
    m_pasteRestoreCountSpin->setRange(0, 99);
    m_pasteRestoreCountSpin->setValue(m_config.value(QStringLiteral("paste/restoreCount"), 1).toInt());
    restoreCountLayout->addWidget(m_pasteRestoreCountSpin);
    restoreCountLayout->addStretch();
    pasteBehaviorLayout->addWidget(restoreCountRow);
    auto *pasteBehaviorFooter = new QHBoxLayout;
    pasteBehaviorFooter->addStretch();
    auto *pasteBehaviorRestore = new QPushButton(QStringLiteral("恢复默认"), pasteBehavior);
    pasteBehaviorRestore->setFixedWidth(86);
    pasteBehaviorFooter->addWidget(pasteBehaviorRestore);
    pasteBehaviorLayout->addLayout(pasteBehaviorFooter);
    QObject::connect(pasteBehaviorRestore, &QPushButton::clicked, this, [this, pasteVirtualDesktopCheck, pasteHideOnFileDialogCheck] {
        m_pasteUseCapturePositionCheck->setChecked(true);
        m_pasteWheelCenterCheck->setChecked(false);
        m_pasteSmoothScaleCheck->setChecked(true);
        pasteVirtualDesktopCheck->setChecked(true);
        m_pasteResetTransformCheck->setChecked(true);
        m_pasteFlashTransparencyCheck->setChecked(true);
        m_pasteFlashOnGroupSwitchCheck->setChecked(true);
        m_pasteHideAllOnPasteKeyCheck->setChecked(false);
        m_pasteKeepOnDragCheck->setChecked(false);
        pasteHideOnFileDialogCheck->setChecked(false);
        m_pasteCloseConfirmCheck->setChecked(true);
        m_pasteDestroyConfirmCheck->setChecked(true);
        m_pasteFlashNewCheck->setChecked(true);
        m_pasteActivateNewCheck->setChecked(true);
        m_pasteRestoreCountSpin->setValue(1);
    });
    m_pageSavers.append([this, pasteVirtualDesktopCheck, pasteHideOnFileDialogCheck] {
        m_config.setValue(QStringLiteral("paste/virtualDesktop"), pasteVirtualDesktopCheck->isChecked());
        m_config.setValue(QStringLiteral("paste/hideOnFileDialog"), pasteHideOnFileDialogCheck->isChecked());
    });
    pasteTabs->addTab(pasteBehavior, QStringLiteral("行为"));

    auto *pasteKeys = new QWidget(pasteTabs);
    auto *pasteKeysLayout = new QVBoxLayout(pasteKeys);
    pasteKeysLayout->setContentsMargins(6, 6, 6, 6);
    auto *pasteActionGroup = new QGroupBox(QStringLiteral("动作"), pasteKeys);
    auto *pasteActionForm = new QFormLayout(pasteActionGroup);
    pasteActionForm->setContentsMargins(12, 18, 12, 10);
    pasteActionForm->setVerticalSpacing(7);
    auto *escapeAction = new QComboBox(pasteActionGroup);
    escapeAction->setObjectName(QStringLiteral("pasteEscapeAction"));
    escapeAction->setFixedWidth(112);
    escapeAction->addItems({QStringLiteral("关闭贴图"), QStringLiteral("销毁贴图"), QStringLiteral("无")});
    escapeAction->setCurrentText(m_config.value(QStringLiteral("paste/escapeAction"), QStringLiteral("关闭贴图")).toString());
    pasteActionForm->addRow(QStringLiteral("Esc:"), escapeAction);
    auto *shiftEscapeAction = new QComboBox(pasteActionGroup);
    shiftEscapeAction->setObjectName(QStringLiteral("pasteShiftEscapeAction"));
    shiftEscapeAction->setFixedWidth(112);
    shiftEscapeAction->addItems({QStringLiteral("销毁贴图"), QStringLiteral("关闭贴图"), QStringLiteral("无")});
    shiftEscapeAction->setCurrentText(m_config.value(QStringLiteral("paste/shiftEscapeAction"), QStringLiteral("销毁贴图")).toString());
    pasteActionForm->addRow(QStringLiteral("Shift + Esc:"), shiftEscapeAction);
    pasteKeysLayout->addWidget(pasteActionGroup);
    pasteKeysLayout->addStretch();
    auto *pasteKeysFooter = new QHBoxLayout;
    pasteKeysFooter->addStretch();
    auto *pasteKeysRestore = new QPushButton(QStringLiteral("恢复默认"), pasteKeys);
    pasteKeysRestore->setFixedWidth(86);
    pasteKeysFooter->addWidget(pasteKeysRestore);
    pasteKeysLayout->addLayout(pasteKeysFooter);
    QObject::connect(pasteKeysRestore, &QPushButton::clicked, this, [escapeAction, shiftEscapeAction] {
        escapeAction->setCurrentText(QStringLiteral("关闭贴图"));
        shiftEscapeAction->setCurrentText(QStringLiteral("销毁贴图"));
    });
    m_pageSavers.append([this, escapeAction, shiftEscapeAction] {
        m_config.setValue(QStringLiteral("paste/escapeAction"), escapeAction->currentText());
        m_config.setValue(QStringLiteral("paste/shiftEscapeAction"), shiftEscapeAction->currentText());
    });
    pasteTabs->addTab(pasteKeys, QStringLiteral("按键"));

    auto *pasteTransform = new QWidget(pasteTabs);
    auto *pasteTransformLayout = new QVBoxLayout(pasteTransform);
    auto *pasteTransformGroup = new QGroupBox(pasteTransform);
    auto *pasteTransformForm = new QFormLayout(pasteTransformGroup);
    pasteTransformForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    pasteTransformForm->setHorizontalSpacing(10);
    pasteTransformForm->setVerticalSpacing(7);
    pasteTransformLayout->addWidget(pasteTransformGroup);
    m_pasteTextToImageCheck = new QCheckBox(QStringLiteral("文字转图片"), pasteTransform);
    m_pasteTextToImageCheck->setChecked(m_config.value(QStringLiteral("paste/textToImage"), true).toBool());
    pasteTransformForm->addRow(m_pasteTextToImageCheck);
    m_pasteIgnoreHtmlCheck = new QCheckBox(QStringLiteral("总是忽略 HTML 格式"), pasteTransform);
    m_pasteIgnoreHtmlCheck->setChecked(m_config.value(QStringLiteral("paste/ignoreHtml"), false).toBool());
    pasteTransformForm->addRow(m_pasteIgnoreHtmlCheck);
    m_pastePathToImageCheck = new QCheckBox(QStringLiteral("不把文件路径转化成贴图"), pasteTransform);
    m_pastePathToImageCheck->setChecked(m_config.value(QStringLiteral("paste/pathToImage"), true).toBool());
    pasteTransformForm->addRow(m_pastePathToImageCheck);
    m_pasteTextMarginSpin = new QSpinBox(pasteTransform);
    m_pasteTextMarginSpin->setFixedWidth(92);
    m_pasteTextMarginSpin->setRange(0, 100);
    m_pasteTextMarginSpin->setSuffix(QStringLiteral(" 像素"));
    m_pasteTextMarginSpin->setValue(m_config.value(QStringLiteral("paste/textMargin"), 12).toInt());
    pasteTransformForm->addRow(QStringLiteral("页边距:"), m_pasteTextMarginSpin);
    m_pasteTextMaxWidthSpin = new QSpinBox(pasteTransform);
    m_pasteTextMaxWidthSpin->setFixedWidth(112);
    m_pasteTextMaxWidthSpin->setRange(100, 10000);
    m_pasteTextMaxWidthSpin->setSuffix(QStringLiteral(" 像素"));
    m_pasteTextMaxWidthSpin->setValue(m_config.value(QStringLiteral("paste/textMaxWidth"), 750).toInt());
    pasteTransformForm->addRow(QStringLiteral("图片最大宽度:"), m_pasteTextMaxWidthSpin);
    m_pasteTextFontEdit = new QLineEdit(m_config.value(QStringLiteral("paste/textFont"), QStringLiteral("Microsoft YaHei UI, 11")).toString(), pasteTransform);
    m_pasteTextFontEdit->setFixedWidth(190);
    m_pasteTextFontEdit->setReadOnly(true);
    pasteTransformForm->addRow(QStringLiteral("字体:"), m_pasteTextFontEdit);
    auto *textColorRow = new QWidget(pasteTransform);
    auto *textColorLayout = new QHBoxLayout(textColorRow);
    textColorLayout->setContentsMargins(0, 0, 0, 0);
    auto *backgroundButton = new QPushButton(QStringLiteral("背景"), textColorRow);
    auto *textButton = new QPushButton(QStringLiteral("■ 文本"), textColorRow);
    backgroundButton->setFixedWidth(90);
    textButton->setFixedWidth(90);
    backgroundButton->setObjectName(QStringLiteral("pasteTextBackgroundColor"));
    textButton->setObjectName(QStringLiteral("pasteTextColor"));
    const QColor textBackground = QColor::fromString(
        m_config.value(QStringLiteral("paste/textBackgroundColor"), QStringLiteral("#ffffffff")).toString());
    const QColor textForeground = QColor::fromString(
        m_config.value(QStringLiteral("paste/textColor"), QStringLiteral("#ff111111")).toString());
    backgroundButton->setProperty("color", textBackground.isValid() ? textBackground : QColor(Qt::white));
    textButton->setProperty("color", textForeground.isValid() ? textForeground : QColor(QStringLiteral("#111111")));
    backgroundButton->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #999999;")
                                        .arg(backgroundButton->property("color").value<QColor>().name(QColor::HexArgb)));
    textButton->setStyleSheet(QStringLiteral("background-color: %1; color: #ffffff; border: 1px solid #999999;")
                                  .arg(textButton->property("color").value<QColor>().name(QColor::HexArgb)));
    textColorLayout->addWidget(backgroundButton);
    textColorLayout->addWidget(textButton);
    textColorLayout->addStretch();
    pasteTransformForm->addRow(QStringLiteral("颜色:"), textColorRow);
    auto *preview = new QLabel(QStringLiteral("预览\nAaOo012"), pasteTransform);
    preview->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    preview->setMargin(10);
    QFont previewFont = preview->font();
    previewFont.setPointSize(14);
    preview->setFont(previewFont);
    preview->setFixedSize(110, 70);
    preview->setStyleSheet(QStringLiteral("border: 1px solid #333333; background: #ffffff; color: #111111;"));
    const auto updateTextPreview = [preview, backgroundButton, textButton] {
        const QColor background = backgroundButton->property("color").value<QColor>();
        const QColor text = textButton->property("color").value<QColor>();
        preview->setStyleSheet(QStringLiteral("border: 1px solid #333333; background: %1; color: %2;")
                                   .arg(background.name(QColor::HexArgb), text.name(QColor::HexArgb)));
    };
    const auto chooseTextColor = [this, updateTextPreview](QPushButton *button, const QString &title) {
        QObject::connect(button, &QPushButton::clicked, this, [button, title, updateTextPreview] {
            const QColor current = button->property("color").value<QColor>();
            const QColor selected = QColorDialog::getColor(current, button, title, QColorDialog::ShowAlphaChannel);
            if (!selected.isValid())
                return;
            button->setProperty("color", selected);
            button->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #999999;")
                                      .arg(selected.name(QColor::HexArgb)));
            updateTextPreview();
        });
    };
    chooseTextColor(backgroundButton, QStringLiteral("选择文字背景颜色"));
    chooseTextColor(textButton, QStringLiteral("选择文字颜色"));
    updateTextPreview();
    m_pageSavers.append([this, backgroundButton, textButton] {
        m_config.setValue(QStringLiteral("paste/textBackgroundColor"),
                          backgroundButton->property("color").value<QColor>().name(QColor::HexArgb));
        m_config.setValue(QStringLiteral("paste/textColor"),
                          textButton->property("color").value<QColor>().name(QColor::HexArgb));
    });
    auto *previewRow = new QHBoxLayout;
    previewRow->addStretch();
    previewRow->addWidget(preview);
    pasteTransformForm->addRow(previewRow);
    auto *pasteTransformFooter = new QHBoxLayout;
    pasteTransformFooter->addStretch();
    auto *pasteTransformRestore = new QPushButton(QStringLiteral("恢复默认"), pasteTransform);
    pasteTransformRestore->setFixedWidth(86);
    pasteTransformFooter->addWidget(pasteTransformRestore);
    pasteTransformLayout->addLayout(pasteTransformFooter);
    pasteTransformLayout->addStretch();
    QObject::connect(pasteTransformRestore, &QPushButton::clicked, this, [this, backgroundButton, textButton, updateTextPreview] {
        m_pasteTextToImageCheck->setChecked(true);
        m_pasteIgnoreHtmlCheck->setChecked(false);
        m_pastePathToImageCheck->setChecked(true);
        m_pasteTextMarginSpin->setValue(12);
        m_pasteTextMaxWidthSpin->setValue(750);
        m_pasteTextFontEdit->setText(QStringLiteral("Microsoft YaHei UI, 11"));
        backgroundButton->setProperty("color", QColor(QStringLiteral("#ffffff")));
        textButton->setProperty("color", QColor(QStringLiteral("#111111")));
        backgroundButton->setStyleSheet(QStringLiteral("background-color: #ffffff; border: 1px solid #999999;"));
        textButton->setStyleSheet(QStringLiteral("background-color: #111111; color: #ffffff; border: 1px solid #999999;"));
        updateTextPreview();
    });
    pasteTabs->addTab(pasteTransform, QStringLiteral("转换"));
    pasteLayout->addWidget(pasteTabs);
    tabs->addTab(pastePage, QStringLiteral("贴图"));

    auto *outputPage = new QWidget(tabs);
    auto *outputLayout = new QVBoxLayout(outputPage);
    auto *outputTabs = new QTabWidget(outputPage);
    outputTabs->setObjectName(QStringLiteral("outputTabs"));

    auto *fileOutput = new QWidget(outputTabs);
    auto *fileLayout = new QVBoxLayout(fileOutput);
    fileLayout->setContentsMargins(6, 6, 6, 6);

    const QString defaultFileName = defaultOutputFileNamePattern();
    const QString desktopDirectory = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    const QString picturesDirectory = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    m_quickSaveDirectory = m_config.value(QStringLiteral("output/saveDirectory"),
                                             desktopDirectory)
                                 .toString();
    if (m_quickSaveDirectory.trimmed().isEmpty())
        m_quickSaveDirectory = desktopDirectory;
    m_autoSaveDirectory = m_config.value(QStringLiteral("output/autoSaveDirectory"),
                                                picturesDirectory)
                                    .toString();
    if (m_autoSaveDirectory.trimmed().isEmpty())
        m_autoSaveDirectory = picturesDirectory;

    auto *qualityRow = new QWidget(fileOutput);
    auto *qualityLayout = new QHBoxLayout(qualityRow);
    qualityLayout->setContentsMargins(0, 0, 0, 0);
    qualityLayout->addWidget(new QLabel(QStringLiteral("图像质量:"), qualityRow));
    m_outputQualitySpin = new QSpinBox(qualityRow);
    m_outputQualitySpin->setObjectName(QStringLiteral("outputImageQuality"));
    m_outputQualitySpin->setFixedWidth(72);
    m_outputQualitySpin->setRange(-1, 100);
    m_outputQualitySpin->setValue(m_config.value(QStringLiteral("output/imageQuality"), -1).toInt());
    auto *qualityInfo = makeInfoButton(qualityRow);
    qualityLayout->addWidget(m_outputQualitySpin);
    qualityLayout->addWidget(qualityInfo);
    qualityLayout->addStretch();
    fileLayout->addWidget(qualityRow);

    auto *manualGroup = new QGroupBox(QStringLiteral("手动保存"), fileOutput);
    auto *manualForm = new QFormLayout(manualGroup);
    manualForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    manualForm->setHorizontalSpacing(10);
    manualForm->setVerticalSpacing(7);
    m_outputFileNameEdit = new QLineEdit(
        m_config.value(QStringLiteral("output/fileNamePattern"), defaultFileName).toString(), manualGroup);
    manualForm->addRow(QStringLiteral("文件名:"), m_outputFileNameEdit);
    auto *manualPreview = new QLineEdit(manualGroup);
    manualPreview->setReadOnly(true);
    manualPreview->setMinimumHeight(26);
    manualPreview->setStyleSheet(QStringLiteral("color: #666666; background: #ffffff; padding: 3px;"));
    manualForm->addRow(QStringLiteral("预览:"), manualPreview);
    m_outputRememberExtensionCheck = new QCheckBox(QStringLiteral("记住上一次使用的图片扩展名"), manualGroup);
    m_outputRememberExtensionCheck->setChecked(
        m_config.value(QStringLiteral("output/rememberExtension"), true).toBool());
    manualForm->addRow(m_outputRememberExtensionCheck);
    fileLayout->addWidget(manualGroup);

    auto *quickGroup = new QGroupBox(QStringLiteral("快捷保存"), fileOutput);
    auto *quickForm = new QFormLayout(quickGroup);
    quickForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    quickForm->setHorizontalSpacing(10);
    quickForm->setVerticalSpacing(7);
    m_outputQuickSaveNotificationCheck = new QCheckBox(QStringLiteral("显示通知"), quickGroup);
    m_outputQuickSaveNotificationCheck->setChecked(
        m_config.value(QStringLiteral("output/quickSaveNotification"), true).toBool());
    quickForm->addRow(m_outputQuickSaveNotificationCheck);
    auto *quickPathRow = new QWidget(quickGroup);
    auto *quickPathLayout = new QHBoxLayout(quickPathRow);
    quickPathLayout->setContentsMargins(0, 0, 0, 0);
    m_saveDirectoryEdit = new QLineEdit(quickPathRow);
    m_saveDirectoryEdit->setReadOnly(true);
    auto *quickOpenFolder = new QPushButton(QStringLiteral("打开文件夹"), quickPathRow);
    auto *quickChangeFolder = new QPushButton(QStringLiteral("更改文件夹"), quickPathRow);
    quickPathLayout->addWidget(m_saveDirectoryEdit, 1);
    quickPathLayout->addWidget(quickOpenFolder);
    quickPathLayout->addWidget(quickChangeFolder);
    quickForm->addRow(QStringLiteral("路径:"), quickPathRow);
    m_outputQuickSavePreviewEdit = new QLineEdit(quickGroup);
    m_outputQuickSavePreviewEdit->setReadOnly(true);
    quickForm->addRow(QStringLiteral("预览:"), m_outputQuickSavePreviewEdit);
    auto *quickInfoRow = new QHBoxLayout;
    quickInfoRow->addWidget(makeInfoButton(quickGroup));
    quickInfoRow->addStretch();
    quickForm->addRow(quickInfoRow);
    fileLayout->addWidget(quickGroup);

    auto *autoGroup = new QGroupBox(fileOutput);
    auto *autoLayout = new QVBoxLayout(autoGroup);
    autoLayout->setContentsMargins(8, 8, 8, 8);
    m_autoSaveCheck = new QCheckBox(QStringLiteral("自动保存"), autoGroup);
    m_autoSaveCheck->setChecked(m_config.value(QStringLiteral("capture/autoSave"), false).toBool());
    autoLayout->addWidget(m_autoSaveCheck);
    auto *autoPathRow = new QWidget(autoGroup);
    autoPathRow->setEnabled(m_autoSaveCheck->isChecked());
    auto *autoPathLayout = new QHBoxLayout(autoPathRow);
    autoPathLayout->setContentsMargins(0, 0, 0, 0);
    m_outputAutoSavePathEdit = new QLineEdit(autoPathRow);
    m_outputAutoSavePathEdit->setReadOnly(true);
    auto *autoOpenFolder = new QPushButton(QStringLiteral("打开文件夹"), autoPathRow);
    auto *autoChangeFolder = new QPushButton(QStringLiteral("更改文件夹"), autoPathRow);
    autoPathLayout->addWidget(m_outputAutoSavePathEdit, 1);
    autoPathLayout->addWidget(autoOpenFolder);
    autoPathLayout->addWidget(autoChangeFolder);
    autoLayout->addWidget(new QLabel(QStringLiteral("路径:"), autoGroup));
    autoLayout->addWidget(autoPathRow);
    auto *autoPreviewRow = new QWidget(autoGroup);
    autoPreviewRow->setEnabled(m_autoSaveCheck->isChecked());
    auto *autoPreviewLayout = new QHBoxLayout(autoPreviewRow);
    autoPreviewLayout->setContentsMargins(0, 0, 0, 0);
    autoPreviewLayout->addWidget(new QLabel(QStringLiteral("预览:"), autoPreviewRow));
    auto *autoPreview = new QLineEdit(autoPreviewRow);
    autoPreview->setReadOnly(true);
    autoPreviewLayout->addWidget(autoPreview, 1);
    autoLayout->addWidget(autoPreviewRow);
    autoLayout->addWidget(makeInfoButton(autoGroup), 0, Qt::AlignLeft);
    fileLayout->addWidget(autoGroup);

    auto *outputFileFooter = new QHBoxLayout;
    auto *namingRuleButton = new QPushButton(QStringLiteral("命名规则"), fileOutput);
    auto *fileRestoreButton = new QPushButton(QStringLiteral("恢复默认"), fileOutput);
    outputFileFooter->addWidget(namingRuleButton);
    outputFileFooter->addStretch();
    outputFileFooter->addWidget(fileRestoreButton);
    fileLayout->addLayout(outputFileFooter);

    const auto updateOutputPreviews = [this, manualPreview, autoPreview,
                                       autoPreviewRow] {
        const QString pattern = m_outputFileNameEdit->text().trimmed().isEmpty()
                                    ? defaultOutputFileNamePattern()
                                    : m_outputFileNameEdit->text().trimmed();
        const QDateTime now = QDateTime::currentDateTime();
        const QString previewName = expandOutputPattern(pattern, now);
        manualPreview->setText(previewName);
        m_saveDirectoryEdit->setText(outputPathForDirectory(m_quickSaveDirectory, pattern));
        m_saveDirectoryEdit->setCursorPosition(0);
        m_outputQuickSavePreviewEdit->setText(outputPathForDirectory(m_quickSaveDirectory, previewName));
        m_outputQuickSavePreviewEdit->setCursorPosition(0);
        m_outputAutoSavePathEdit->setText(outputPathForDirectory(m_autoSaveDirectory, pattern));
        m_outputAutoSavePathEdit->setCursorPosition(0);
        autoPreview->setText(outputPathForDirectory(m_autoSaveDirectory, previewName));
        autoPreview->setCursorPosition(0);
        autoPreviewRow->setEnabled(m_autoSaveCheck->isChecked());
    };
    updateOutputPreviews();
    QObject::connect(m_outputFileNameEdit, &QLineEdit::textChanged, this,
                     [updateOutputPreviews](const QString &) { updateOutputPreviews(); });
    QObject::connect(m_autoSaveCheck, &QCheckBox::toggled, this, [autoPathRow, autoPreviewRow](bool checked) {
        autoPathRow->setEnabled(checked);
        autoPreviewRow->setEnabled(checked);
    });
    QObject::connect(qualityInfo, &QPushButton::clicked, this, [this] {
        QMessageBox::information(this, QStringLiteral("图像质量"),
                                 QStringLiteral("-1 使用系统默认质量；0 到 100 可指定 JPEG 质量。"));
    });
    QObject::connect(namingRuleButton, &QPushButton::clicked, this, [this] {
        QMessageBox::information(this, QStringLiteral("命名规则"),
                                 QStringLiteral("文件名中的 $yyyy-MM-dd_HH-mm-ss$ 会替换为当前日期和时间。"));
    });
    QObject::connect(quickOpenFolder, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_quickSaveDirectory));
    });
    QObject::connect(quickChangeFolder, &QPushButton::clicked, this,
                     [this, updateOutputPreviews] {
                         const QString path = QFileDialog::getExistingDirectory(
                             this, QStringLiteral("选择快捷保存目录"), m_quickSaveDirectory);
                         if (!path.isEmpty()) {
                             m_quickSaveDirectory = path;
                             updateOutputPreviews();
                         }
                     });
    QObject::connect(autoOpenFolder, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_autoSaveDirectory));
    });
    QObject::connect(autoChangeFolder, &QPushButton::clicked, this,
                     [this, updateOutputPreviews] {
                         const QString path = QFileDialog::getExistingDirectory(
                             this, QStringLiteral("选择自动保存目录"), m_autoSaveDirectory);
                         if (!path.isEmpty()) {
                             m_autoSaveDirectory = path;
                             updateOutputPreviews();
                         }
                     });
    const auto restoreOutputDefaults = [this, desktopDirectory,
                                        picturesDirectory, updateOutputPreviews] {
        m_outputQualitySpin->setValue(-1);
        m_outputFileNameEdit->setText(defaultOutputFileNamePattern());
        m_outputRememberExtensionCheck->setChecked(true);
        m_outputQuickSaveNotificationCheck->setChecked(true);
        m_autoSaveCheck->setChecked(false);
        m_outputCopyWindowsBitmapCheck->setChecked(true);
        m_outputCopyScreenshotCheck->setChecked(true);
        m_outputCopyPasteCheck->setChecked(false);
        m_outputCopyAsFileShortcutRadio->setChecked(true);
        m_quickSaveDirectory = desktopDirectory;
        m_autoSaveDirectory = picturesDirectory;
        updateOutputPreviews();
    };
    QObject::connect(fileRestoreButton, &QPushButton::clicked, this, restoreOutputDefaults);

    outputTabs->addTab(fileOutput, QStringLiteral("文件"));

    auto *clipboardOutput = new QWidget(outputTabs);
    auto *clipboardLayout = new QVBoxLayout(clipboardOutput);
    clipboardLayout->setContentsMargins(8, 8, 8, 8);
    auto *bitmapRow = new QWidget(clipboardOutput);
    auto *bitmapLayout = new QHBoxLayout(bitmapRow);
    bitmapLayout->setContentsMargins(0, 0, 0, 0);
    m_outputCopyWindowsBitmapCheck = new QCheckBox(QStringLiteral("以 Windows Bitmap 格式复制图片:"), bitmapRow);
    m_outputCopyWindowsBitmapCheck->setChecked(
        m_config.value(QStringLiteral("output/clipboardWindowsBitmap"), true).toBool());
    m_outputCopyScreenshotCheck = new QCheckBox(QStringLiteral("截图"), bitmapRow);
    m_outputCopyScreenshotCheck->setChecked(
        m_config.value(QStringLiteral("output/clipboardScreenshot"), true).toBool());
    m_outputCopyPasteCheck = new QCheckBox(QStringLiteral("贴图"), bitmapRow);
    m_outputCopyPasteCheck->setChecked(
        m_config.value(QStringLiteral("output/clipboardPaste"), false).toBool());
    bitmapLayout->addWidget(m_outputCopyWindowsBitmapCheck);
    bitmapLayout->addWidget(m_outputCopyScreenshotCheck);
    bitmapLayout->addWidget(m_outputCopyPasteCheck);
    bitmapLayout->addWidget(makeInfoButton(bitmapRow));
    bitmapLayout->addStretch();
    clipboardLayout->addWidget(bitmapRow);
    auto *copyFileRow = new QWidget(clipboardOutput);
    auto *copyFileLayout = new QHBoxLayout(copyFileRow);
    copyFileLayout->setContentsMargins(0, 0, 0, 0);
    copyFileLayout->addWidget(new QLabel(QStringLiteral("复制图像为文件:"), copyFileRow));
    m_outputCopyAsFileAlwaysRadio = new QRadioButton(QStringLiteral("总是"), copyFileRow);
    m_outputCopyAsFileShortcutRadio = new QRadioButton(QStringLiteral("Ctrl + Alt + S"), copyFileRow);
    m_outputCopyAsFileDisabledRadio = new QRadioButton(QStringLiteral("禁用"), copyFileRow);
    copyFileLayout->addWidget(m_outputCopyAsFileAlwaysRadio);
    copyFileLayout->addWidget(m_outputCopyAsFileShortcutRadio);
    copyFileLayout->addWidget(m_outputCopyAsFileDisabledRadio);
    copyFileLayout->addStretch();
    const int copyAsFileMode = m_config.value(QStringLiteral("output/copyAsFileMode"), 1).toInt();
    (copyAsFileMode == 0 ? m_outputCopyAsFileAlwaysRadio
                          : copyAsFileMode == 1 ? m_outputCopyAsFileShortcutRadio
                                                : m_outputCopyAsFileDisabledRadio)
        ->setChecked(true);
    clipboardLayout->addWidget(copyFileRow);
    auto *clipboardFooter = new QHBoxLayout;
    clipboardFooter->addStretch();
    auto *clipboardRestore = new QPushButton(QStringLiteral("恢复默认"), clipboardOutput);
    clipboardFooter->addWidget(clipboardRestore);
    clipboardLayout->addLayout(clipboardFooter);
    QObject::connect(clipboardRestore, &QPushButton::clicked, this, restoreOutputDefaults);
    outputTabs->addTab(clipboardOutput, QStringLiteral("剪贴板"));

    auto *textRecognition = new QWidget(outputTabs);
    auto *textRecognitionLayout = new QVBoxLayout(textRecognition);
    textRecognitionLayout->setContentsMargins(8, 8, 8, 8);
    auto *ocrSoundRow = new QWidget(textRecognition);
    auto *ocrSoundLayout = new QHBoxLayout(ocrSoundRow);
    ocrSoundLayout->setContentsMargins(0, 0, 0, 0);
    auto *ocrSoundCheck = new QCheckBox(QStringLiteral("完成后播放音效"), ocrSoundRow);
    ocrSoundCheck->setObjectName(QStringLiteral("ocrSoundCheck"));
    ocrSoundCheck->setChecked(m_config.value(QStringLiteral("output/ocrSound"), true).toBool());
    auto *ocrPlay = new QToolButton(ocrSoundRow);
    ocrPlay->setIcon(textRecognition->style()->standardIcon(QStyle::SP_MediaPlay));
    ocrPlay->setFixedSize(28, 26);
    ocrSoundLayout->addWidget(ocrSoundCheck);
    ocrSoundLayout->addWidget(ocrPlay);
    ocrSoundLayout->addStretch();
    textRecognitionLayout->addWidget(ocrSoundRow);

    auto *ocrEngineRow = new QHBoxLayout;
    ocrEngineRow->addWidget(new QLabel(QStringLiteral("OCR 引擎:"), textRecognition));
    auto *ocrEngine = new QComboBox(textRecognition);
    ocrEngine->setObjectName(QStringLiteral("ocrEngine"));
    ocrEngine->setFixedWidth(120);
    ocrEngine->addItems({QStringLiteral("系统自带"), QStringLiteral("Tesseract")});
    ocrEngine->setCurrentText(m_config.value(QStringLiteral("output/ocrEngine"), QStringLiteral("系统自带")).toString());
    ocrEngineRow->addWidget(ocrEngine);
    ocrEngineRow->addStretch();
    textRecognitionLayout->addLayout(ocrEngineRow);

    auto *ocrLanguageRow = new QHBoxLayout;
    ocrLanguageRow->addWidget(new QLabel(QStringLiteral("首选语言:"), textRecognition));
    auto *ocrLanguage = new QComboBox(textRecognition);
    ocrLanguage->setObjectName(QStringLiteral("ocrLanguage"));
    ocrLanguage->setFixedWidth(180);
    ocrLanguage->addItems({QStringLiteral("系统默认"), QStringLiteral("简体中文"), QStringLiteral("English")});
    ocrLanguage->setCurrentText(m_config.value(QStringLiteral("output/ocrLanguage"), QStringLiteral("系统默认")).toString());
    ocrLanguageRow->addWidget(ocrLanguage);
    ocrLanguageRow->addStretch();
    textRecognitionLayout->addLayout(ocrLanguageRow);

    auto *ocrNote = new QLabel(QStringLiteral("* 需要 Windows 10 版本 1903 或更高版本"), textRecognition);
    ocrNote->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    textRecognitionLayout->addWidget(ocrNote);
    textRecognitionLayout->addStretch();
    m_pageSavers.append([this, ocrSoundCheck, ocrEngine, ocrLanguage] {
        m_config.setValue(QStringLiteral("output/ocrSound"), ocrSoundCheck->isChecked());
        m_config.setValue(QStringLiteral("output/ocrEngine"), ocrEngine->currentText());
        m_config.setValue(QStringLiteral("output/ocrLanguage"), ocrLanguage->currentText());
    });
    outputTabs->addTab(textRecognition, QStringLiteral("文字识别"));
    outputLayout->addWidget(outputTabs);
    tabs->addTab(outputPage, QStringLiteral("输出"));

    auto *controlPage = new QWidget(tabs);
    auto *controlLayout = new QVBoxLayout(controlPage);
    auto *controlTabs = new QTabWidget(controlPage);
    controlTabs->setObjectName(QStringLiteral("controlTabs"));

    auto *globalKeys = new QWidget(controlTabs);
    auto *globalKeysLayout = new QVBoxLayout(globalKeys);
    globalKeysLayout->setContentsMargins(8, 8, 8, 8);
    globalKeysLayout->setSpacing(2);
    m_snipHotkeyEdit = makeEditableHotkeyRow(
        QStringLiteral("截屏:"),
        QKeySequence(m_config.value(QStringLiteral("hotkeys/snip"), QStringLiteral("F1")).toString()),
        globalKeys);
    globalKeysLayout->addWidget(m_snipHotkeyEdit->parentWidget());
    m_copyHotkeyEdit = makeEditableHotkeyRow(
        QStringLiteral("截屏并自动复制:"),
        QKeySequence(m_config.value(QStringLiteral("hotkeys/copy"), QStringLiteral("Ctrl+F1")).toString()),
        globalKeys);
    globalKeysLayout->addWidget(m_copyHotkeyEdit->parentWidget());
    const auto addGlobalHotkey = [this, globalKeys, globalKeysLayout](const QString &label,
                                                                       const QString &key,
                                                                       const QString &fallback) {
        auto *edit = makeEditableHotkeyRow(label,
                                           QKeySequence(m_config.value(key, fallback).toString()),
                                           globalKeys);
        edit->setObjectName(key);
        edit->setProperty("defaultShortcut", fallback);
        globalKeysLayout->addWidget(edit->parentWidget());
        m_pageSavers.append([this, edit, key] {
            m_config.setValue(key, edit->keySequence().toString(QKeySequence::PortableText));
        });
    };
    addGlobalHotkey(QStringLiteral("自定义截屏:"), QStringLiteral("hotkeys/custom"), QStringLiteral("Shift+F1"));
    m_pasteHotkeyEdit = makeEditableHotkeyRow(
        QStringLiteral("贴图:"),
        QKeySequence(m_config.value(QStringLiteral("hotkeys/paste"), QStringLiteral("F3")).toString()),
        globalKeys);
    globalKeysLayout->addWidget(m_pasteHotkeyEdit->parentWidget());
    addGlobalHotkey(QStringLiteral("隐藏/显示所有贴图:"), QStringLiteral("hotkeys/toggleImages"), QStringLiteral("Shift+F3"));
    addGlobalHotkey(QStringLiteral("切换到另一贴图组:"), QStringLiteral("hotkeys/switchGroup"), QStringLiteral("Ctrl+F3"));
    addGlobalHotkey(QStringLiteral("开/关鼠标穿透:"), QStringLiteral("hotkeys/clickThrough"), QString());
    m_snipHotkeyEdit->setObjectName(QStringLiteral("hotkeys/snip"));
    m_copyHotkeyEdit->setObjectName(QStringLiteral("hotkeys/copy"));
    m_pasteHotkeyEdit->setObjectName(QStringLiteral("hotkeys/paste"));

    auto *addGlobalCommand = new QPushButton(QStringLiteral("＋ 添加新命令"), globalKeys);
    addGlobalCommand->setObjectName(QStringLiteral("addGlobalCommand"));
    addGlobalCommand->setToolTip(QStringLiteral("为任意命令绑定全局快捷键，保存后立即生效。"));
    addGlobalCommand->setFlat(true);
    addGlobalCommand->setStyleSheet(QStringLiteral("QPushButton { color: #3b6ea8; text-align: left; padding: 2px 0; }"));
    QObject::connect(addGlobalCommand, &QPushButton::clicked, this, [this] {
        QDialog editor(this);
        editor.setWindowTitle(QStringLiteral("编辑命令快捷键"));
        editor.setModal(true);
        editor.resize(520, 300);
        auto *layout = new QVBoxLayout(&editor);
        auto *help = new QLabel(QStringLiteral("选择命令并设置一个快捷键；命令也可以直接填写命令行别名。"), &editor);
        help->setWordWrap(true);
        layout->addWidget(help);
        auto *table = new QTableWidget(&editor);
        table->setColumnCount(3);
        table->setHorizontalHeaderLabels({QStringLiteral("命令"), QStringLiteral("快捷键"), QStringLiteral("操作")});
        table->setSelectionMode(QAbstractItemView::NoSelection);
        table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
        table->horizontalHeader()->setStretchLastSection(false);
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        const QStringList builtIns = {
            QStringLiteral("snip.start"), QStringLiteral("snip.copy"), QStringLiteral("snip.fullScreen"),
            QStringLiteral("snip.activeScreen"), QStringLiteral("snip.window"), QStringLiteral("snip.element"),
            QStringLiteral("snip.last"), QStringLiteral("snip.custom"), QStringLiteral("paste.clipboard"),
            QStringLiteral("paste.toggleAll"), QStringLiteral("paste.switchGroup"), QStringLiteral("paste.switchGroupPrev"),
            QStringLiteral("paste.createGroup"), QStringLiteral("paste.emptyGroup"), QStringLiteral("paste.solo"),
            QStringLiteral("paste.destroySelected"), QStringLiteral("paste.destroyAll"), QStringLiteral("history.open"),
            QStringLiteral("history.clear"), QStringLiteral("preferences.open"), QStringLiteral("hotkeys.toggle"),
            QStringLiteral("pickColor"), QStringLiteral("barcodeDecode"), QStringLiteral("app.about"), QStringLiteral("app.quit")
        };
        const auto addRow = [table, builtIns](const QString &command, const QString &shortcut) {
            const int row = table->rowCount();
            table->insertRow(row);
            auto *commandEdit = new QComboBox(table);
            commandEdit->setEditable(true);
            commandEdit->addItems(builtIns);
            commandEdit->setCurrentText(command);
            table->setCellWidget(row, 0, commandEdit);
            auto *keyEdit = new QKeySequenceEdit(QKeySequence(shortcut), table);
            keyEdit->setMaximumSequenceLength(1);
            table->setCellWidget(row, 1, keyEdit);
            auto *remove = new QPushButton(QStringLiteral("删除"), table);
            remove->setFixedWidth(54);
            table->setCellWidget(row, 2, remove);
            QObject::connect(remove, &QPushButton::clicked, table, [table, remove] {
                for (int index = 0; index < table->rowCount(); ++index) {
                    if (table->cellWidget(index, 2) == remove) { table->removeRow(index); break; }
                }
            });
        };
        QStringList bindings = m_config.value(QStringLiteral("hotkeys/commands")).toStringList();
        if (bindings.isEmpty())
            bindings = m_config.value(QStringLiteral("hotkeys/customCommands")).toStringList();
        for (const QString &binding : bindings) {
            const int separator = binding.indexOf(QChar('='));
            if (separator <= 0) continue;
            QString command = binding.left(separator).trimmed();
            QString shortcut = binding.mid(separator + 1).trimmed();
            const QKeySequence parsed(shortcut);
            if (parsed.isEmpty()) std::swap(command, shortcut);
            addRow(command, shortcut);
        }
        if (table->rowCount() == 0)
            addRow(QStringLiteral("snip.start"), QString());
        layout->addWidget(table, 1);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &editor);
        auto *add = buttons->addButton(QStringLiteral("添加"), QDialogButtonBox::ActionRole);
        QObject::connect(add, &QPushButton::clicked, &editor, [addRow] { addRow(QStringLiteral("snip.start"), QString()); });
        QObject::connect(buttons, &QDialogButtonBox::accepted, &editor, &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &editor, &QDialog::reject);
        layout->addWidget(buttons);
        if (editor.exec() != QDialog::Accepted)
            return;
        QStringList result;
        for (int row = 0; row < table->rowCount(); ++row) {
            auto *commandEdit = qobject_cast<QComboBox *>(table->cellWidget(row, 0));
            auto *keyEdit = qobject_cast<QKeySequenceEdit *>(table->cellWidget(row, 1));
            if (!commandEdit || !keyEdit || commandEdit->currentText().trimmed().isEmpty() || keyEdit->keySequence().isEmpty())
                continue;
            result.append(commandEdit->currentText().trimmed() + QStringLiteral("=")
                          + keyEdit->keySequence().toString(QKeySequence::PortableText));
        }
        m_config.setValue(QStringLiteral("hotkeys/commands"), result);
        m_config.setValue(QStringLiteral("hotkeys/customCommands"), result);
    });
    globalKeysLayout->addWidget(addGlobalCommand, 0, Qt::AlignLeft);

    auto *superGroup = new QGroupBox(QStringLiteral("超级截屏"), globalKeys);
    superGroup->setToolTip(QStringLiteral("Win+??????????????????"));
    auto *superLayout = new QGridLayout(superGroup);
    superLayout->setContentsMargins(8, 5, 8, 6);
    superLayout->setHorizontalSpacing(4);
    superLayout->setVerticalSpacing(4);
    auto *superModifier = new QComboBox(superGroup);
    superModifier->addItems({QStringLiteral("无"), QStringLiteral("Ctrl"), QStringLiteral("Shift"), QStringLiteral("Alt")});
    superModifier->setCurrentText(m_config.value(QStringLiteral("control/superCaptureModifier"), QStringLiteral("无")).toString());
    auto *superMouse = new QComboBox(superGroup);
    superMouse->addItems({QStringLiteral("鼠标左键拖拽"), QStringLiteral("鼠标右键拖拽")});
    superMouse->setCurrentText(m_config.value(QStringLiteral("control/superCaptureMouse"), QStringLiteral("鼠标左键拖拽")).toString());
    superLayout->addWidget(new QLabel(QStringLiteral("Win"), superGroup), 0, 0);
    superLayout->addWidget(new QLabel(QStringLiteral("+"), superGroup), 0, 1);
    superLayout->addWidget(superModifier, 0, 2);
    superLayout->addWidget(new QLabel(QStringLiteral("+"), superGroup), 0, 3);
    superLayout->addWidget(superMouse, 0, 4);
    superLayout->addWidget(new QLabel(QStringLiteral("→ 截屏并贴图"), superGroup), 0, 5);
    superLayout->setColumnStretch(4, 1);
    globalKeysLayout->addWidget(superGroup);

    auto *globalRestore = new QPushButton(QStringLiteral("恢复默认"), globalKeys);
    globalRestore->setFixedWidth(86);
    globalRestore->setObjectName(QStringLiteral("controlGlobalRestore"));
    globalKeysLayout->addWidget(globalRestore, 0, Qt::AlignRight);
    m_ignoreApplicationsEdit = new QLineEdit(globalKeys);
    m_ignoreApplicationsEdit->setText(
        m_config.value(QStringLiteral("hotkeys/ignoreApplications")).toStringList().join(QStringLiteral(", ")));
    m_ignoreApplicationsEdit->hide();
    QObject::connect(globalRestore, &QPushButton::clicked, this, [this, superModifier, superMouse, globalKeys] {
        for (auto *edit : globalKeys->findChildren<QKeySequenceEdit *>())
            if (edit->property("defaultShortcut").isValid()) edit->setKeySequence(QKeySequence(edit->property("defaultShortcut").toString()));
        m_snipHotkeyEdit->setKeySequence(QKeySequence(QStringLiteral("F1")));
        m_copyHotkeyEdit->setKeySequence(QKeySequence(QStringLiteral("Ctrl+F1")));
        m_pasteHotkeyEdit->setKeySequence(QKeySequence(QStringLiteral("F3")));
        superModifier->setCurrentText(QStringLiteral("无"));
        superMouse->setCurrentText(QStringLiteral("鼠标左键拖拽"));
    });
    m_pageSavers.append([this, superModifier, superMouse] {
        m_config.setValue(QStringLiteral("control/superCaptureModifier"), superModifier->currentText());
        m_config.setValue(QStringLiteral("control/superCaptureMouse"), superMouse->currentText());
    });
    controlTabs->addTab(globalKeys, QStringLiteral("全局快捷键"));

    auto *applicationKeys = new QWidget(controlTabs);
    auto *applicationLayout = new QVBoxLayout(applicationKeys);
    applicationLayout->setContentsMargins(10, 8, 10, 8);
    applicationLayout->setSpacing(5);
    auto *printHotkey = makeEditableHotkeyRow(QStringLiteral("打印:"), QKeySequence(QStringLiteral("Ctrl+P")), applicationKeys);
    applicationLayout->addWidget(printHotkey->parentWidget());
    auto *addApplicationCommand = new QPushButton(QStringLiteral("＋ 添加新命令"), applicationKeys);
    addApplicationCommand->setToolTip(QStringLiteral("打开命令快捷键编辑器；应用内命令和全局命令使用同一套配置。"));
    addApplicationCommand->setFlat(true);
    addApplicationCommand->setStyleSheet(QStringLiteral("QPushButton { color: #3b6ea8; text-align: left; padding: 2px 0; }"));
    QObject::connect(addApplicationCommand, &QPushButton::clicked, addGlobalCommand,
                     &QPushButton::click);
    applicationLayout->addWidget(addApplicationCommand, 0, Qt::AlignLeft);
    auto *internalKeysButton = new QPushButton(QStringLiteral("内部快捷键"), applicationKeys);
    internalKeysButton->setFixedWidth(92);
    internalKeysButton->setToolTip(QStringLiteral("截图和贴图窗口中的快捷键"));
    QObject::connect(internalKeysButton, &QPushButton::clicked, this, [this] {
        QMessageBox::information(this, QStringLiteral("内部快捷键"),
                                 QStringLiteral("截图：Enter 复制，Ctrl+T 贴图，Ctrl+S 保存，Ctrl+Z/Ctrl+Y 撤销/重做；\n"
                                                "贴图：1/2 旋转，3/4 镜像，5 灰度，6 反色，Esc 关闭，Shift+Esc 销毁。"));
    });
    applicationLayout->addWidget(internalKeysButton, 0, Qt::AlignLeft);
    applicationLayout->addStretch();
    auto *applicationRestore = new QPushButton(QStringLiteral("恢复默认"), applicationKeys);
    applicationRestore->setFixedWidth(86);
    QObject::connect(applicationRestore, &QPushButton::clicked, printHotkey, [printHotkey] {
        printHotkey->setKeySequence(QKeySequence(QStringLiteral("Ctrl+P")));
    });
    applicationLayout->addWidget(applicationRestore, 0, Qt::AlignRight);
    controlTabs->addTab(applicationKeys, QStringLiteral("应用内快捷键"));

    auto *mouseKeys = new QWidget(controlTabs);
    auto *mouseLayout = new QVBoxLayout(mouseKeys);
    mouseLayout->setContentsMargins(8, 8, 8, 8);
    auto *pasteMouseGroup = new QGroupBox(QStringLiteral("贴图操作"), mouseKeys);
    auto *mouseGrid = new QGridLayout(pasteMouseGroup);
    mouseGrid->setContentsMargins(8, 8, 8, 8);
    mouseGrid->setHorizontalSpacing(6);
    mouseGrid->setVerticalSpacing(6);

    const auto addMouseRow = [this, pasteMouseGroup, mouseGrid](int row,
                                                           const QString &label,
                                                           const QString &first,
                                                           const QString &second,
                                                           const QString &third,
                                                           const QString &fourth,
                                                           bool showInfo = false) {
        auto *labelWidget = new QLabel(label, pasteMouseGroup);
        auto *firstBox = new QComboBox(pasteMouseGroup);
        auto *secondBox = new QComboBox(pasteMouseGroup);
        firstBox->addItems({QStringLiteral("无"), QStringLiteral("Ctrl"), QStringLiteral("Shift"), QStringLiteral("Alt")});
        secondBox->addItems({QStringLiteral("无"), QStringLiteral("左键双击"), QStringLiteral("中键单击"),
                             QStringLiteral("右键单击"), QStringLiteral("鼠标右键拖拽")});
        firstBox->setProperty("defaultText", first);
        secondBox->setProperty("defaultText", second);
        firstBox->setCurrentText(first);
        secondBox->setCurrentText(second);
        const auto registerMouseCombo = [this](QComboBox *box) {
            const int index = m_controlMouseCombos.size();
            m_controlMouseCombos.append(box);
            const QString saved = m_config.value(QStringLiteral("control/mouseAction%1").arg(index)).toString();
            if (!saved.isEmpty()) box->setCurrentText(saved);
        };
        registerMouseCombo(firstBox);
        registerMouseCombo(secondBox);
        firstBox->setFixedWidth(64);
        secondBox->setFixedWidth(84);
        mouseGrid->addWidget(labelWidget, row, 0);
        mouseGrid->addWidget(firstBox, row, 1);
        mouseGrid->addWidget(new QLabel(QStringLiteral("+"), pasteMouseGroup), row, 2);
        mouseGrid->addWidget(secondBox, row, 3);
        if (!third.isEmpty()) {
            auto *thirdBox = new QComboBox(pasteMouseGroup);
            auto *fourthBox = new QComboBox(pasteMouseGroup);
            thirdBox->addItems({QStringLiteral("鼠标滚轮"), QStringLiteral("无")});
            fourthBox->addItems({QStringLiteral("+/-"), QStringLiteral("无")});
            thirdBox->setProperty("defaultText", third);
            fourthBox->setProperty("defaultText", fourth);
            thirdBox->setCurrentText(third);
            fourthBox->setCurrentText(fourth);
            registerMouseCombo(thirdBox);
            registerMouseCombo(fourthBox);
            thirdBox->setFixedWidth(92);
            fourthBox->setFixedWidth(58);
            mouseGrid->addWidget(new QLabel(QStringLiteral("+"), pasteMouseGroup), row, 4);
            mouseGrid->addWidget(thirdBox, row, 5);
            mouseGrid->addWidget(new QLabel(QStringLiteral("或"), pasteMouseGroup), row, 6);
            mouseGrid->addWidget(fourthBox, row, 7);
        }
        if (showInfo) mouseGrid->addWidget(makeInfoButton(pasteMouseGroup), row, 8);
    };
    addMouseRow(0, QStringLiteral("贴图缩放:"), QStringLiteral("无"), QStringLiteral("无"), QStringLiteral("鼠标滚轮"), QStringLiteral("+/-"));
    addMouseRow(1, QStringLiteral("调整透明度:"), QStringLiteral("Ctrl"), QStringLiteral("无"), QStringLiteral("鼠标滚轮"), QStringLiteral("+/-"));
    addMouseRow(2, QStringLiteral("关闭贴图:"), QStringLiteral("无"), QStringLiteral("左键双击"), QString(), QString());
    addMouseRow(3, QStringLiteral("重置贴图:"), QStringLiteral("无"), QStringLiteral("中键单击"), QString(), QString());
    addMouseRow(4, QStringLiteral("快速缩略图:"), QStringLiteral("Shift"), QStringLiteral("左键双击"), QString(), QString());
    addMouseRow(5, QStringLiteral("选区缩略图:"), QStringLiteral("无"), QStringLiteral("鼠标右键拖拽"), QString(), QString());
    addMouseRow(6, QStringLiteral("复制文本:"), QStringLiteral("Shift"), QStringLiteral("右键单击"), QString(), QString(), true);
    mouseGrid->setColumnStretch(3, 1);
    pasteMouseGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    mouseLayout->addWidget(pasteMouseGroup);

    auto *trayGroup = new QGroupBox(QStringLiteral("托盘图标"), mouseKeys);
    auto *trayGrid = new QGridLayout(trayGroup);
    trayGrid->setContentsMargins(8, 6, 8, 6);
    trayGrid->setHorizontalSpacing(6);
    trayGrid->setVerticalSpacing(5);
    const QStringList trayLabels = {QStringLiteral("左键点击:"), QStringLiteral("中键点击:"), QStringLiteral("右键点击:")};
    const QStringList trayKeys = {QStringLiteral("control/trayLeft"), QStringLiteral("control/trayMiddle"), QStringLiteral("control/trayRight")};
    const QStringList trayDefaults = {QStringLiteral("snip"), QStringLiteral("paste"), QStringLiteral("show-tray-menu")};
    for (int row = 0; row < trayLabels.size(); ++row) {
        trayGrid->addWidget(new QLabel(trayLabels.at(row), trayGroup), row, 0);
        auto *command = new QLineEdit(trayGroup);
        command->setText(m_config.value(trayKeys.at(row), trayDefaults.at(row)).toString());
        command->setObjectName(QStringLiteral("trayCommand%1").arg(row));
        command->setMinimumWidth(154);
        auto *preset = makeCommandPreset(command, trayGroup);
        preset->setObjectName(QStringLiteral("trayPreset%1").arg(row));
        trayGrid->addWidget(command, row, 1);
        trayGrid->addWidget(preset, row, 2);
        m_controlTrayCommands.append(command);
    }
    mouseLayout->addWidget(trayGroup);
    mouseLayout->addWidget(makeHint(QStringLiteral("* 如有冲突，由上至下优先匹配"), mouseKeys), 0, Qt::AlignRight);
    auto *mouseRestore = new QPushButton(QStringLiteral("恢复默认"), mouseKeys);
    mouseRestore->setFixedWidth(86);
    mouseRestore->setObjectName(QStringLiteral("controlMouseRestore"));
    mouseLayout->addWidget(mouseRestore, 0, Qt::AlignRight);
    QObject::connect(mouseRestore, &QPushButton::clicked, this, [this] {
        for (QComboBox *box : m_controlMouseCombos)
            box->setCurrentText(box->property("defaultText").toString());
        const QStringList defaults = {QStringLiteral("snip"), QStringLiteral("paste"), QStringLiteral("show-tray-menu")};
        for (int i = 0; i < m_controlTrayCommands.size() && i < defaults.size(); ++i)
            m_controlTrayCommands.at(i)->setText(defaults.at(i));
    });
    m_pageSavers.append([this] {
        const QStringList keys = {QStringLiteral("control/trayLeft"), QStringLiteral("control/trayMiddle"), QStringLiteral("control/trayRight")};
        for (int index = 0; index < m_controlTrayCommands.size() && index < keys.size(); ++index)
            m_config.setValue(keys.at(index), m_controlTrayCommands.at(index)->text().trimmed());
    });
    controlTabs->addTab(mouseKeys, QStringLiteral("鼠标"));

    auto *corners = new QWidget(controlTabs);
    auto *cornerLayout = new QVBoxLayout(corners);
    cornerLayout->setContentsMargins(8, 8, 8, 8);
    cornerLayout->setSpacing(5);
    auto *cornerEnabled = new QCheckBox(QStringLiteral("启用"), corners);
    cornerEnabled->setObjectName(QStringLiteral("controlCornerEnabled"));
    cornerEnabled->setChecked(m_config.value(QStringLiteral("control/cornerEnabled"), false).toBool());
    cornerLayout->addWidget(cornerEnabled);
    auto *cornerOptions = new QFormLayout;
    cornerOptions->setContentsMargins(0, 0, 0, 0);
    cornerOptions->setHorizontalSpacing(12);
    auto *cornerSize = new QSpinBox(corners);
    cornerSize->setObjectName(QStringLiteral("controlCornerSize"));
    cornerSize->setRange(1, 200);
    cornerSize->setSuffix(QStringLiteral(" 像素"));
    cornerSize->setValue(m_config.value(QStringLiteral("control/cornerSize"), 10).toInt());
    auto *cornerDelay = new QSpinBox(corners);
    cornerDelay->setObjectName(QStringLiteral("controlCornerDelay"));
    cornerDelay->setRange(0, 5000);
    cornerDelay->setSuffix(QStringLiteral(" 毫秒"));
    cornerDelay->setValue(m_config.value(QStringLiteral("control/cornerDelay"), 300).toInt());
    auto *cornerInterval = new QSpinBox(corners);
    cornerInterval->setObjectName(QStringLiteral("controlCornerInterval"));
    cornerInterval->setRange(10, 2000);
    cornerInterval->setSuffix(QStringLiteral(" 毫秒"));
    cornerInterval->setValue(m_config.value(QStringLiteral("control/cornerInterval"), 125).toInt());
    cornerSize->setFixedWidth(112);
    cornerDelay->setFixedWidth(112);
    cornerInterval->setFixedWidth(112);
    cornerOptions->addRow(QStringLiteral("区域大小:"), cornerSize);
    cornerOptions->addRow(QStringLiteral("触发延迟:"), cornerDelay);
    cornerOptions->addRow(QStringLiteral("检查间隔:"), cornerInterval);
    cornerLayout->addLayout(cornerOptions);
    auto *ignoredCornerRow = new QHBoxLayout;
    auto *ignoredCorner = new QLineEdit(corners);
    ignoredCorner->setObjectName(QStringLiteral("controlCornerIgnoredApplications"));
    ignoredCorner->setPlaceholderText(QStringLiteral("例如：explorer.exe, devenv.exe"));
    ignoredCorner->setText(m_config.value(QStringLiteral("control/cornerIgnoredApplications")).toStringList().join(QStringLiteral(", ")));
    ignoredCornerRow->addWidget(new QLabel(QStringLiteral("忽略程序:"), corners));
    ignoredCornerRow->addWidget(ignoredCorner, 1);
    auto *cornerInfo = makeInfoButton(corners);
    const QString ignoreHelp = QStringLiteral("多个程序名用逗号或分号分隔，不区分大小写。当前前台程序名包含其中任意一项时，不触发屏幕角命令。");
    cornerInfo->setToolTip(ignoreHelp);
    QObject::connect(cornerInfo, &QPushButton::clicked, this, [this, ignoreHelp] {
        QMessageBox::information(this, QStringLiteral("忽略程序列表"), ignoreHelp);
    });
    ignoredCornerRow->addWidget(cornerInfo);
    cornerLayout->addLayout(ignoredCornerRow);

    auto *cornerFunctions = new QGroupBox(QStringLiteral("功能"), corners);
    auto *cornerGrid = new QGridLayout(cornerFunctions);
    cornerGrid->setContentsMargins(8, 6, 8, 6);
    cornerGrid->setHorizontalSpacing(6);
    cornerGrid->setVerticalSpacing(5);
    const QStringList cornerNames = {QStringLiteral("左上角:"), QStringLiteral("右上角:"), QStringLiteral("左下角:"), QStringLiteral("右下角:")};
    const QStringList cornerKeys = {QStringLiteral("control/cornerTopLeft"), QStringLiteral("control/cornerTopRight"), QStringLiteral("control/cornerBottomLeft"), QStringLiteral("control/cornerBottomRight")};
    const QStringList cornerDefaults = {QStringLiteral("toggle-images"), QStringLiteral("toggle-images"), QString(), QString()};
    for (int row = 0; row < cornerNames.size(); ++row) {
        cornerGrid->addWidget(new QLabel(cornerNames.at(row), cornerFunctions), row, 0);
        auto *command = new QLineEdit(cornerFunctions);
        command->setText(m_config.value(cornerKeys.at(row), cornerDefaults.at(row)).toString());
        command->setObjectName(QStringLiteral("controlCorner%1").arg(row));
        command->setMinimumWidth(150);
        auto *preset = makeCommandPreset(command, cornerFunctions);
        preset->setObjectName(QStringLiteral("cornerPreset%1").arg(row));
        cornerGrid->addWidget(command, row, 1);
        cornerGrid->addWidget(preset, row, 2);
        m_controlCornerCommands.append(command);
    }
    cornerLayout->addWidget(cornerFunctions);
    cornerLayout->addStretch();
    auto *cornerRestore = new QPushButton(QStringLiteral("恢复默认"), corners);
    cornerRestore->setFixedWidth(86);
    cornerRestore->setObjectName(QStringLiteral("controlCornerRestore"));
    cornerLayout->addWidget(cornerRestore, 0, Qt::AlignRight);
    QObject::connect(cornerRestore, &QPushButton::clicked, this, [cornerEnabled, cornerSize, cornerDelay, cornerInterval, ignoredCorner, this] {
        cornerEnabled->setChecked(false);
        cornerSize->setValue(10);
        cornerDelay->setValue(300);
        cornerInterval->setValue(125);
        ignoredCorner->clear();
        const QStringList defaults = {QStringLiteral("toggle-images"), QStringLiteral("toggle-images"), QString(), QString()};
        for (int i = 0; i < m_controlCornerCommands.size() && i < defaults.size(); ++i)
            m_controlCornerCommands.at(i)->setText(defaults.at(i));
    });
    m_pageSavers.append([this, cornerEnabled, cornerSize, cornerDelay, cornerInterval, ignoredCorner] {
        m_config.setValue(QStringLiteral("control/cornerEnabled"), cornerEnabled->isChecked());
        m_config.setValue(QStringLiteral("control/cornerSize"), cornerSize->value());
        m_config.setValue(QStringLiteral("control/cornerDelay"), cornerDelay->value());
        m_config.setValue(QStringLiteral("control/cornerInterval"), cornerInterval->value());
        QStringList ignored;
        for (const QString &entry : ignoredCorner->text().split(QRegularExpression(QStringLiteral("[,;\\n]")), Qt::SkipEmptyParts))
            if (!entry.trimmed().isEmpty()) ignored.append(entry.trimmed());
        m_config.setValue(QStringLiteral("control/cornerIgnoredApplications"), ignored);
        const QStringList keys = {QStringLiteral("control/cornerTopLeft"), QStringLiteral("control/cornerTopRight"), QStringLiteral("control/cornerBottomLeft"), QStringLiteral("control/cornerBottomRight")};
        for (int i = 0; i < m_controlCornerCommands.size() && i < keys.size(); ++i)
            m_config.setValue(keys.at(i), m_controlCornerCommands.at(i)->text().trimmed());
    });
    controlTabs->addTab(corners, QStringLiteral("触发角"));
    controlLayout->addWidget(controlTabs);
    tabs->addTab(controlPage, QStringLiteral("控制"));

    // The update page follows the compact professional edition layout.  Keep
    // the old general/* keys as compatibility aliases while storing the
    // complete settings under update/* for future updater integration.
    auto *updatePage = new QWidget(tabs);
    updatePage->setProperty("referenceHeight", 326);
    auto *updateLayout = new QVBoxLayout(updatePage);
    updateLayout->setContentsMargins(10, 8, 10, 8);
    updateLayout->setSpacing(5);

    auto *includeBetaRow = new QHBoxLayout;
    auto *includeBeta = new QCheckBox(QStringLiteral("包括 beta 版"), updatePage);
    includeBeta->setObjectName(QStringLiteral("updateIncludeBeta"));
    includeBeta->setChecked(m_config.value(QStringLiteral("update/includeBeta"), false).toBool());
    includeBetaRow->addWidget(includeBeta);
    auto *betaInfo = makeInfoButton(updatePage);
    betaInfo->setObjectName(QStringLiteral("updateBetaInfo"));
    betaInfo->setToolTip(QStringLiteral("包括尚未正式发布的测试版本。"));
    QObject::connect(betaInfo, &QPushButton::clicked, this, [this] {
        QMessageBox::information(this, QStringLiteral("测试版本"),
                                 QStringLiteral("启用后，检查更新时也会列出 beta 测试版本。"));
    });
    includeBetaRow->addWidget(betaInfo);
    includeBetaRow->addStretch();
    updateLayout->addLayout(includeBetaRow);

    auto *checkAtStartup = new QCheckBox(QStringLiteral("启动时检查更新"), updatePage);
    checkAtStartup->setObjectName(QStringLiteral("updateCheckAtStartup"));
    checkAtStartup->setChecked(m_config.value(QStringLiteral("update/checkAtStartup"),
                                              m_config.value(QStringLiteral("general/autoUpdate"), true)).toBool());
    updateLayout->addWidget(checkAtStartup);

    auto *periodicGroup = new QGroupBox(QStringLiteral("定期检查"), updatePage);
    periodicGroup->setObjectName(QStringLiteral("updatePeriodicGroup"));
    auto *periodicLayout = new QHBoxLayout(periodicGroup);
    periodicLayout->setContentsMargins(8, 5, 8, 5);
    auto *periodicCheck = new QCheckBox(QStringLiteral("每隔"), periodicGroup);
    periodicCheck->setObjectName(QStringLiteral("updatePeriodicCheck"));
    periodicCheck->setChecked(m_config.value(QStringLiteral("update/checkIntervalEnabled"), true).toBool());
    periodicLayout->addWidget(periodicCheck);
    auto *periodicHours = new QSpinBox(periodicGroup);
    periodicHours->setObjectName(QStringLiteral("updateCheckIntervalHours"));
    periodicHours->setRange(1, 720);
    periodicHours->setFixedWidth(70);
    periodicHours->setValue(m_config.value(QStringLiteral("update/checkIntervalHours"), 24).toInt());
    periodicLayout->addWidget(periodicHours);
    periodicLayout->addWidget(new QLabel(QStringLiteral("小时检查"), periodicGroup));
    periodicLayout->addStretch();
    updateLayout->addWidget(periodicGroup);

    auto *proxyGroup = new QGroupBox(QStringLiteral("代理"), updatePage);
    proxyGroup->setObjectName(QStringLiteral("updateProxyGroup"));
    auto *proxyForm = new QFormLayout(proxyGroup);
    proxyForm->setContentsMargins(8, 5, 8, 5);
    proxyForm->setHorizontalSpacing(8);
    proxyForm->setVerticalSpacing(4);
    auto *proxyType = new QComboBox(proxyGroup);
    proxyType->setObjectName(QStringLiteral("updateProxyType"));
    proxyType->addItems({QStringLiteral("系统代理"), QStringLiteral("不使用代理"),
                         QStringLiteral("HTTP 代理"), QStringLiteral("SOCKS5 代理")});
    proxyType->setCurrentText(m_config.value(QStringLiteral("update/proxyType"), QStringLiteral("系统代理")).toString());
    proxyForm->addRow(QStringLiteral("代理类型:"), proxyType);

    auto *proxyServerRow = new QWidget(proxyGroup);
    auto *proxyServerLayout = new QHBoxLayout(proxyServerRow);
    proxyServerLayout->setContentsMargins(0, 0, 0, 0);
    auto *proxyServer = new QLineEdit(proxyServerRow);
    proxyServer->setObjectName(QStringLiteral("updateProxyServer"));
    proxyServer->setPlaceholderText(QStringLiteral("服务器地址"));
    proxyServer->setText(m_config.value(QStringLiteral("update/proxyServer")).toString());
    proxyServerLayout->addWidget(proxyServer, 1);
    auto *proxyPort = new QSpinBox(proxyServerRow);
    proxyPort->setObjectName(QStringLiteral("updateProxyPort"));
    proxyPort->setRange(0, 65535);
    proxyPort->setSpecialValueText(QStringLiteral("0"));
    proxyPort->setFixedWidth(68);
    proxyPort->setValue(m_config.value(QStringLiteral("update/proxyPort"), 0).toInt());
    proxyServerLayout->addWidget(proxyPort);
    proxyForm->addRow(QStringLiteral("服务器:"), proxyServerRow);

    auto *proxyAuth = new QCheckBox(QStringLiteral("代理服务器需要密码"), proxyGroup);
    proxyAuth->setObjectName(QStringLiteral("updateProxyRequiresAuth"));
    proxyAuth->setChecked(m_config.value(QStringLiteral("update/proxyRequiresAuth"), false).toBool());
    proxyForm->addRow(proxyAuth);
    auto *proxyUsername = new QLineEdit(proxyGroup);
    proxyUsername->setObjectName(QStringLiteral("updateProxyUsername"));
    proxyUsername->setText(m_config.value(QStringLiteral("update/proxyUsername")).toString());
    proxyForm->addRow(QStringLiteral("用户名:"), proxyUsername);
    auto *proxyPassword = new QLineEdit(proxyGroup);
    proxyPassword->setObjectName(QStringLiteral("updateProxyPassword"));
    proxyPassword->setEchoMode(QLineEdit::Password);
    proxyPassword->setText(m_config.value(QStringLiteral("update/proxyPassword")).toString());
    proxyForm->addRow(QStringLiteral("密码:"), proxyPassword);

    const auto refreshProxyState = [proxyType, proxyServer, proxyPort, proxyAuth, proxyUsername, proxyPassword] {
        const bool customProxy = proxyType->currentIndex() >= 2;
        proxyServer->setEnabled(customProxy);
        proxyPort->setEnabled(customProxy);
        proxyAuth->setEnabled(customProxy);
        const bool auth = customProxy && proxyAuth->isChecked();
        proxyUsername->setEnabled(auth);
        proxyPassword->setEnabled(auth);
    };
    QObject::connect(proxyType, &QComboBox::currentTextChanged, this, [refreshProxyState](const QString &) {
        refreshProxyState();
    });
    QObject::connect(proxyAuth, &QCheckBox::toggled, this, [refreshProxyState](bool) {
        refreshProxyState();
    });
    refreshProxyState();
    updateLayout->addWidget(proxyGroup);

    auto *updateButtons = new QHBoxLayout;
    auto *checkNow = new QPushButton(QStringLiteral("检查更新"), updatePage);
    checkNow->setObjectName(QStringLiteral("updateCheckNow"));
    auto *updateRestore = new QPushButton(QStringLiteral("恢复默认"), updatePage);
    updateRestore->setObjectName(QStringLiteral("updateRestore"));
    updateButtons->addWidget(checkNow);
    updateButtons->addStretch();
    updateButtons->addWidget(updateRestore);
    updateLayout->addLayout(updateButtons);
    QObject::connect(checkNow, &QPushButton::clicked, this, [this] {
        QMessageBox::information(this, QStringLiteral("检查更新"),
                                 QStringLiteral("当前已是最新版本。"));
    });
    QObject::connect(updateRestore, &QPushButton::clicked, this,
                     [includeBeta, checkAtStartup, periodicCheck, periodicHours, proxyType,
                      proxyServer, proxyPort, proxyAuth, proxyUsername, proxyPassword, refreshProxyState] {
        includeBeta->setChecked(false);
        checkAtStartup->setChecked(true);
        periodicCheck->setChecked(true);
        periodicHours->setValue(24);
        proxyType->setCurrentText(QStringLiteral("系统代理"));
        proxyServer->clear();
        proxyPort->setValue(0);
        proxyAuth->setChecked(false);
        proxyUsername->clear();
        proxyPassword->clear();
        refreshProxyState();
    });
    tabs->addTab(updatePage, QStringLiteral("更新"));
    m_pageSavers.append([this, includeBeta, checkAtStartup, periodicCheck, periodicHours,
                         proxyType, proxyServer, proxyPort, proxyAuth, proxyUsername, proxyPassword] {
        m_config.setValue(QStringLiteral("update/includeBeta"), includeBeta->isChecked());
        m_config.setValue(QStringLiteral("update/checkAtStartup"), checkAtStartup->isChecked());
        m_config.setValue(QStringLiteral("update/checkIntervalEnabled"), periodicCheck->isChecked());
        m_config.setValue(QStringLiteral("update/checkIntervalHours"), periodicHours->value());
        m_config.setValue(QStringLiteral("update/proxyType"), proxyType->currentText());
        m_config.setValue(QStringLiteral("update/proxyServer"), proxyServer->text());
        m_config.setValue(QStringLiteral("update/proxyPort"), proxyPort->value());
        m_config.setValue(QStringLiteral("update/proxyRequiresAuth"), proxyAuth->isChecked());
        m_config.setValue(QStringLiteral("update/proxyUsername"), proxyUsername->text());
        m_config.setValue(QStringLiteral("update/proxyPassword"), proxyPassword->text());
        // Compatibility with the first implementation and existing configs.
        m_config.setValue(QStringLiteral("general/autoUpdate"), checkAtStartup->isChecked());
    });

    auto *aboutPage = new QWidget(tabs);
    aboutPage->setProperty("referenceHeight", 500);
    auto *aboutLayout = new QVBoxLayout(aboutPage);
    aboutLayout->setContentsMargins(10, 10, 10, 8);
    aboutLayout->setSpacing(6);
    auto *aboutTop = new QHBoxLayout;
    aboutTop->setSpacing(12);
    auto *aboutDetails = new QWidget(aboutPage);
    auto *aboutDetailsLayout = new QVBoxLayout(aboutDetails);
    aboutDetailsLayout->setContentsMargins(0, 0, 0, 0);
    aboutDetailsLayout->setSpacing(3);
    auto *aboutBrandRow = new QHBoxLayout;
    aboutBrandRow->setContentsMargins(0, 0, 0, 0);
    auto *aboutLogo = new QLabel(aboutDetails);
    aboutLogo->setObjectName(QStringLiteral("aboutLogo"));
    QPixmap logo(24, 34);
    logo.fill(Qt::transparent);
    {
        QPainter painter(&logo);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(QStringLiteral("#4c9be8")));
        painter.drawRect(QRect(0, 0, 10, 16));
        painter.setBrush(QColor(QStringLiteral("#f3c442")));
        painter.drawRect(QRect(0, 18, 10, 16));
        painter.setBrush(QColor(QStringLiteral("#e96552")));
        painter.drawRect(QRect(13, 0, 10, 16));
        painter.setBrush(QColor(QStringLiteral("#55b766")));
        painter.drawRect(QRect(13, 18, 10, 16));
    }
    aboutLogo->setPixmap(logo);
    aboutBrandRow->addWidget(aboutLogo);
    auto *aboutName = new QLabel(QStringLiteral("Snipaste <small>(64 位)</small>"), aboutDetails);
    aboutName->setObjectName(QStringLiteral("aboutName"));
    aboutName->setTextFormat(Qt::RichText);
    QFont aboutFont = aboutName->font();
    aboutFont.setPointSize(18);
    aboutName->setFont(aboutFont);
    aboutBrandRow->addWidget(aboutName);
    aboutBrandRow->addStretch();
    aboutDetailsLayout->addLayout(aboutBrandRow);
    auto *aboutVersion = new QLabel(QStringLiteral("版本 0.1.0（开源开发版）"), aboutDetails);
    aboutVersion->setObjectName(QStringLiteral("aboutVersion"));
    aboutDetailsLayout->addWidget(aboutVersion);
    auto *aboutCopyright = new QLabel(QStringLiteral("版权所有 © 2016-2026 snipaste.com"), aboutDetails);
    aboutCopyright->setObjectName(QStringLiteral("aboutCopyright"));
    aboutDetailsLayout->addWidget(aboutCopyright);
    aboutTop->addWidget(aboutDetails, 1);

    auto *aboutButtons = new QVBoxLayout;
    aboutButtons->setSpacing(6);
    auto *builtWithQt = new QLabel(aboutPage);
    builtWithQt->setObjectName(QStringLiteral("aboutBuiltWithQt"));
    builtWithQt->setText(QStringLiteral("<span style='color:white;'>Built with </span>"
                                       "<span style='background:#111;color:white;font-weight:bold;padding:2px 4px;'>Qt</span>"));
    builtWithQt->setTextFormat(Qt::RichText);
    builtWithQt->setAlignment(Qt::AlignCenter);
    builtWithQt->setStyleSheet(QStringLiteral("background:#111111; padding:2px 3px;"));
    aboutButtons->addWidget(builtWithQt);
    auto *aboutOpenSource = new QPushButton(QStringLiteral("开源许可"), aboutPage);
    aboutOpenSource->setObjectName(QStringLiteral("aboutOpenSourceButton"));
    auto *aboutReview = new QPushButton(QStringLiteral("评价"), aboutPage);
    aboutReview->setObjectName(QStringLiteral("aboutReviewButton"));
    auto *aboutPurchase = new QPushButton(QStringLiteral("购买专业版"), aboutPage);
    aboutPurchase->setObjectName(QStringLiteral("aboutActivationButton"));
    aboutButtons->addWidget(aboutOpenSource);
    aboutButtons->addWidget(aboutReview);
    aboutButtons->addWidget(aboutPurchase);
    aboutTop->addLayout(aboutButtons);
    aboutLayout->addLayout(aboutTop);

    auto *supportBrowser = new QScrollArea(aboutPage);
    supportBrowser->setObjectName(QStringLiteral("aboutSupportBrowser"));
    supportBrowser->setWidgetResizable(true);
    supportBrowser->setFrameShape(QFrame::StyledPanel);
    supportBrowser->setFixedHeight(287);
    auto *supportContent = new QWidget;
    supportContent->setMinimumHeight(350);
    auto *supportLayout = new QVBoxLayout(supportContent);
    supportLayout->setContentsMargins(6, 4, 8, 6);
    supportLayout->setSpacing(2);
    auto *supportText = new QLabel(
        QStringLiteral("<b>支持</b><hr>"
                       "官网首页 - <a href='https://zh.snipaste.com'>https://zh.snipaste.com</a><br>"
                       "使用手册 - <a href='https://docs.snipaste.com/zh-cn'>https://docs.snipaste.com/zh-cn</a><br>"
                       "问题反馈 - <a href='https://github.com/Snipaste/feedback/issues'>https://snipaste.com/issues</a><br>"
                       "社交网站 - <a href='https://weibo.com'>微博</a> | <a href='https://twitter.com'>Twitter</a><br>"
                       "相关链接 - <a href='https://snipaste.com'>最终用户许可协议</a> | <a href='https://snipaste.com'>隐私政策</a><br><br>"
                       "<b>译者</b> +<hr>"
                       "简体中文　　　　 - <a href='https://github.com/liulex'>liulex</a><br>"
                       "繁体中文　　　　 - <a href='https://github.com/Brownsugar'>zhtw, Brownsugar</a><br>"
                       "德语　　　　　　 - <a href='https://github.com'>Samuel Marcius</a>"),
        supportContent);
    supportText->setObjectName(QStringLiteral("aboutSupportText"));
    supportText->setTextFormat(Qt::RichText);
    supportText->setOpenExternalLinks(false);
    supportText->setWordWrap(true);
    QObject::connect(supportText, &QLabel::linkActivated, this, [](const QString &url) {
        QDesktopServices::openUrl(QUrl(url));
    });
    supportLayout->addWidget(supportText);
    supportLayout->addStretch();
    supportBrowser->setWidget(supportContent);
    aboutLayout->addWidget(supportBrowser);
    auto *aboutFooter = new QHBoxLayout;
    auto *heart = new QLabel(QStringLiteral("♥"), aboutPage);
    heart->setStyleSheet(QStringLiteral("color:#e05252; font-size:16px;"));
    aboutFooter->addWidget(heart);
    aboutFooter->addStretch();
    auto *contact = new QLabel(QStringLiteral("联系:<a href='mailto:snipaste.app@gmail.com'>snipaste.app@gmail.com</a>"), aboutPage);
    contact->setObjectName(QStringLiteral("aboutContact"));
    contact->setTextFormat(Qt::RichText);
    contact->setOpenExternalLinks(true);
    aboutFooter->addWidget(contact);
    aboutLayout->addLayout(aboutFooter);
    QObject::connect(aboutOpenSource, &QPushButton::clicked, this, [this] {
        QMessageBox::information(this, QStringLiteral("开源许可"),
                                 QStringLiteral("snipate 使用 Qt 开源组件，项目源代码和许可说明位于 docs/ 目录。"));
    });
    QObject::connect(aboutReview, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com")));
    });
    QObject::connect(aboutPurchase, &QPushButton::clicked, this, [this] {
        QMessageBox::information(this, QStringLiteral("专业版"),
                                 QStringLiteral("这是开源版，专业版购买入口暂未接入。"));
    });
    tabs->addTab(aboutPage, QStringLiteral("关于"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->addWidget(tabs);
    compactLayoutTree(layout);

    const auto resizeToCurrentPage = [this, tabs, layout] {
        QWidget *page = tabs->currentWidget();
        if (!page)
            return;

        const int pageReferenceHeight = page->property("referenceHeight").toInt();
        int pageHeight = pageReferenceHeight > 0 ? pageReferenceHeight : page->sizeHint().height();
        if (auto *nested = page->findChild<QTabWidget *>()) {
            QWidget *current = nested->currentWidget();
            const int referenceHeight = current ? current->property("referenceHeight").toInt() : 0;
            const int currentHeight = referenceHeight > 0 ? referenceHeight : (current ? current->sizeHint().height() : 0);
            const int nestedHeight = currentHeight + nested->tabBar()->sizeHint().height() + 8;
            nested->setFixedHeight(nestedHeight);
            pageHeight = nestedHeight + 8;
        }

        const int outerTabHeight = tabs->tabBar()->sizeHint().height() + pageHeight + 8;
        tabs->setFixedHeight(outerTabHeight);
        const QMargins margins = layout->contentsMargins();
        const int totalHeight = margins.top() + margins.bottom() + outerTabHeight;
        setFixedHeight(totalHeight);
    };
    const auto queueResize = [this, resizeToCurrentPage] {
        QTimer::singleShot(0, this, resizeToCurrentPage);
    };
    QObject::connect(tabs, &QTabWidget::currentChanged, this, [queueResize](int) { queueResize(); });
    for (QTabWidget *nested : {interfaceTabs, captureTabs, pasteTabs, outputTabs, controlTabs}) {
        QObject::connect(nested, &QTabWidget::currentChanged, this, [queueResize](int) { queueResize(); });
    }
    QTimer::singleShot(0, this, resizeToCurrentPage);
}

void PreferencesDialog::accept()
{
    const bool startupChanged = m_startupCheck->isChecked()
        != m_config.value(QStringLiteral("general/startup"), false).toBool();
    m_config.setValue(QStringLiteral("general/startup"), m_startupCheck->isChecked());
    m_config.setValue(QStringLiteral("general/backup"), m_backupCheck->isChecked());
    m_config.setValue(QStringLiteral("general/quickResponse"), m_quickResponseCheck->isChecked());
    m_config.setValue(QStringLiteral("general/extendedTrayMenu"), m_extendedTrayCheck->isChecked());
    m_config.setValue(QStringLiteral("capture/cursor"), m_captureCursorCheck->isChecked());
    m_config.setValue(QStringLiteral("capture/showAnchor"), m_captureShowAnchorCheck->isChecked());
    m_config.setValue(QStringLiteral("capture/magnifierMask"), m_captureMagnifierMaskCheck->isChecked());
    m_config.setValue(QStringLiteral("capture/magnifierBorder"), m_captureMagnifierBorderCheck->isChecked());
    m_config.setValue(QStringLiteral("capture/magnifierAnchor"), m_captureMagnifierAnchorCheck->isChecked());
    m_config.setValue(QStringLiteral("capture/crosshair"), m_captureCrosshairCheck->isChecked());
    m_config.setValue(QStringLiteral("capture/disableGuides"), m_captureDisableGuidesCheck->isChecked());
    m_config.setValue(QStringLiteral("capture/shortcutHints"), m_captureShortcutHintsCheck->isChecked());
    m_config.setValue(QStringLiteral("capture/autoDetectWindow"), m_captureAutoDetectWindowCheck->isChecked());
    m_config.setValue(QStringLiteral("capture/hideOneByOne"), m_captureHideOneByOneCheck->isChecked());
    m_config.setValue(QStringLiteral("capture/autoDetectElements"), m_captureAutoDetectElementsCheck->isChecked());
    m_config.setValue(QStringLiteral("capture/disable"), m_captureDisableCheck->isChecked());
    m_config.setValue(QStringLiteral("capture/adjustHdr"), m_captureAdjustHdrCheck->isChecked());
    m_config.setValue(QStringLiteral("capture/exitOnFocus"), m_captureExitOnFocusCheck->isChecked());
    m_config.setValue(QStringLiteral("capture/maxHistoryRegions"), m_captureHistoryRegionSpin->value());
    m_config.setValue(QStringLiteral("capture/maxHistory"), m_captureHistorySpin->value());
    m_config.setValue(QStringLiteral("capture/soundEnabled"), m_captureSoundCheck->isChecked());
    m_config.setValue(QStringLiteral("capture/soundFile"), m_captureSoundFileEdit->text());
    m_config.setValue(QStringLiteral("paste/shadow"), m_pasteShadowCheck->isChecked());
    m_config.setValue(QStringLiteral("paste/helpKeys"), m_pasteHelpKeysCheck->isChecked());
    m_config.setValue(QStringLiteral("paste/opacity"), m_pasteOpacitySpin->value());
    m_config.setValue(QStringLiteral("paste/maxSize"), m_pasteMaxSizeSpin->value());
    m_config.setValue(QStringLiteral("paste/thumbWidth"), m_pasteThumbWidthSpin->value());
    m_config.setValue(QStringLiteral("paste/thumbHeight"), m_pasteThumbHeightSpin->value());
    m_config.setValue(QStringLiteral("paste/useCapturePosition"), m_pasteUseCapturePositionCheck->isChecked());
    m_config.setValue(QStringLiteral("paste/wheelCenter"), m_pasteWheelCenterCheck->isChecked());
    m_config.setValue(QStringLiteral("paste/smoothScale"), m_pasteSmoothScaleCheck->isChecked());
    m_config.setValue(QStringLiteral("paste/resetTransform"), m_pasteResetTransformCheck->isChecked());
    m_config.setValue(QStringLiteral("paste/flashTransparency"), m_pasteFlashTransparencyCheck->isChecked());
    m_config.setValue(QStringLiteral("paste/flashGroupSwitch"), m_pasteFlashOnGroupSwitchCheck->isChecked());
    m_config.setValue(QStringLiteral("paste/hideAllOnPasteKey"), m_pasteHideAllOnPasteKeyCheck->isChecked());
    m_config.setValue(QStringLiteral("paste/keepOnDrag"), m_pasteKeepOnDragCheck->isChecked());
    m_config.setValue(QStringLiteral("paste/closeConfirm"), m_pasteCloseConfirmCheck->isChecked());
    m_config.setValue(QStringLiteral("paste/destroyConfirm"), m_pasteDestroyConfirmCheck->isChecked());
    m_config.setValue(QStringLiteral("paste/flashNew"), m_pasteFlashNewCheck->isChecked());
    m_config.setValue(QStringLiteral("paste/activateNew"), m_pasteActivateNewCheck->isChecked());
    m_config.setValue(QStringLiteral("paste/restoreCount"), m_pasteRestoreCountSpin->value());
    m_config.setValue(QStringLiteral("paste/textToImage"), m_pasteTextToImageCheck->isChecked());
    m_config.setValue(QStringLiteral("paste/ignoreHtml"), m_pasteIgnoreHtmlCheck->isChecked());
    m_config.setValue(QStringLiteral("paste/pathToImage"), m_pastePathToImageCheck->isChecked());
    m_config.setValue(QStringLiteral("paste/textMargin"), m_pasteTextMarginSpin->value());
    m_config.setValue(QStringLiteral("paste/textMaxWidth"), m_pasteTextMaxWidthSpin->value());
    m_config.setValue(QStringLiteral("paste/textFont"), m_pasteTextFontEdit->text());
    m_config.setValue(QStringLiteral("capture/autoSave"), m_autoSaveCheck->isChecked());
    m_config.setValue(QStringLiteral("capture/borderWidth"), m_borderWidthSpin->value());
    m_config.setValue(QStringLiteral("output/imageQuality"), m_outputQualitySpin->value());
    m_config.setValue(QStringLiteral("output/fileNamePattern"), m_outputFileNameEdit->text());
    m_config.setValue(QStringLiteral("output/rememberExtension"),
                      m_outputRememberExtensionCheck->isChecked());
    m_config.setValue(QStringLiteral("output/quickSaveNotification"),
                      m_outputQuickSaveNotificationCheck->isChecked());
    m_config.setValue(QStringLiteral("output/saveDirectory"),
                      outputDirectoryFromPath(m_saveDirectoryEdit->text()));
    m_config.setValue(QStringLiteral("output/autoSaveDirectory"),
                      outputDirectoryFromPath(m_outputAutoSavePathEdit->text()));
    m_config.setValue(QStringLiteral("output/clipboardWindowsBitmap"),
                      m_outputCopyWindowsBitmapCheck->isChecked());
    m_config.setValue(QStringLiteral("output/clipboardScreenshot"),
                      m_outputCopyScreenshotCheck->isChecked());
    m_config.setValue(QStringLiteral("output/clipboardPaste"), m_outputCopyPasteCheck->isChecked());
    m_config.setValue(QStringLiteral("output/copyAsFileMode"),
                      m_outputCopyAsFileAlwaysRadio->isChecked()
                          ? 0
                          : m_outputCopyAsFileShortcutRadio->isChecked() ? 1 : 2);
    m_config.setValue(QStringLiteral("hotkeys/snip"),
                      m_snipHotkeyEdit->keySequence().toString(QKeySequence::PortableText));
    m_config.setValue(QStringLiteral("hotkeys/copy"),
                      m_copyHotkeyEdit->keySequence().toString(QKeySequence::PortableText));
    m_config.setValue(QStringLiteral("hotkeys/paste"),
                      m_pasteHotkeyEdit->keySequence().toString(QKeySequence::PortableText));
    for (int index = 0; index < m_controlMouseCombos.size(); ++index) {
        m_config.setValue(QStringLiteral("control/mouseAction%1").arg(index),
                          m_controlMouseCombos.at(index)->currentText());
    }
    const QStringList ignoredApplications = m_ignoreApplicationsEdit->text().split(
        QRegularExpression(QStringLiteral("[,;]")), Qt::SkipEmptyParts);
    m_config.setValue(QStringLiteral("hotkeys/ignoreApplications"), ignoredApplications);
    for (const auto &save : m_pageSavers) save();
    m_config.save();
#ifdef Q_OS_WIN
    if (startupChanged) {
    QSettings startupSettings(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                              QSettings::NativeFormat);
    const QString startupName = QStringLiteral("snipate");
    if (m_startupCheck->isChecked()) {
        startupSettings.setValue(startupName,
                                 QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
    } else {
        startupSettings.remove(startupName);
    }
    startupSettings.sync();
    }
#else
    Q_UNUSED(startupChanged)
#endif
    QDialog::accept();
}

void PreferencesDialog::reject()
{
    // Snipaste applies preference changes when the window closes. Keep that
    // interaction model while still returning Accepted to the command layer
    // so global hotkeys are refreshed immediately.
    accept();
}

} // namespace snipate::ui
