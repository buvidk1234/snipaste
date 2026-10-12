#pragma once

#include <QDialog>
#include <QVector>
#include <functional>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QKeySequenceEdit;
class QRadioButton;
class QSpinBox;

namespace snipate::core {
class ConfigStore;
}

namespace snipate::ui {

class PreferencesDialog final : public QDialog
{
public:
    explicit PreferencesDialog(core::ConfigStore &config, QWidget *parent = nullptr);

protected:
    void accept() override;
    void reject() override;

private:
    QWidget *createGeneralPage(QWidget *parent);
    QWidget *createInterfacePage(QWidget *parent);
    QWidget *createCapturePage(QWidget *parent);
    int px(int physicalPixels) const;
    QVector<std::function<void()>> m_pageSavers;
    core::ConfigStore &m_config;
    QString m_quickSaveDirectory;
    QString m_autoSaveDirectory;
    QCheckBox *m_startupCheck = nullptr;
    QCheckBox *m_backupCheck = nullptr;
    QCheckBox *m_quickResponseCheck = nullptr;
    QCheckBox *m_extendedTrayCheck = nullptr;
    QCheckBox *m_captureCursorCheck = nullptr;
    QCheckBox *m_captureShowAnchorCheck = nullptr;
    QCheckBox *m_captureMagnifierMaskCheck = nullptr;
    QCheckBox *m_captureMagnifierBorderCheck = nullptr;
    QCheckBox *m_captureMagnifierAnchorCheck = nullptr;
    QCheckBox *m_captureCrosshairCheck = nullptr;
    QCheckBox *m_captureDisableGuidesCheck = nullptr;
    QCheckBox *m_captureShortcutHintsCheck = nullptr;
    QCheckBox *m_captureAutoDetectWindowCheck = nullptr;
    QCheckBox *m_captureHideOneByOneCheck = nullptr;
    QCheckBox *m_captureAutoDetectElementsCheck = nullptr;
    QCheckBox *m_captureDisableCheck = nullptr;
    QCheckBox *m_captureAdjustHdrCheck = nullptr;
    QCheckBox *m_captureExitOnFocusCheck = nullptr;
    QCheckBox *m_captureSoundCheck = nullptr;
    QCheckBox *m_pasteShadowCheck = nullptr;
    QCheckBox *m_pasteHelpKeysCheck = nullptr;
    QCheckBox *m_pasteUseCapturePositionCheck = nullptr;
    QCheckBox *m_pasteWheelCenterCheck = nullptr;
    QCheckBox *m_pasteSmoothScaleCheck = nullptr;
    QCheckBox *m_pasteResetTransformCheck = nullptr;
    QCheckBox *m_pasteFlashTransparencyCheck = nullptr;
    QCheckBox *m_pasteFlashOnGroupSwitchCheck = nullptr;
    QCheckBox *m_pasteHideAllOnPasteKeyCheck = nullptr;
    QCheckBox *m_pasteKeepOnDragCheck = nullptr;
    QCheckBox *m_pasteCloseConfirmCheck = nullptr;
    QCheckBox *m_pasteDestroyConfirmCheck = nullptr;
    QCheckBox *m_pasteFlashNewCheck = nullptr;
    QCheckBox *m_pasteActivateNewCheck = nullptr;
    QCheckBox *m_pasteTextToImageCheck = nullptr;
    QCheckBox *m_pasteIgnoreHtmlCheck = nullptr;
    QCheckBox *m_pastePathToImageCheck = nullptr;
    QCheckBox *m_autoSaveCheck = nullptr;
    QCheckBox *m_outputRememberExtensionCheck = nullptr;
    QCheckBox *m_outputQuickSaveNotificationCheck = nullptr;
    QCheckBox *m_outputCopyWindowsBitmapCheck = nullptr;
    QCheckBox *m_outputCopyScreenshotCheck = nullptr;
    QCheckBox *m_outputCopyPasteCheck = nullptr;
    QSpinBox *m_borderWidthSpin = nullptr;
    QSpinBox *m_captureHistoryRegionSpin = nullptr;
    QSpinBox *m_captureHistorySpin = nullptr;
    QSpinBox *m_pasteOpacitySpin = nullptr;
    QSpinBox *m_pasteMaxSizeSpin = nullptr;
    QSpinBox *m_pasteThumbWidthSpin = nullptr;
    QSpinBox *m_pasteThumbHeightSpin = nullptr;
    QSpinBox *m_pasteRestoreCountSpin = nullptr;
    QSpinBox *m_pasteTextMarginSpin = nullptr;
    QSpinBox *m_pasteTextMaxWidthSpin = nullptr;
    QSpinBox *m_outputQualitySpin = nullptr;
    QLineEdit *m_saveDirectoryEdit = nullptr;
    QLineEdit *m_outputFileNameEdit = nullptr;
    QLineEdit *m_outputQuickSavePreviewEdit = nullptr;
    QLineEdit *m_outputAutoSavePathEdit = nullptr;
    QLineEdit *m_captureSoundFileEdit = nullptr;
    QLineEdit *m_pasteTextFontEdit = nullptr;
    QLineEdit *m_ignoreApplicationsEdit = nullptr;
    QKeySequenceEdit *m_snipHotkeyEdit = nullptr;
    QKeySequenceEdit *m_copyHotkeyEdit = nullptr;
    QKeySequenceEdit *m_pasteHotkeyEdit = nullptr;
    QRadioButton *m_outputCopyAsFileAlwaysRadio = nullptr;
    QRadioButton *m_outputCopyAsFileShortcutRadio = nullptr;
    QRadioButton *m_outputCopyAsFileDisabledRadio = nullptr;
    QVector<QComboBox *> m_controlMouseCombos;
    QVector<QLineEdit *> m_controlTrayCommands;
    QVector<QLineEdit *> m_controlCornerCommands;
};

} // namespace snipate::ui
