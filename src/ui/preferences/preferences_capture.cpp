#include "ui/preferences/preferences_dialog.h"
#include "core/config/config_store.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QColor>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QStyle>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace snipaste::ui {
namespace {

QPushButton *smallColorButton(QWidget *parent, const QString &name, const QColor &color)
{
    auto *button = new QPushButton(parent);
    button->setObjectName(name);
    button->setFixedSize(30, 28);
    button->setProperty("color", color);
    button->setStyleSheet(QStringLiteral("padding: 0; border: 1px solid #aaa;"));
    return button;
}

void setColorButtonAppearance(QPushButton *button, const QColor &color)
{
    QPixmap pixmap(28, 26);
    pixmap.fill(Qt::white);
    QPainter painter(&pixmap);
    constexpr int tile = 7;
    for (int y = 0; y < pixmap.height(); y += tile) {
        for (int x = 0; x < pixmap.width(); x += tile) {
            if (((x / tile) + (y / tile)) % 2 == 0)
                painter.fillRect(x, y, tile, tile, QColor(QStringLiteral("#d8d8d8")));
        }
    }
    painter.fillRect(pixmap.rect(), color);
    painter.setPen(QColor(QStringLiteral("#aaaaaa")));
    painter.drawRect(pixmap.rect().adjusted(0, 0, -1, -1));
    button->setProperty("color", color);
    button->setIcon(QIcon(pixmap));
    button->setIconSize(pixmap.size());
}

QLabel *info(QWidget *parent, const QString &tip)
{
    auto *label = new QLabel(QStringLiteral("i"), parent);
    label->setAlignment(Qt::AlignCenter);
    label->setFixedSize(16, 16);
    label->setStyleSheet(QStringLiteral("QLabel { color: #555555; border: 1px solid #777777; border-radius: 8px; font-size: 11px; font-weight: 600; }"));
    label->setToolTip(tip);
    return label;
}

void connectColorButton(QPushButton *button,
                        QWidget *parent,
                        const QString &title,
                        const QColor &initial)
{
    button->setProperty("color", initial);
    QObject::connect(button, &QPushButton::clicked, parent, [button, parent, title] {
        const QColor current = button->property("color").value<QColor>();
        const QColor selected = QColorDialog::getColor(
            current, parent, title, QColorDialog::ShowAlphaChannel);
        if (!selected.isValid())
            return;
        setColorButtonAppearance(button, selected);
    });
}

QHBoxLayout *line(QWidget *parent, QVBoxLayout *column)
{
    auto *widget = new QWidget(parent);
    widget->setFixedHeight(28);
    auto *layout = new QHBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(7);
    column->addWidget(widget);
    return layout;
}

} // namespace

