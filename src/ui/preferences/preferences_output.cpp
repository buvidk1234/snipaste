#include "ui/preferences/preferences_dialog.h"

#include "core/config/config_store.h"
#include "platform/keyboard.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStyle>
#include <QTabWidget>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <memory>

#include "core/command/argument_parser.h"
#include "ui/preferences/preference_widgets.h"

namespace snipaste::ui
{
using namespace preferences;
QWidget *PreferencesDialog::createOutputPage(QWidget *parent)
{
    struct PageControls
    {
        QString quickSaveDirectory;
        QString autoSaveDirectory;
        QCheckBox *autoSaveCheck = nullptr;
        QCheckBox *outputRememberExtensionCheck = nullptr;
        QCheckBox *outputQuickSaveNotificationCheck = nullptr;
        QCheckBox *outputCopyWindowsBitmapCheck = nullptr;
        QCheckBox *outputCopyScreenshotCheck = nullptr;
        QCheckBox *outputCopyPasteCheck = nullptr;
        QSpinBox *outputQualitySpin = nullptr;
        QLineEdit *saveDirectoryEdit = nullptr;
        QLineEdit *outputFileNameEdit = nullptr;
        QLineEdit *outputQuickSavePreviewEdit = nullptr;
        QLineEdit *outputAutoSavePathEdit = nullptr;
        QRadioButton *outputCopyAsFileAlwaysRadio = nullptr;
        QRadioButton *outputCopyAsFileShortcutRadio = nullptr;
        QRadioButton *outputCopyAsFileDisabledRadio = nullptr;
    };
    auto controls = std::make_shared<PageControls>();
    auto *outputPage = new QWidget(parent);
    auto *outputLayout = new QVBoxLayout(outputPage);
    auto *outputTabs = new QTabWidget(outputPage);
    outputTabs->setObjectName(QStringLiteral("outputTabs"));

    auto *fileOutput = new QWidget(outputTabs);
    auto *fileLayout = new QVBoxLayout(fileOutput);
    fileLayout->setContentsMargins(6, 6, 6, 6);

    const QString defaultFileName = defaultOutputFileNamePattern();
    const QString desktopDirectory = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    const QString picturesDirectory = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    controls->quickSaveDirectory =
        m_config.value(QStringLiteral("output/saveDirectory"), desktopDirectory).toString();
    if (controls->quickSaveDirectory.trimmed().isEmpty())
        controls->quickSaveDirectory = desktopDirectory;
    controls->autoSaveDirectory =
        m_config.value(QStringLiteral("output/autoSaveDirectory"), picturesDirectory).toString();
    if (controls->autoSaveDirectory.trimmed().isEmpty())
        controls->autoSaveDirectory = picturesDirectory;

    auto *qualityRow = new QWidget(fileOutput);
    auto *qualityLayout = new QHBoxLayout(qualityRow);
    qualityLayout->setContentsMargins(0, 0, 0, 0);
    qualityLayout->addWidget(new QLabel(QStringLiteral("图像质量:"), qualityRow));
    controls->outputQualitySpin = new QSpinBox(qualityRow);
    controls->outputQualitySpin->setObjectName(QStringLiteral("outputImageQuality"));
    controls->outputQualitySpin->setFixedWidth(72);
    controls->outputQualitySpin->setRange(-1, 100);
    controls->outputQualitySpin->setValue(m_config.value(QStringLiteral("output/imageQuality"), -1).toInt());
    auto *qualityInfo = makeInfoButton(qualityRow);
    qualityLayout->addWidget(controls->outputQualitySpin);
    qualityLayout->addWidget(qualityInfo);
    qualityLayout->addStretch();
    fileLayout->addWidget(qualityRow);

    auto *manualGroup = new QGroupBox(QStringLiteral("手动保存"), fileOutput);
    auto *manualForm = new QFormLayout(manualGroup);
    manualForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    manualForm->setHorizontalSpacing(10);
    manualForm->setVerticalSpacing(7);
    controls->outputFileNameEdit = new QLineEdit(
        m_config.value(QStringLiteral("output/fileNamePattern"), defaultFileName).toString(), manualGroup);
    manualForm->addRow(QStringLiteral("文件名:"), controls->outputFileNameEdit);
    auto *manualPreview = new QLineEdit(manualGroup);
    manualPreview->setReadOnly(true);
    manualPreview->setMinimumHeight(26);
    manualPreview->setStyleSheet(QStringLiteral("color: #666666; background: #ffffff; padding: 3px;"));
    manualForm->addRow(QStringLiteral("预览:"), manualPreview);
    controls->outputRememberExtensionCheck =
        new QCheckBox(QStringLiteral("记住上一次使用的图片扩展名"), manualGroup);
    controls->outputRememberExtensionCheck->setChecked(
        m_config.value(QStringLiteral("output/rememberExtension"), true).toBool());
    manualForm->addRow(controls->outputRememberExtensionCheck);
    fileLayout->addWidget(manualGroup);

    auto *quickGroup = new QGroupBox(QStringLiteral("快捷保存"), fileOutput);
    auto *quickForm = new QFormLayout(quickGroup);
    quickForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    quickForm->setHorizontalSpacing(10);
    quickForm->setVerticalSpacing(7);
    controls->outputQuickSaveNotificationCheck = new QCheckBox(QStringLiteral("显示通知"), quickGroup);
    controls->outputQuickSaveNotificationCheck->setChecked(
        m_config.value(QStringLiteral("output/quickSaveNotification"), true).toBool());
    quickForm->addRow(controls->outputQuickSaveNotificationCheck);
    auto *quickPathRow = new QWidget(quickGroup);
    auto *quickPathLayout = new QHBoxLayout(quickPathRow);
    quickPathLayout->setContentsMargins(0, 0, 0, 0);
    controls->saveDirectoryEdit = new QLineEdit(quickPathRow);
    controls->saveDirectoryEdit->setReadOnly(true);
    auto *quickOpenFolder = new QPushButton(QStringLiteral("打开文件夹"), quickPathRow);
    auto *quickChangeFolder = new QPushButton(QStringLiteral("更改文件夹"), quickPathRow);
    quickPathLayout->addWidget(controls->saveDirectoryEdit, 1);
    quickPathLayout->addWidget(quickOpenFolder);
    quickPathLayout->addWidget(quickChangeFolder);
    quickForm->addRow(QStringLiteral("路径:"), quickPathRow);
    controls->outputQuickSavePreviewEdit = new QLineEdit(quickGroup);
    controls->outputQuickSavePreviewEdit->setReadOnly(true);
    quickForm->addRow(QStringLiteral("预览:"), controls->outputQuickSavePreviewEdit);
    auto *quickInfoRow = new QHBoxLayout;
    quickInfoRow->addWidget(makeInfoButton(quickGroup));
    quickInfoRow->addStretch();
    quickForm->addRow(quickInfoRow);
    fileLayout->addWidget(quickGroup);

    auto *autoGroup = new QGroupBox(fileOutput);
    auto *autoLayout = new QVBoxLayout(autoGroup);
    autoLayout->setContentsMargins(8, 8, 8, 8);
    controls->autoSaveCheck = new QCheckBox(QStringLiteral("自动保存"), autoGroup);
    controls->autoSaveCheck->setObjectName(QStringLiteral("outputAutoSave"));
    controls->autoSaveCheck->setChecked(m_config.value(QStringLiteral("capture/autoSave"), false).toBool());
    autoLayout->addWidget(controls->autoSaveCheck);
    auto *autoPathRow = new QWidget(autoGroup);
    autoPathRow->setEnabled(controls->autoSaveCheck->isChecked());
    auto *autoPathLayout = new QHBoxLayout(autoPathRow);
    autoPathLayout->setContentsMargins(0, 0, 0, 0);
    controls->outputAutoSavePathEdit = new QLineEdit(autoPathRow);
    controls->outputAutoSavePathEdit->setReadOnly(true);
    auto *autoOpenFolder = new QPushButton(QStringLiteral("打开文件夹"), autoPathRow);
    auto *autoChangeFolder = new QPushButton(QStringLiteral("更改文件夹"), autoPathRow);
    autoPathLayout->addWidget(controls->outputAutoSavePathEdit, 1);
    autoPathLayout->addWidget(autoOpenFolder);
    autoPathLayout->addWidget(autoChangeFolder);
    autoLayout->addWidget(new QLabel(QStringLiteral("路径:"), autoGroup));
    autoLayout->addWidget(autoPathRow);
    auto *autoPreviewRow = new QWidget(autoGroup);
    autoPreviewRow->setEnabled(controls->autoSaveCheck->isChecked());
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
    fileRestoreButton->setObjectName(QStringLiteral("outputFileRestore"));
    outputFileFooter->addWidget(namingRuleButton);
    outputFileFooter->addStretch();
    outputFileFooter->addWidget(fileRestoreButton);
    fileLayout->addLayout(outputFileFooter);

    const auto updateOutputPreviews = [=] {
        const QString pattern = controls->outputFileNameEdit->text().trimmed().isEmpty()
                                    ? defaultOutputFileNamePattern()
                                    : controls->outputFileNameEdit->text().trimmed();
        const QDateTime now = QDateTime::currentDateTime();
        const QString previewName = core::expandOutputFilePattern(pattern, now);
        manualPreview->setText(previewName);
        controls->saveDirectoryEdit->setText(outputPathForDirectory(controls->quickSaveDirectory, pattern));
        controls->saveDirectoryEdit->setCursorPosition(0);
        controls->outputQuickSavePreviewEdit->setText(
            outputPathForDirectory(controls->quickSaveDirectory, previewName));
        controls->outputQuickSavePreviewEdit->setCursorPosition(0);
        controls->outputAutoSavePathEdit->setText(
            outputPathForDirectory(controls->autoSaveDirectory, pattern));
        controls->outputAutoSavePathEdit->setCursorPosition(0);
        autoPreview->setText(outputPathForDirectory(controls->autoSaveDirectory, previewName));
        autoPreview->setCursorPosition(0);
        autoPreviewRow->setEnabled(controls->autoSaveCheck->isChecked());
    };
    updateOutputPreviews();
    QObject::connect(controls->outputFileNameEdit, &QLineEdit::textChanged, this,
                     [updateOutputPreviews](const QString &) { updateOutputPreviews(); });
    QObject::connect(controls->autoSaveCheck, &QCheckBox::toggled, this,
                     [autoPathRow, autoPreviewRow](bool checked) {
                         autoPathRow->setEnabled(checked);
                         autoPreviewRow->setEnabled(checked);
                     });
    QObject::connect(qualityInfo, &QPushButton::clicked, this, [=, this] {
        QMessageBox::information(this, QStringLiteral("图像质量"),
                                 QStringLiteral("-1 使用系统默认质量；0 到 100 可指定 JPEG 质量。"));
    });
    QObject::connect(namingRuleButton, &QPushButton::clicked, this, [=, this] {
        QMessageBox::information(this, QStringLiteral("命名规则"),
                                 QStringLiteral("文件名中的 $yyyy-MM-dd_HH-mm-ss$ 会替换为当前日期和时间。"));
    });
    QObject::connect(quickOpenFolder, &QPushButton::clicked, this,
                     [=] { QDesktopServices::openUrl(QUrl::fromLocalFile(controls->quickSaveDirectory)); });
    QObject::connect(quickChangeFolder, &QPushButton::clicked, this, [=, this] {
        const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("选择快捷保存目录"),
                                                               controls->quickSaveDirectory);
        if (!path.isEmpty())
        {
            controls->quickSaveDirectory = path;
            updateOutputPreviews();
        }
    });
    QObject::connect(autoOpenFolder, &QPushButton::clicked, this,
                     [=] { QDesktopServices::openUrl(QUrl::fromLocalFile(controls->autoSaveDirectory)); });
    QObject::connect(autoChangeFolder, &QPushButton::clicked, this, [=, this] {
        const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("选择自动保存目录"),
                                                               controls->autoSaveDirectory);
        if (!path.isEmpty())
        {
            controls->autoSaveDirectory = path;
            updateOutputPreviews();
        }
    });
    const auto restoreOutputDefaults = [=] {
        controls->outputQualitySpin->setValue(-1);
        controls->outputFileNameEdit->setText(defaultOutputFileNamePattern());
        controls->outputRememberExtensionCheck->setChecked(true);
        controls->outputQuickSaveNotificationCheck->setChecked(true);
        controls->autoSaveCheck->setChecked(false);
        controls->outputCopyWindowsBitmapCheck->setChecked(true);
        controls->outputCopyScreenshotCheck->setChecked(true);
        controls->outputCopyPasteCheck->setChecked(false);
        controls->outputCopyAsFileShortcutRadio->setChecked(true);
        controls->quickSaveDirectory = desktopDirectory;
        controls->autoSaveDirectory = picturesDirectory;
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
    controls->outputCopyWindowsBitmapCheck =
        new QCheckBox(QStringLiteral("以 Windows Bitmap 格式复制图片:"), bitmapRow);
    controls->outputCopyWindowsBitmapCheck->setChecked(
        m_config.value(QStringLiteral("output/clipboardWindowsBitmap"), true).toBool());
#if defined(Q_OS_MACOS)
    // CF_DIB is a Windows clipboard compatibility format and is not exposed
    // by the Cocoa clipboard backend.
    controls->outputCopyWindowsBitmapCheck->hide();
    controls->outputCopyWindowsBitmapCheck->setChecked(false);
#endif
    controls->outputCopyScreenshotCheck = new QCheckBox(QStringLiteral("截图"), bitmapRow);
    controls->outputCopyScreenshotCheck->setChecked(
        m_config.value(QStringLiteral("output/clipboardScreenshot"), true).toBool());
    controls->outputCopyPasteCheck = new QCheckBox(QStringLiteral("贴图"), bitmapRow);
    controls->outputCopyPasteCheck->setChecked(
        m_config.value(QStringLiteral("output/clipboardPaste"), false).toBool());
    bitmapLayout->addWidget(controls->outputCopyWindowsBitmapCheck);
    bitmapLayout->addWidget(controls->outputCopyScreenshotCheck);
    bitmapLayout->addWidget(controls->outputCopyPasteCheck);
    bitmapLayout->addWidget(makeInfoButton(bitmapRow));
    bitmapLayout->addStretch();
    clipboardLayout->addWidget(bitmapRow);
    auto *copyFileRow = new QWidget(clipboardOutput);
    auto *copyFileLayout = new QHBoxLayout(copyFileRow);
    copyFileLayout->setContentsMargins(0, 0, 0, 0);
    copyFileLayout->addWidget(new QLabel(QStringLiteral("复制图像为文件:"), copyFileRow));
    controls->outputCopyAsFileAlwaysRadio = new QRadioButton(QStringLiteral("总是"), copyFileRow);
    controls->outputCopyAsFileShortcutRadio = new QRadioButton(
        QStringLiteral("%1 + Alt + S").arg(snipaste::platform::primaryModifierText()), copyFileRow);
    controls->outputCopyAsFileDisabledRadio = new QRadioButton(QStringLiteral("禁用"), copyFileRow);
    copyFileLayout->addWidget(controls->outputCopyAsFileAlwaysRadio);
    copyFileLayout->addWidget(controls->outputCopyAsFileShortcutRadio);
    copyFileLayout->addWidget(controls->outputCopyAsFileDisabledRadio);
    copyFileLayout->addStretch();
    const int copyAsFileMode = m_config.value(QStringLiteral("output/copyAsFileMode"), 1).toInt();
    (copyAsFileMode == 0   ? controls->outputCopyAsFileAlwaysRadio
     : copyAsFileMode == 1 ? controls->outputCopyAsFileShortcutRadio
                           : controls->outputCopyAsFileDisabledRadio)
        ->setChecked(true);
    clipboardLayout->addWidget(copyFileRow);
    auto *clipboardFooter = new QHBoxLayout;
    clipboardFooter->addStretch();
    auto *clipboardRestore = new QPushButton(QStringLiteral("恢复默认"), clipboardOutput);
    clipboardRestore->setObjectName(QStringLiteral("outputClipboardRestore"));
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
    ocrEngine->setCurrentText(
        m_config.value(QStringLiteral("output/ocrEngine"), QStringLiteral("系统自带")).toString());
    ocrEngineRow->addWidget(ocrEngine);
    ocrEngineRow->addStretch();
    textRecognitionLayout->addLayout(ocrEngineRow);

    auto *ocrLanguageRow = new QHBoxLayout;
    ocrLanguageRow->addWidget(new QLabel(QStringLiteral("首选语言:"), textRecognition));
    auto *ocrLanguage = new QComboBox(textRecognition);
    ocrLanguage->setObjectName(QStringLiteral("ocrLanguage"));
    ocrLanguage->setFixedWidth(180);
    ocrLanguage->addItems(
        {QStringLiteral("系统默认"), QStringLiteral("简体中文"), QStringLiteral("English")});
    ocrLanguage->setCurrentText(
        m_config.value(QStringLiteral("output/ocrLanguage"), QStringLiteral("系统默认")).toString());
    ocrLanguageRow->addWidget(ocrLanguage);
    ocrLanguageRow->addStretch();
    textRecognitionLayout->addLayout(ocrLanguageRow);

    auto *ocrNote = new QLabel(
#if defined(Q_OS_MACOS)
        QStringLiteral("* 使用 macOS Vision；无结果时回退到 Tesseract")
#else
        QStringLiteral("* 需要 Windows 10 版本 1903 或更高版本")
#endif
            ,
        textRecognition);
    ocrNote->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    textRecognitionLayout->addWidget(ocrNote);
    textRecognitionLayout->addStretch();
    m_pageSavers.append([=, this] {
        m_config.setValue(QStringLiteral("output/ocrSound"), ocrSoundCheck->isChecked());
        m_config.setValue(QStringLiteral("output/ocrEngine"), ocrEngine->currentText());
        m_config.setValue(QStringLiteral("output/ocrLanguage"), ocrLanguage->currentText());
    });
    outputTabs->addTab(textRecognition, QStringLiteral("文字识别"));
    outputLayout->addWidget(outputTabs);
    m_pageSavers.append([=, this] {
        m_config.setValue(QStringLiteral("capture/autoSave"), controls->autoSaveCheck->isChecked());
        m_config.setValue(QStringLiteral("output/imageQuality"), controls->outputQualitySpin->value());
        m_config.setValue(QStringLiteral("output/fileNamePattern"), controls->outputFileNameEdit->text());
        m_config.setValue(QStringLiteral("output/rememberExtension"),
                          controls->outputRememberExtensionCheck->isChecked());
        m_config.setValue(QStringLiteral("output/quickSaveNotification"),
                          controls->outputQuickSaveNotificationCheck->isChecked());
        m_config.setValue(QStringLiteral("output/saveDirectory"),
                          outputDirectoryFromPath(controls->saveDirectoryEdit->text()));
        m_config.setValue(QStringLiteral("output/autoSaveDirectory"),
                          outputDirectoryFromPath(controls->outputAutoSavePathEdit->text()));
        m_config.setValue(QStringLiteral("output/clipboardWindowsBitmap"),
                          controls->outputCopyWindowsBitmapCheck->isChecked());
        m_config.setValue(QStringLiteral("output/clipboardScreenshot"),
                          controls->outputCopyScreenshotCheck->isChecked());
        m_config.setValue(QStringLiteral("output/clipboardPaste"),
                          controls->outputCopyPasteCheck->isChecked());
        m_config.setValue(QStringLiteral("output/copyAsFileMode"),
                          controls->outputCopyAsFileAlwaysRadio->isChecked()     ? 0
                          : controls->outputCopyAsFileShortcutRadio->isChecked() ? 1
                                                                                 : 2);
    });
    return outputPage;
}
} // namespace snipaste::ui
