#include "ui/preferences/preferences_dialog.h"

#include "core/config/config_store.h"
#include "platform/keyboard.h"

#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>
#include <memory>

#include "core/command/argument_parser.h"
#include "ui/preferences/preference_widgets.h"

namespace snipaste::ui
{
using namespace preferences;
QWidget *PreferencesDialog::createPastePage(QWidget *parent)
{
    struct PageControls
    {
        QCheckBox *pasteShadowCheck = nullptr;
        QCheckBox *pasteHelpKeysCheck = nullptr;
        QCheckBox *pasteUseCapturePositionCheck = nullptr;
        QCheckBox *pasteWheelCenterCheck = nullptr;
        QCheckBox *pasteSmoothScaleCheck = nullptr;
        QCheckBox *pasteResetTransformCheck = nullptr;
        QCheckBox *pasteFlashTransparencyCheck = nullptr;
        QCheckBox *pasteFlashOnGroupSwitchCheck = nullptr;
        QCheckBox *pasteHideAllOnPasteKeyCheck = nullptr;
        QCheckBox *pasteKeepOnDragCheck = nullptr;
        QCheckBox *pasteCloseConfirmCheck = nullptr;
        QCheckBox *pasteDestroyConfirmCheck = nullptr;
        QCheckBox *pasteFlashNewCheck = nullptr;
        QCheckBox *pasteActivateNewCheck = nullptr;
        QCheckBox *pasteTextToImageCheck = nullptr;
        QCheckBox *pasteIgnoreHtmlCheck = nullptr;
        QCheckBox *pastePathToImageCheck = nullptr;
        QSpinBox *pasteOpacitySpin = nullptr;
        QSpinBox *pasteMaxSizeSpin = nullptr;
        QSpinBox *pasteThumbWidthSpin = nullptr;
        QSpinBox *pasteThumbHeightSpin = nullptr;
        QSpinBox *pasteRestoreCountSpin = nullptr;
        QSpinBox *pasteTextMarginSpin = nullptr;
        QSpinBox *pasteTextMaxWidthSpin = nullptr;
        QLineEdit *pasteTextFontEdit = nullptr;
    };
    auto controls = std::make_shared<PageControls>();
    auto *pastePage = new QWidget(parent);
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
    controls->pasteShadowCheck = new QCheckBox(QStringLiteral("窗口阴影"), shadowRow);
    controls->pasteShadowCheck->setObjectName(QStringLiteral("pasteShadowCheck"));
    controls->pasteShadowCheck->setChecked(m_config.value(QStringLiteral("paste/shadow"), true).toBool());
    shadowLayout->addWidget(controls->pasteShadowCheck);
    auto *shadowColor = makeColorButton(QColor(QStringLiteral("#808080")), shadowRow);
    shadowColor->setObjectName(QStringLiteral("pasteShadowColor"));
    shadowLayout->addWidget(shadowColor);
    shadowLayout->addStretch();
    pasteDisplayForm->addRow(shadowRow);
    controls->pasteHelpKeysCheck = new QCheckBox(QStringLiteral("在右键菜单显示助记键"), pasteDisplay);
    controls->pasteHelpKeysCheck->setObjectName(QStringLiteral("pasteHelpKeysCheck"));
    controls->pasteHelpKeysCheck->setChecked(
        m_config.value(QStringLiteral("paste/helpKeys"), false).toBool());
    pasteDisplayForm->addRow(controls->pasteHelpKeysCheck);
    auto *backgroundMode = new QComboBox(pasteDisplay);
    backgroundMode->setObjectName(QStringLiteral("pasteBackgroundMode"));
    backgroundMode->setFixedWidth(120);
    backgroundMode->addItems(
        {QStringLiteral("伪透明"), QStringLiteral("透明"), QStringLiteral("白色"), QStringLiteral("黑色")});
    backgroundMode->setCurrentText(
        m_config.value(QStringLiteral("paste/backgroundMode"), QStringLiteral("伪透明")).toString());
    pasteDisplayForm->addRow(QStringLiteral("默认背景模式:"), backgroundMode);
    controls->pasteOpacitySpin = new QSpinBox(pasteDisplay);
    controls->pasteOpacitySpin->setFixedWidth(92);
    controls->pasteOpacitySpin->setRange(10, 100);
    controls->pasteOpacitySpin->setSuffix(QStringLiteral(" %"));
    const double rawPasteOpacity = m_config.value(QStringLiteral("paste/opacity"), 100).toDouble();
    controls->pasteOpacitySpin->setValue(
        qBound(10, qRound(rawPasteOpacity > 1.0 ? rawPasteOpacity : rawPasteOpacity * 100.0), 100));
    pasteDisplayForm->addRow(QStringLiteral("默认不透明度:"), controls->pasteOpacitySpin);
    controls->pasteMaxSizeSpin = new QSpinBox(pasteDisplay);
    controls->pasteMaxSizeSpin->setFixedWidth(112);
    controls->pasteMaxSizeSpin->setRange(256, 20000);
    controls->pasteMaxSizeSpin->setSuffix(QStringLiteral(" 像素"));
    controls->pasteMaxSizeSpin->setValue(m_config.value(QStringLiteral("paste/maxSize"), 12000).toInt());
    pasteDisplayForm->addRow(QStringLiteral("最大窗口尺寸:"), controls->pasteMaxSizeSpin);
    auto *thumbRow = new QWidget(pasteDisplay);
    auto *thumbLayout = new QHBoxLayout(thumbRow);
    thumbLayout->setContentsMargins(0, 0, 0, 0);
    controls->pasteThumbWidthSpin = new QSpinBox(thumbRow);
    controls->pasteThumbHeightSpin = new QSpinBox(thumbRow);
    for (QSpinBox *spin : {controls->pasteThumbWidthSpin, controls->pasteThumbHeightSpin})
    {
        spin->setRange(16, 512);
        spin->setFixedWidth(74);
    }
    controls->pasteThumbWidthSpin->setValue(m_config.value(QStringLiteral("paste/thumbWidth"), 62).toInt());
    controls->pasteThumbHeightSpin->setValue(m_config.value(QStringLiteral("paste/thumbHeight"), 62).toInt());
    thumbLayout->addWidget(controls->pasteThumbWidthSpin);
    thumbLayout->addWidget(new QLabel(QStringLiteral("×"), thumbRow));
    thumbLayout->addWidget(controls->pasteThumbHeightSpin);
    thumbLayout->addStretch();
    pasteDisplayForm->addRow(QStringLiteral("快速缩略图大小:"), thumbRow);
    auto *pasteDisplayRestore = new QPushButton(QStringLiteral("恢复默认"), pasteDisplay);
    pasteDisplayRestore->setFixedWidth(86);
    auto *pasteDisplayFooter = new QHBoxLayout;
    pasteDisplayFooter->addStretch();
    pasteDisplayFooter->addWidget(pasteDisplayRestore);
    pasteDisplayForm->addRow(pasteDisplayFooter);
    QObject::connect(pasteDisplayRestore, &QPushButton::clicked, this, [=] {
        controls->pasteShadowCheck->setChecked(true);
        controls->pasteHelpKeysCheck->setChecked(false);
        controls->pasteOpacitySpin->setValue(100);
        controls->pasteMaxSizeSpin->setValue(12000);
        controls->pasteThumbWidthSpin->setValue(62);
        controls->pasteThumbHeightSpin->setValue(62);
        backgroundMode->setCurrentText(QStringLiteral("伪透明"));
    });
    m_pageSavers.append([=, this] {
        m_config.setValue(QStringLiteral("paste/backgroundMode"), backgroundMode->currentText());
    });
    pasteTabs->addTab(pasteDisplay, QStringLiteral("显示"));

    auto *pasteBehavior = new QWidget(pasteTabs);
    auto *pasteBehaviorLayout = new QVBoxLayout(pasteBehavior);
    pasteBehaviorLayout->setContentsMargins(6, 6, 6, 6);
    auto addPasteBehaviorCheck = [this, pasteBehaviorLayout,
                                  pasteBehavior](QCheckBox *&slot, const QString &text, const QString &key,
                                                 bool fallback) {
        slot = new QCheckBox(text, pasteBehavior);
        slot->setChecked(m_config.value(key, fallback).toBool());
        pasteBehaviorLayout->addWidget(slot);
    };
    addPasteBehaviorCheck(controls->pasteUseCapturePositionCheck, QStringLiteral("使用截屏时的位置"),
                          QStringLiteral("paste/useCapturePosition"), true);
    addPasteBehaviorCheck(controls->pasteWheelCenterCheck, QStringLiteral("鼠标滚轮缩放时以指针为中心"),
                          QStringLiteral("paste/wheelCenter"), false);
    addPasteBehaviorCheck(controls->pasteSmoothScaleCheck, QStringLiteral("平滑缩放"),
                          QStringLiteral("paste/smoothScale"), true);
    QCheckBox *pasteVirtualDesktopCheck = nullptr;
    addPasteBehaviorCheck(pasteVirtualDesktopCheck, QStringLiteral("固定到所有虚拟桌面"),
                          QStringLiteral("paste/virtualDesktop"), true);
    pasteVirtualDesktopCheck->setObjectName(QStringLiteral("pasteVirtualDesktopCheck"));
    addPasteBehaviorCheck(controls->pasteResetTransformCheck, QStringLiteral("缩略图变换‘重置’操作"),
                          QStringLiteral("paste/resetTransform"), true);
    addPasteBehaviorCheck(controls->pasteFlashTransparencyCheck, QStringLiteral("边框闪烁时使窗口回归半透明"),
                          QStringLiteral("paste/flashTransparency"), true);
    addPasteBehaviorCheck(controls->pasteFlashOnGroupSwitchCheck,
                          QStringLiteral("切换到另一分组时自动闪烁贴图"),
                          QStringLiteral("paste/flashGroupSwitch"), true);
    addPasteBehaviorCheck(controls->pasteHideAllOnPasteKeyCheck,
                          QStringLiteral("在贴图闪烁时按下贴图键，隐藏所有贴图"),
                          QStringLiteral("paste/hideAllOnPasteKey"), false);
    addPasteBehaviorCheck(controls->pasteKeepOnDragCheck, QStringLiteral("拖放操作时保留贴图"),
                          QStringLiteral("paste/keepOnDrag"), false);
    QCheckBox *pasteHideOnFileDialogCheck = nullptr;
    addPasteBehaviorCheck(pasteHideOnFileDialogCheck, QStringLiteral("文件对话框打开时隐藏所有贴图"),
                          QStringLiteral("paste/hideOnFileDialog"), false);
    pasteHideOnFileDialogCheck->setObjectName(QStringLiteral("pasteHideOnFileDialogCheck"));
    addPasteBehaviorCheck(controls->pasteCloseConfirmCheck, QStringLiteral("关闭多个贴图时弹出确认对话框"),
                          QStringLiteral("paste/closeConfirm"), true);
    addPasteBehaviorCheck(controls->pasteDestroyConfirmCheck, QStringLiteral("销毁多个贴图时弹出确认对话框"),
                          QStringLiteral("paste/destroyConfirm"), true);
    auto *newPasteRow = new QWidget(pasteBehavior);
    auto *newPasteLayout = new QHBoxLayout(newPasteRow);
    newPasteLayout->setContentsMargins(0, 0, 0, 0);
    newPasteLayout->addWidget(new QLabel(QStringLiteral("新的图片被贴出时:"), newPasteRow));
    controls->pasteFlashNewCheck = new QCheckBox(QStringLiteral("闪烁"), newPasteRow);
    controls->pasteFlashNewCheck->setChecked(m_config.value(QStringLiteral("paste/flashNew"), true).toBool());
    controls->pasteActivateNewCheck = new QCheckBox(QStringLiteral("激活"), newPasteRow);
    controls->pasteActivateNewCheck->setChecked(
        m_config.value(QStringLiteral("paste/activateNew"), true).toBool());
    newPasteLayout->addWidget(controls->pasteFlashNewCheck);
    newPasteLayout->addWidget(controls->pasteActivateNewCheck);
    newPasteLayout->addStretch();
    pasteBehaviorLayout->addWidget(newPasteRow);
    auto *restoreCountRow = new QWidget(pasteBehavior);
    auto *restoreCountLayout = new QHBoxLayout(restoreCountRow);
    restoreCountLayout->setContentsMargins(0, 0, 0, 0);
    restoreCountLayout->addWidget(new QLabel(QStringLiteral("可被恢复的贴图数:"), restoreCountRow));
    controls->pasteRestoreCountSpin = new QSpinBox(restoreCountRow);
    controls->pasteRestoreCountSpin->setRange(0, 99);
    controls->pasteRestoreCountSpin->setValue(
        m_config.value(QStringLiteral("paste/restoreCount"), 1).toInt());
    restoreCountLayout->addWidget(controls->pasteRestoreCountSpin);
    restoreCountLayout->addStretch();
    pasteBehaviorLayout->addWidget(restoreCountRow);
    auto *pasteBehaviorFooter = new QHBoxLayout;
    pasteBehaviorFooter->addStretch();
    auto *pasteBehaviorRestore = new QPushButton(QStringLiteral("恢复默认"), pasteBehavior);
    pasteBehaviorRestore->setFixedWidth(86);
    pasteBehaviorFooter->addWidget(pasteBehaviorRestore);
    pasteBehaviorLayout->addLayout(pasteBehaviorFooter);
    QObject::connect(pasteBehaviorRestore, &QPushButton::clicked, this, [=] {
        controls->pasteUseCapturePositionCheck->setChecked(true);
        controls->pasteWheelCenterCheck->setChecked(false);
        controls->pasteSmoothScaleCheck->setChecked(true);
        pasteVirtualDesktopCheck->setChecked(true);
        controls->pasteResetTransformCheck->setChecked(true);
        controls->pasteFlashTransparencyCheck->setChecked(true);
        controls->pasteFlashOnGroupSwitchCheck->setChecked(true);
        controls->pasteHideAllOnPasteKeyCheck->setChecked(false);
        controls->pasteKeepOnDragCheck->setChecked(false);
        pasteHideOnFileDialogCheck->setChecked(false);
        controls->pasteCloseConfirmCheck->setChecked(true);
        controls->pasteDestroyConfirmCheck->setChecked(true);
        controls->pasteFlashNewCheck->setChecked(true);
        controls->pasteActivateNewCheck->setChecked(true);
        controls->pasteRestoreCountSpin->setValue(1);
    });
    m_pageSavers.append([=, this] {
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
    escapeAction->setCurrentText(
        m_config.value(QStringLiteral("paste/escapeAction"), QStringLiteral("关闭贴图")).toString());
    pasteActionForm->addRow(QStringLiteral("Esc:"), escapeAction);
    auto *shiftEscapeAction = new QComboBox(pasteActionGroup);
    shiftEscapeAction->setObjectName(QStringLiteral("pasteShiftEscapeAction"));
    shiftEscapeAction->setFixedWidth(112);
    shiftEscapeAction->addItems(
        {QStringLiteral("销毁贴图"), QStringLiteral("关闭贴图"), QStringLiteral("无")});
    shiftEscapeAction->setCurrentText(
        m_config.value(QStringLiteral("paste/shiftEscapeAction"), QStringLiteral("销毁贴图")).toString());
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
    m_pageSavers.append([=, this] {
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
    controls->pasteTextToImageCheck = new QCheckBox(QStringLiteral("文字转图片"), pasteTransform);
    controls->pasteTextToImageCheck->setChecked(
        m_config.value(QStringLiteral("paste/textToImage"), true).toBool());
    pasteTransformForm->addRow(controls->pasteTextToImageCheck);
    controls->pasteIgnoreHtmlCheck = new QCheckBox(QStringLiteral("总是忽略 HTML 格式"), pasteTransform);
    controls->pasteIgnoreHtmlCheck->setChecked(
        m_config.value(QStringLiteral("paste/ignoreHtml"), false).toBool());
    pasteTransformForm->addRow(controls->pasteIgnoreHtmlCheck);
    controls->pastePathToImageCheck = new QCheckBox(QStringLiteral("不把文件路径转化成贴图"), pasteTransform);
    controls->pastePathToImageCheck->setChecked(
        m_config.value(QStringLiteral("paste/pathToImage"), true).toBool());
    pasteTransformForm->addRow(controls->pastePathToImageCheck);
    controls->pasteTextMarginSpin = new QSpinBox(pasteTransform);
    controls->pasteTextMarginSpin->setFixedWidth(92);
    controls->pasteTextMarginSpin->setRange(0, 100);
    controls->pasteTextMarginSpin->setSuffix(QStringLiteral(" 像素"));
    controls->pasteTextMarginSpin->setValue(m_config.value(QStringLiteral("paste/textMargin"), 12).toInt());
    pasteTransformForm->addRow(QStringLiteral("页边距:"), controls->pasteTextMarginSpin);
    controls->pasteTextMaxWidthSpin = new QSpinBox(pasteTransform);
    controls->pasteTextMaxWidthSpin->setFixedWidth(112);
    controls->pasteTextMaxWidthSpin->setRange(100, 10000);
    controls->pasteTextMaxWidthSpin->setSuffix(QStringLiteral(" 像素"));
    controls->pasteTextMaxWidthSpin->setValue(
        m_config.value(QStringLiteral("paste/textMaxWidth"), 750).toInt());
    pasteTransformForm->addRow(QStringLiteral("图片最大宽度:"), controls->pasteTextMaxWidthSpin);
    controls->pasteTextFontEdit = new QLineEdit(
        m_config.value(QStringLiteral("paste/textFont"), QStringLiteral("Microsoft YaHei UI, 11")).toString(),
        pasteTransform);
    controls->pasteTextFontEdit->setFixedWidth(190);
    controls->pasteTextFontEdit->setReadOnly(true);
    pasteTransformForm->addRow(QStringLiteral("字体:"), controls->pasteTextFontEdit);
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
    textButton->setProperty("color",
                            textForeground.isValid() ? textForeground : QColor(QStringLiteral("#111111")));
    backgroundButton->setStyleSheet(
        QStringLiteral("background-color: %1; border: 1px solid #999999;")
            .arg(backgroundButton->property("color").value<QColor>().name(QColor::HexArgb)));
    textButton->setStyleSheet(
        QStringLiteral("background-color: %1; color: #ffffff; border: 1px solid #999999;")
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
    const auto chooseTextColor = [=, this](QPushButton *button, const QString &title) {
        QObject::connect(button, &QPushButton::clicked, this, [button, title, updateTextPreview] {
            const QColor current = button->property("color").value<QColor>();
            const QColor selected =
                QColorDialog::getColor(current, button, title, QColorDialog::ShowAlphaChannel);
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
    m_pageSavers.append([=, this] {
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
    QObject::connect(pasteTransformRestore, &QPushButton::clicked, this, [=] {
        controls->pasteTextToImageCheck->setChecked(true);
        controls->pasteIgnoreHtmlCheck->setChecked(false);
        controls->pastePathToImageCheck->setChecked(true);
        controls->pasteTextMarginSpin->setValue(12);
        controls->pasteTextMaxWidthSpin->setValue(750);
        controls->pasteTextFontEdit->setText(QStringLiteral("Microsoft YaHei UI, 11"));
        backgroundButton->setProperty("color", QColor(QStringLiteral("#ffffff")));
        textButton->setProperty("color", QColor(QStringLiteral("#111111")));
        backgroundButton->setStyleSheet(
            QStringLiteral("background-color: #ffffff; border: 1px solid #999999;"));
        textButton->setStyleSheet(
            QStringLiteral("background-color: #111111; color: #ffffff; border: 1px solid #999999;"));
        updateTextPreview();
    });
    pasteTabs->addTab(pasteTransform, QStringLiteral("转换"));
    pasteLayout->addWidget(pasteTabs);
    m_pageSavers.append([=, this] {
        m_config.setValue(QStringLiteral("paste/shadow"), controls->pasteShadowCheck->isChecked());
        m_config.setValue(QStringLiteral("paste/helpKeys"), controls->pasteHelpKeysCheck->isChecked());
        m_config.setValue(QStringLiteral("paste/opacity"), controls->pasteOpacitySpin->value());
        m_config.setValue(QStringLiteral("paste/maxSize"), controls->pasteMaxSizeSpin->value());
        m_config.setValue(QStringLiteral("paste/thumbWidth"), controls->pasteThumbWidthSpin->value());
        m_config.setValue(QStringLiteral("paste/thumbHeight"), controls->pasteThumbHeightSpin->value());
        m_config.setValue(QStringLiteral("paste/useCapturePosition"),
                          controls->pasteUseCapturePositionCheck->isChecked());
        m_config.setValue(QStringLiteral("paste/wheelCenter"), controls->pasteWheelCenterCheck->isChecked());
        m_config.setValue(QStringLiteral("paste/smoothScale"), controls->pasteSmoothScaleCheck->isChecked());
        m_config.setValue(QStringLiteral("paste/resetTransform"),
                          controls->pasteResetTransformCheck->isChecked());
        m_config.setValue(QStringLiteral("paste/flashTransparency"),
                          controls->pasteFlashTransparencyCheck->isChecked());
        m_config.setValue(QStringLiteral("paste/flashGroupSwitch"),
                          controls->pasteFlashOnGroupSwitchCheck->isChecked());
        m_config.setValue(QStringLiteral("paste/hideAllOnPasteKey"),
                          controls->pasteHideAllOnPasteKeyCheck->isChecked());
        m_config.setValue(QStringLiteral("paste/keepOnDrag"), controls->pasteKeepOnDragCheck->isChecked());
        m_config.setValue(QStringLiteral("paste/closeConfirm"),
                          controls->pasteCloseConfirmCheck->isChecked());
        m_config.setValue(QStringLiteral("paste/destroyConfirm"),
                          controls->pasteDestroyConfirmCheck->isChecked());
        m_config.setValue(QStringLiteral("paste/flashNew"), controls->pasteFlashNewCheck->isChecked());
        m_config.setValue(QStringLiteral("paste/activateNew"), controls->pasteActivateNewCheck->isChecked());
        m_config.setValue(QStringLiteral("paste/restoreCount"), controls->pasteRestoreCountSpin->value());
        m_config.setValue(QStringLiteral("paste/textToImage"), controls->pasteTextToImageCheck->isChecked());
        m_config.setValue(QStringLiteral("paste/ignoreHtml"), controls->pasteIgnoreHtmlCheck->isChecked());
        m_config.setValue(QStringLiteral("paste/pathToImage"), controls->pastePathToImageCheck->isChecked());
        m_config.setValue(QStringLiteral("paste/textMargin"), controls->pasteTextMarginSpin->value());
        m_config.setValue(QStringLiteral("paste/textMaxWidth"), controls->pasteTextMaxWidthSpin->value());
        m_config.setValue(QStringLiteral("paste/textFont"), controls->pasteTextFontEdit->text());
    });
    return pastePage;
}
} // namespace snipaste::ui