QWidget *PreferencesDialog::createCapturePage(QWidget *parent)
{
    auto *page = new QWidget(parent);
    page->setObjectName(QStringLiteral("capturePage"));
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    auto *tabs = new QTabWidget(page);
    tabs->setObjectName(QStringLiteral("captureTabs"));
    layout->addWidget(tabs);

    auto *display = new QWidget(tabs);
    auto *displayLayout = new QVBoxLayout(display);
    displayLayout->setContentsMargins(12, 13, 12, 12);
    displayLayout->setSpacing(7);
    auto *border = line(display, displayLayout);
    border->addWidget(new QLabel(QStringLiteral("边框宽度:"), display));
    m_borderWidthSpin = new QSpinBox(display);
    m_borderWidthSpin->setObjectName(QStringLiteral("captureBorderWidth"));
    m_borderWidthSpin->setRange(1, 20);
    m_borderWidthSpin->setValue(m_config.value(QStringLiteral("capture/borderWidth"), 3).toInt());
    border->addWidget(m_borderWidthSpin);
    border->addWidget(new QLabel(QStringLiteral("像素"), display));
    border->addStretch();

    auto *mask = line(display, displayLayout);
    mask->addWidget(new QLabel(QStringLiteral("遮罩颜色:"), display));
    const QColor maskInitial = QColor::fromString(
        m_config.value(QStringLiteral("capture/maskColor"), QStringLiteral("#78000000")).toString());
    const QColor maskFallback = maskInitial.isValid() ? maskInitial : QColor(0, 0, 0, 120);
    auto *maskColor = smallColorButton(display, QStringLiteral("captureMaskColor"), maskFallback);
    setColorButtonAppearance(maskColor, maskFallback);
    connectColorButton(maskColor, display, QStringLiteral("选择遮罩颜色"), maskFallback);
    mask->addWidget(maskColor);
    mask->addStretch();

    auto *anchor = line(display, displayLayout);
    m_captureShowAnchorCheck = new QCheckBox(QStringLiteral("显示锚点"), display);
    m_captureShowAnchorCheck->setChecked(m_config.value(QStringLiteral("capture/showAnchor"), true).toBool());
    anchor->addWidget(m_captureShowAnchorCheck); anchor->addStretch();

    auto *anchorColor = line(display, displayLayout);
    anchorColor->addWidget(new QLabel(QStringLiteral("锚点描边颜色:"), display));
    const QColor anchorInitial = QColor::fromString(
        m_config.value(QStringLiteral("capture/anchorColor"), QStringLiteral("#ffffffff")).toString());
    const QColor anchorFallback = anchorInitial.isValid() ? anchorInitial : QColor(Qt::white);
    auto *anchorColorButton = smallColorButton(display, QStringLiteral("captureAnchorColor"), anchorFallback);
    setColorButtonAppearance(anchorColorButton, anchorFallback);
    connectColorButton(anchorColorButton, display, QStringLiteral("选择锚点描边颜色"), anchorFallback);
    anchorColor->addWidget(anchorColorButton);
    anchorColor->addStretch();

    auto *magnifier = line(display, displayLayout);
    magnifier->addWidget(new QLabel(QStringLiteral("在放大镜中，显示:"), display));
    m_captureMagnifierMaskCheck = new QCheckBox(QStringLiteral("遮罩"), display);
    m_captureMagnifierBorderCheck = new QCheckBox(QStringLiteral("边框"), display);
    m_captureMagnifierAnchorCheck = new QCheckBox(QStringLiteral("锚点"), display);
    m_captureMagnifierMaskCheck->setChecked(m_config.value(QStringLiteral("capture/magnifierMask"), true).toBool());
    m_captureMagnifierBorderCheck->setChecked(m_config.value(QStringLiteral("capture/magnifierBorder"), true).toBool());
    m_captureMagnifierAnchorCheck->setChecked(m_config.value(QStringLiteral("capture/magnifierAnchor"), false).toBool());
    magnifier->addWidget(m_captureMagnifierMaskCheck); magnifier->addWidget(m_captureMagnifierBorderCheck);
    magnifier->addWidget(m_captureMagnifierAnchorCheck); magnifier->addStretch();

    auto *crosshair = line(display, displayLayout);
    m_captureCrosshairCheck = new QCheckBox(QStringLiteral("显示全屏十字线"), display);
    m_captureCrosshairCheck->setChecked(m_config.value(QStringLiteral("capture/crosshair"), false).toBool());
    crosshair->addWidget(m_captureCrosshairCheck); crosshair->addStretch();
    auto *guides = line(display, displayLayout);
    m_captureDisableGuidesCheck = new QCheckBox(QStringLiteral("禁用辅助线"), display);
    m_captureDisableGuidesCheck->setChecked(m_config.value(QStringLiteral("capture/disableGuides"), false).toBool());
    guides->addWidget(m_captureDisableGuidesCheck); guides->addStretch();
    auto *hints = line(display, displayLayout);
    m_captureShortcutHintsCheck = new QCheckBox(QStringLiteral("显示快捷键提示"), display);
    m_captureShortcutHintsCheck->setChecked(m_config.value(QStringLiteral("capture/shortcutHints"), true).toBool());
    hints->addWidget(m_captureShortcutHintsCheck); hints->addStretch();
    displayLayout->addStretch();
    auto *displayFooter = new QHBoxLayout; displayFooter->addStretch();
    auto *displayRestore = new QPushButton(QStringLiteral("恢复默认"), display); displayRestore->setObjectName(QStringLiteral("restoreCaptureDisplay"));
    displayFooter->addWidget(displayRestore); displayLayout->addLayout(displayFooter);
    connect(displayRestore, &QPushButton::clicked, this, [this, maskColor, anchorColorButton] {
        m_borderWidthSpin->setValue(3); m_captureShowAnchorCheck->setChecked(true);
        setColorButtonAppearance(maskColor, QColor(0, 0, 0, 120));
        setColorButtonAppearance(anchorColorButton, QColor(Qt::white));
        m_captureMagnifierMaskCheck->setChecked(true); m_captureMagnifierBorderCheck->setChecked(true);
        m_captureMagnifierAnchorCheck->setChecked(false); m_captureCrosshairCheck->setChecked(false);
        m_captureDisableGuidesCheck->setChecked(false); m_captureShortcutHintsCheck->setChecked(true);
    });
    m_pageSavers.append([this, maskColor, anchorColorButton] {
        m_config.setValue(QStringLiteral("capture/maskColor"),
                          maskColor->property("color").value<QColor>().name(QColor::HexArgb));
        m_config.setValue(QStringLiteral("capture/anchorColor"),
                          anchorColorButton->property("color").value<QColor>().name(QColor::HexArgb));
    });
    tabs->addTab(display, QStringLiteral("显示"));

    auto *behavior = new QWidget(tabs);
    auto *behaviorLayout = new QVBoxLayout(behavior);
    behaviorLayout->setContentsMargins(12, 13, 12, 12); behaviorLayout->setSpacing(7);
    auto *autoRow = line(behavior, behaviorLayout);
    m_captureAutoDetectWindowCheck = new QCheckBox(QStringLiteral("自动检测窗口"), behavior);
    m_captureHideOneByOneCheck = new QCheckBox(QStringLiteral("隐藏 1x1 提示"), behavior);
    m_captureAutoDetectWindowCheck->setChecked(m_config.value(QStringLiteral("capture/autoDetectWindow"), true).toBool());
    m_captureHideOneByOneCheck->setChecked(m_config.value(QStringLiteral("capture/hideOneByOne"), true).toBool());
    autoRow->addWidget(m_captureAutoDetectWindowCheck); autoRow->addWidget(m_captureHideOneByOneCheck); autoRow->addStretch();
    auto *elements = line(behavior, behaviorLayout);
    m_captureAutoDetectElementsCheck = new QCheckBox(QStringLiteral("自动检测界面元素"), behavior);
    m_captureAutoDetectElementsCheck->setChecked(m_config.value(QStringLiteral("capture/autoDetectElements"), true).toBool());
    elements->addWidget(m_captureAutoDetectElementsCheck); elements->addWidget(info(behavior, QStringLiteral("使用窗口和界面元素检测进行自动框选。"))); elements->addStretch();
    auto *cursor = line(behavior, behaviorLayout);
    m_captureCursorCheck = new QCheckBox(QStringLiteral("捕捉鼠标指针"), behavior);
    m_captureDisableCheck = new QCheckBox(QStringLiteral("完全禁用"), behavior);
    m_captureCursorCheck->setChecked(m_config.value(QStringLiteral("capture/cursor"), false).toBool());
    m_captureDisableCheck->setChecked(m_config.value(QStringLiteral("capture/disable"), false).toBool());
    cursor->addWidget(m_captureCursorCheck); cursor->addWidget(info(behavior, QStringLiteral("将鼠标指针合成到截图中。")));
    cursor->addWidget(m_captureDisableCheck); cursor->addWidget(info(behavior, QStringLiteral("禁用所有自动检测和辅助显示。"))); cursor->addStretch();
    auto *hdr = line(behavior, behaviorLayout);
    m_captureAdjustHdrCheck = new QCheckBox(QStringLiteral("调整 HDR 显示器上截图的颜色"), behavior);
    m_captureAdjustHdrCheck->setChecked(m_config.value(QStringLiteral("capture/adjustHdr"), true).toBool()); hdr->addWidget(m_captureAdjustHdrCheck); hdr->addStretch();
    auto *focus = line(behavior, behaviorLayout);
    m_captureExitOnFocusCheck = new QCheckBox(QStringLiteral("截屏时如有其他程序窗口被激活，自动退出截屏"), behavior);
    m_captureExitOnFocusCheck->setChecked(m_config.value(QStringLiteral("capture/exitOnFocus"), true).toBool()); focus->addWidget(m_captureExitOnFocusCheck); focus->addStretch();
    auto *dialogOverlay = line(behavior, behaviorLayout);
    auto *hideOverlay = new QCheckBox(QStringLiteral("文件对话框打开时隐藏遮罩"), behavior);
    hideOverlay->setChecked(m_config.value(QStringLiteral("capture/hideOverlayOnFileDialog"), true).toBool());
    dialogOverlay->addWidget(hideOverlay); dialogOverlay->addStretch();
    auto *defaultAnnotation = line(behavior, behaviorLayout);
    defaultAnnotation->addWidget(new QLabel(QStringLiteral("默认标注:"), behavior));
    auto *annotationCombo = new QComboBox(behavior); annotationCombo->addItems({QStringLiteral("无"), QStringLiteral("矩形"), QStringLiteral("箭头")});
    annotationCombo->setCurrentText(m_config.value(QStringLiteral("capture/defaultAnnotation"), QStringLiteral("无")).toString()); defaultAnnotation->addWidget(annotationCombo); defaultAnnotation->addStretch();
    auto *regions = line(behavior, behaviorLayout);
    regions->addWidget(new QLabel(QStringLiteral("最大历史截屏区域数:"), behavior));
    m_captureHistoryRegionSpin = new QSpinBox(behavior); m_captureHistoryRegionSpin->setRange(1, 99); m_captureHistoryRegionSpin->setValue(m_config.value(QStringLiteral("capture/maxHistoryRegions"), 8).toInt()); regions->addWidget(m_captureHistoryRegionSpin);
    auto *cycle = new QCheckBox(QStringLiteral("循环"), behavior); cycle->setChecked(m_config.value(QStringLiteral("capture/historyCycle"), false).toBool()); regions->addWidget(cycle); regions->addStretch();
    auto *history = line(behavior, behaviorLayout);
    history->addWidget(new QLabel(QStringLiteral("最大历史记录数:"), behavior));
    m_captureHistorySpin = new QSpinBox(behavior); m_captureHistorySpin->setRange(1, 999); m_captureHistorySpin->setValue(m_config.value(QStringLiteral("capture/maxHistory"), 20).toInt()); history->addWidget(m_captureHistorySpin);
    auto *unsaved = new QCheckBox(QStringLiteral("包含未保存的截图"), behavior); unsaved->setChecked(m_config.value(QStringLiteral("capture/includeUnsaved"), false).toBool()); history->addWidget(unsaved); history->addStretch();
    auto *sound = new QGroupBox(QStringLiteral("音效"), behavior);
    auto *soundLayout = new QVBoxLayout(sound); soundLayout->setContentsMargins(12, 18, 12, 10); soundLayout->setSpacing(7);
    auto *soundFile = new QHBoxLayout; soundFile->addWidget(new QLabel(QStringLiteral("音效文件:"), sound));
    m_captureSoundFileEdit = new QLineEdit(m_config.value(QStringLiteral("capture/soundFile"), QStringLiteral("sound/snip.wav")).toString(), sound); soundFile->addWidget(m_captureSoundFileEdit);
    auto *play = new QToolButton(sound); play->setIcon(sound->style()->standardIcon(QStyle::SP_MediaPlay)); play->setFixedSize(28, 26); soundFile->addWidget(play); soundLayout->addLayout(soundFile);
    auto *when = new QHBoxLayout; when->addWidget(new QLabel(QStringLiteral("何时:"), sound)); m_captureSoundCheck = new QCheckBox(QStringLiteral("复制到剪贴板"), sound); m_captureSoundCheck->setChecked(m_config.value(QStringLiteral("capture/soundEnabled"), true).toBool()); auto *soundQuick = new QCheckBox(QStringLiteral("快捷保存"), sound); soundQuick->setChecked(m_config.value(QStringLiteral("capture/soundQuickSave"), false).toBool()); when->addWidget(m_captureSoundCheck); when->addWidget(soundQuick); when->addStretch(); auto *changeSound = new QPushButton(QStringLiteral("更改"), sound); when->addWidget(changeSound); soundLayout->addLayout(when);
    behaviorLayout->addWidget(sound); behaviorLayout->addStretch();
    auto *behaviorFooter = new QHBoxLayout; behaviorFooter->addStretch(); auto *behaviorRestore = new QPushButton(QStringLiteral("恢复默认"), behavior); behaviorFooter->addWidget(behaviorRestore); behaviorLayout->addLayout(behaviorFooter);
    connect(changeSound, &QPushButton::clicked, this, [this] { const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择音效"), {}, QStringLiteral("音频 (*.wav *.mp3)")); if (!path.isEmpty()) m_captureSoundFileEdit->setText(path); });
    connect(behaviorRestore, &QPushButton::clicked, this, [this, hideOverlay, annotationCombo, cycle, unsaved, soundQuick] {
        m_captureAutoDetectWindowCheck->setChecked(true); m_captureHideOneByOneCheck->setChecked(true); m_captureAutoDetectElementsCheck->setChecked(true); m_captureCursorCheck->setChecked(false); m_captureDisableCheck->setChecked(false); m_captureAdjustHdrCheck->setChecked(true); m_captureExitOnFocusCheck->setChecked(true); hideOverlay->setChecked(true); annotationCombo->setCurrentText(QStringLiteral("无")); m_captureHistoryRegionSpin->setValue(8); cycle->setChecked(false); m_captureHistorySpin->setValue(20); unsaved->setChecked(false); m_captureSoundCheck->setChecked(true); soundQuick->setChecked(false); m_captureSoundFileEdit->setText(QStringLiteral("sound/snip.wav"));
    });
    tabs->addTab(behavior, QStringLiteral("行为"));
    m_pageSavers.append([this, hideOverlay, annotationCombo, cycle, unsaved, soundQuick] {
        m_config.setValue(QStringLiteral("capture/hideOverlayOnFileDialog"), hideOverlay->isChecked()); m_config.setValue(QStringLiteral("capture/defaultAnnotation"), annotationCombo->currentText()); m_config.setValue(QStringLiteral("capture/historyCycle"), cycle->isChecked()); m_config.setValue(QStringLiteral("capture/includeUnsaved"), unsaved->isChecked()); m_config.setValue(QStringLiteral("capture/soundQuickSave"), soundQuick->isChecked());
    });

    auto *keys = new QWidget(tabs);
    auto *keysLayout = new QVBoxLayout(keys); keysLayout->setContentsMargins(12, 13, 12, 12); keysLayout->setSpacing(7);
    const auto keyCheck = [keysLayout, keys](const QString &text, const QString &key, bool value) { auto *check = new QCheckBox(text, keys); check->setObjectName(key); check->setChecked(value); keysLayout->addWidget(check); return check; };
    auto *disableScroll = keyCheck(QStringLiteral("禁用鼠标滚轮功能"), QStringLiteral("capture/disableWheel"), m_config.value(QStringLiteral("capture/disableWheel"), false).toBool());
    auto *confirmEsc = keyCheck(QStringLiteral("按下 Esc 键时弹出确认对话框"), QStringLiteral("capture/confirmEsc"), m_config.value(QStringLiteral("capture/confirmEsc"), false).toBool());
    auto *ignoreCopy = keyCheck(QStringLiteral("忽略其他程序模拟的“复制”命令"), QStringLiteral("capture/ignoreSimulatedCopy"), m_config.value(QStringLiteral("capture/ignoreSimulatedCopy"), true).toBool());
    auto *winRow = new QHBoxLayout; winRow->addWidget(new QLabel(QStringLiteral("屏蔽 Win 键:"), keys)); auto *winLeft = new QCheckBox(QStringLiteral("左"), keys); auto *winRight = new QCheckBox(QStringLiteral("右"), keys); winLeft->setChecked(m_config.value(QStringLiteral("capture/blockWinLeft"), true).toBool()); winRight->setChecked(m_config.value(QStringLiteral("capture/blockWinRight"), false).toBool()); winRow->addWidget(winLeft); winRow->addWidget(winRight); winRow->addStretch(); keysLayout->addLayout(winRow);
    auto *actionGroup = new QGroupBox(QStringLiteral("动作"), keys); auto *actionLayout = new QFormLayout(actionGroup); actionLayout->setContentsMargins(12, 18, 12, 10); actionLayout->setVerticalSpacing(7);
    const auto actionRow = [actionGroup, actionLayout, this](const QString &label, const QString &key, const QString &fallback, const QString &exitKey) { auto *rowWidget = new QWidget(actionGroup); auto *rowLayout = new QHBoxLayout(rowWidget); rowLayout->setContentsMargins(0, 0, 0, 0); auto *combo = new QComboBox(rowWidget); combo->setObjectName(key); combo->addItems({QStringLiteral("复制到剪贴板"), QStringLiteral("贴到屏幕"), QStringLiteral("保存文件"), QStringLiteral("无")}); combo->setCurrentText(m_config.value(key, fallback).toString()); auto *then = new QCheckBox(QStringLiteral("然后"), rowWidget); auto *exit = new QCheckBox(QStringLiteral("退出截屏"), rowWidget); exit->setChecked(m_config.value(exitKey, true).toBool()); rowLayout->addWidget(combo); rowLayout->addWidget(then); rowLayout->addWidget(exit); rowLayout->addStretch(); actionLayout->addRow(label, rowWidget); return std::pair<QComboBox *, QCheckBox *>(combo, exit); };
    const auto left = actionRow(QStringLiteral("左键双击:"), QStringLiteral("capture/doubleClickAction"), QStringLiteral("复制到剪贴板"), QStringLiteral("capture/doubleClickExit"));
    const auto middle = actionRow(QStringLiteral("中键单击:"), QStringLiteral("capture/middleClickAction"), QStringLiteral("贴到屏幕"), QStringLiteral("capture/middleClickExit"));
    const auto enter = actionRow(QStringLiteral("回车键:"), QStringLiteral("capture/enterAction"), QStringLiteral("复制到剪贴板"), QStringLiteral("capture/enterExit"));
    keysLayout->addWidget(actionGroup); keysLayout->addStretch(); auto *keysFooter = new QHBoxLayout; keysFooter->addStretch(); auto *keysRestore = new QPushButton(QStringLiteral("恢复默认"), keys); keysFooter->addWidget(keysRestore); keysLayout->addLayout(keysFooter);
    connect(keysRestore, &QPushButton::clicked, this, [=] { disableScroll->setChecked(false); confirmEsc->setChecked(false); ignoreCopy->setChecked(true); winLeft->setChecked(true); winRight->setChecked(false); left.first->setCurrentText(QStringLiteral("复制到剪贴板")); left.second->setChecked(true); middle.first->setCurrentText(QStringLiteral("贴到屏幕")); middle.second->setChecked(true); enter.first->setCurrentText(QStringLiteral("复制到剪贴板")); enter.second->setChecked(true); });
    m_pageSavers.append([=, this] { m_config.setValue(QStringLiteral("capture/disableWheel"), disableScroll->isChecked()); m_config.setValue(QStringLiteral("capture/confirmEsc"), confirmEsc->isChecked()); m_config.setValue(QStringLiteral("capture/ignoreSimulatedCopy"), ignoreCopy->isChecked()); m_config.setValue(QStringLiteral("capture/blockWinLeft"), winLeft->isChecked()); m_config.setValue(QStringLiteral("capture/blockWinRight"), winRight->isChecked()); m_config.setValue(QStringLiteral("capture/doubleClickAction"), left.first->currentText()); m_config.setValue(QStringLiteral("capture/doubleClickExit"), left.second->isChecked()); m_config.setValue(QStringLiteral("capture/middleClickAction"), middle.first->currentText()); m_config.setValue(QStringLiteral("capture/middleClickExit"), middle.second->isChecked()); m_config.setValue(QStringLiteral("capture/enterAction"), enter.first->currentText()); m_config.setValue(QStringLiteral("capture/enterExit"), enter.second->isChecked()); });
    tabs->addTab(keys, QStringLiteral("按键"));
    return page;
}

} // namespace snipaste::ui
