#include "core/config/config_store.h"
#include "core/control/hot_corner_state.h"
#include "ui/annotation/annotation_canvas.h"
#include "ui/annotation/image_region.h"
#include "ui/preferences/preferences_dialog.h"
#include "ui/paste/paste_manager.h"
#include "ui/paste/paste_window.h"
#include "ui/snip/snip_overlay.h"
#include "ui/snip/scroll_capture_panel.h"
#include "ui/snip/scroll_capture_preview.h"
#include "ui/clipboard_output.h"
#include "ui/tray/tray_controller.h"
#include "ui/history/screenshot_history_dialog.h"
#include "core/command/command_registry.h"
#include "platform/keyboard.h"

#include <QApplication>
#include <QBackingStore>
#include <QCheckBox>
#include <QColor>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFrame>
#include <QFontDatabase>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QDateTime>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QAction>
#include <QScreen>
#include <QSignalSpy>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedLayout>
#include <QGroupBox>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QWheelEvent>
#include <memory>

using namespace snipaste;

class UiRegressionTest : public QObject
{
    Q_OBJECT
private slots:
    void preferencesUpdateCheckUsesCurrentSettings();
    void preferencesCloseSavesWithoutQuitting();
    void preferencesPendingCallbacksAreCancelled();
    void preferencesProfessionalReferencePages();
    void copyAsFileModesProduceExpectedClipboardFormats();
    void overlayCopyButtonHonorsCopyAsFileMode();
    void scrollCapturePanelUsesFloatingToolbar();
    void scrollCapturePreviewShowsStitchedImage();
    void trayContextMenuMatchesReference();
    void hotCornerDwellState();
    void toolbarOnlyAfterSelection();
    void preparedOverlayReusesControlsAndReleasesPixels();
    void annotationExportSharesCommittedCache();
    void annotationInvalidationReleasesPixels();
    void magnifierRegionPreservesFullFramePixels();
    void historyDialogLoadsPreviewsAsynchronously();
    void proGroupedToolsAndStyles();
    void proSizeRefreshAndOutput();
    void proHistoryAndSelectionShortcuts();
    void historyRestoresPairedGeometryAndBackground();
    void legacyHistoryUsesNeutralPreview();
    void proLayoutAndAnnotations();
    void magnifierFramesMoveIndependently();
    void autoCopySkipsToolbar();
    void inlineAnnotationsReachClipboard();
    void annotationStylesAndEffectsSurviveUndo();
    void brushCursorsRemainDistinct();
    void mosaicCoverageMatchesBrushDiameter();
    void mosaicRendersDuringDrag();
    void mosaicEffectShapesUseOneAlgorithm();
    void brushesContinueOverExistingStrokes_data();
    void brushesContinueOverExistingStrokes();
    void eraserRemovesOnlyCoveredStrokeRegion();
    void eraserRemovesOnlyCoveredShapeRegion();
    void eraserRemovesMagnifierInteriorLocally();
    void annotationHistoryMixedOperations();
    void annotationHistoryShapeEdits();
    void annotationHistoryClearAndBranch();
    void annotationHistoryCaptureOffset();
    void annotationHistoryNumberSequence();
    void inlineTextInputAndCancel();
    void inlineTextOutput_data();
    void inlineTextOutput();
    void textEditorCleanupAndToolSwitch();
    void hoverTargetLocksUntilReset();
    void selectionBorderRemainsVisibleAfterConfirm();
    void manualDragOverridesDetectedTarget();
    void detectionModesAndColorShortcuts();
    void ctrlASelectsCurrentScreenOnFirstPress();
    void resizeSelectionEdgesAndCorners_data();
    void resizeSelectionEdgesAndCorners();
    void resizePreservesAnnotationsAndUndo();
    void moveSelectionAndClampToScreen();
    void pastePreservesOriginalPixels_data();
    void pastePreservesOriginalPixels();
    void pasteDefaultsOpaque();
    void pasteOutlineSurvivesFlashAndZoom_data();
    void pasteOutlineSurvivesFlashAndZoom();
    void pasteDisplayKeepsNativePixels_data();
    void pasteDisplayKeepsNativePixels();
    void pasteSelectionClipboardSize_data();
    void pasteSelectionClipboardSize();
    void pasteCaptureOriginAndClipboardIsolation();
    void pasteClickAndDragCursor();
    void pasteDoubleClickRestores();
    void pasteRecoveryLimitAndDestruction();
    void pasteNewClipboardTakesPriority();
    void pasteGroupOrdering();
    void pastePersistence();
    void pastePersistenceReusesPixelsAndHandlesFailure();
    void independentWidthPresetsAndCornerRadius();
};

void UiRegressionTest::preferencesUpdateCheckUsesCurrentSettings()
{
    QTemporaryDir directory;
    core::ConfigStore config(directory.filePath(QStringLiteral("config.ini")));
    QVERIFY(config.load());
    ui::PreferencesDialog dialog(config);
    int requests = 0;
    dialog.setUpdateCheckHandler([&](QWidget *parent) {
        QCOMPARE(parent, &dialog);
        QVERIFY(config.value(QStringLiteral("update/includeBeta")).toBool());
        QCOMPARE(config.value(QStringLiteral("update/proxyServer")).toString(), QStringLiteral("127.0.0.1"));
        QCOMPARE(config.value(QStringLiteral("update/proxyPort")).toInt(), 8080);
        ++requests;
    });
    auto *beta = dialog.findChild<QCheckBox *>(QStringLiteral("updateIncludeBeta"));
    auto *server = dialog.findChild<QLineEdit *>(QStringLiteral("updateProxyServer"));
    auto *port = dialog.findChild<QSpinBox *>(QStringLiteral("updateProxyPort"));
    auto *check = dialog.findChild<QPushButton *>(QStringLiteral("updateCheckNow"));
    QVERIFY(beta && server && port && check);
    beta->setChecked(true);
    server->setText(QStringLiteral("127.0.0.1"));
    port->setValue(8080);
    check->click();
    QCOMPARE(requests, 1);
}

void UiRegressionTest::copyAsFileModesProduceExpectedClipboardFormats()
{
    QImage image(QSize(7, 5), QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(12, 34, 56, 255));
    auto *clipboard = QApplication::clipboard();
    QVERIFY(clipboard);

    const auto verify = [&](ui::CopyAsFileMode mode, bool shortcut, bool expectFile) {
        clipboard->clear();
        QVERIFY(ui::copyImageToClipboard(image, mode, shortcut));
        const QMimeData *mime = clipboard->mimeData();
        QVERIFY(mime);
        QVERIFY(mime->hasImage());
        QCOMPARE(mime->hasUrls(), expectFile);
        if (expectFile) {
            QCOMPARE(mime->urls().size(), 1);
            const QString path = mime->urls().first().toLocalFile();
            QVERIFY(!path.isEmpty());
            QVERIFY(QFileInfo::exists(path));
            QVERIFY(QFile(path).remove());
        }
    };

    verify(ui::CopyAsFileMode::Disabled, false, false);
    verify(ui::CopyAsFileMode::Shortcut, false, false);
    verify(ui::CopyAsFileMode::Shortcut, true, true);
    verify(ui::CopyAsFileMode::Always, false, true);
    clipboard->clear();
}

void UiRegressionTest::overlayCopyButtonHonorsCopyAsFileMode()
{
    auto *clipboard = QApplication::clipboard();
    QVERIFY(clipboard);
    QScreen *screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);

    const QList<QPair<ui::CopyAsFileMode, bool>> cases{
        {ui::CopyAsFileMode::Disabled, false},
        {ui::CopyAsFileMode::Shortcut, false},
        {ui::CopyAsFileMode::Always, true},
    };
    for (const auto &[mode, expectFile] : cases) {
        clipboard->clear();
        QPointer<ui::SnipOverlay> overlay = new ui::SnipOverlay(
            screen, false, false, QRect(20, 20, 120, 80));
        overlay->setCopyAsFileMode(static_cast<int>(mode));
        overlay->show();
        auto *canvas = overlay->findChild<ui::AnnotationCanvas *>();
        auto *copy = overlay->findChild<QToolButton *>(QStringLiteral("完成"));
        QVERIFY(canvas && copy);
        QTRY_VERIFY(copy->isVisible());
        QVERIFY(!canvas->renderedImage().isNull());

        QTest::mouseClick(copy, Qt::LeftButton);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        const QMimeData *mime = clipboard->mimeData();
        QVERIFY(mime);
        QVERIFY(mime->hasImage());
        QCOMPARE(mime->hasUrls(), expectFile);
        if (expectFile) {
            QCOMPARE(mime->urls().size(), 1);
            const QString path = mime->urls().first().toLocalFile();
            QVERIFY(QFileInfo::exists(path));
            QVERIFY(QFile(path).remove());
        }
        QVERIFY(overlay.isNull());
    }
    clipboard->clear();
}

void UiRegressionTest::scrollCapturePanelUsesFloatingToolbar()
{
    ui::ScrollCapturePanel panel;
    panel.show();
    QTRY_VERIFY(panel.isVisible());
    QCOMPARE(panel.height(), 72);
    QVERIFY(panel.width() >= 320);
    QVERIFY(panel.findChild<QToolButton *>(QStringLiteral("scrollCaptureEdit")));
    QVERIFY(panel.findChild<QToolButton *>(QStringLiteral("scrollCaptureSave")));
    QVERIFY(panel.findChild<QToolButton *>(QStringLiteral("scrollCaptureFinish")));
    QVERIFY(panel.findChild<QToolButton *>(QStringLiteral("scrollCaptureCancel")));
    QVERIFY(panel.findChild<QToolButton *>(QStringLiteral("scrollCaptureCurrent")));
    QVERIFY(panel.findChild<QToolButton *>(QStringLiteral("scrollCaptureAuto")));
    auto *preview = panel.findChild<QLabel *>(QStringLiteral("scrollCapturePreview"));
    QVERIFY(preview);
    QVERIFY(!preview->isVisible());

    const QString artifactDir = qEnvironmentVariable("SNIPASTE_TEST_ARTIFACTS");
    if (!artifactDir.isEmpty()) {
        QDir().mkpath(artifactDir);
        QVERIFY(panel.grab().save(QDir(artifactDir).filePath(QStringLiteral("scroll-capture-toolbar.png"))));
    }
    panel.close();
}

void UiRegressionTest::scrollCapturePreviewShowsStitchedImage()
{
    ui::ScrollCapturePreview preview;
    preview.show();
    QTRY_VERIFY(preview.isVisible());
    QCOMPARE(preview.width(), 260);
    QVERIFY(preview.height() >= 240);

    QImage testFrame(300, 600, QImage::Format_ARGB32_Premultiplied);
    testFrame.fill(QColor(240, 242, 245));
    QPainter p(&testFrame);
    p.setPen(Qt::black);
    p.drawText(20, 50, QStringLiteral("Preview Item 1"));
    p.drawText(20, 550, QStringLiteral("Preview Item 2"));
    p.end();

    preview.setImage(testFrame);
    QCOMPARE(preview.image().size(), testFrame.size());

    auto *scrollArea = preview.findChild<QScrollArea *>(QStringLiteral("scrollCaptureScrollArea"));
    QVERIFY(scrollArea);
    auto *label = preview.findChild<QLabel *>(QStringLiteral("scrollCapturePreviewImage"));
    QVERIFY(label);
    QVERIFY(!label->pixmap().isNull());

    const QString artifactDir = qEnvironmentVariable("SNIPASTE_TEST_ARTIFACTS");
    if (!artifactDir.isEmpty()) {
        QDir().mkpath(artifactDir);
        QVERIFY(preview.grab().save(QDir(artifactDir).filePath(QStringLiteral("scroll-capture-preview.png"))));
    }
    preview.close();
}

void UiRegressionTest::preferencesCloseSavesWithoutQuitting()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    core::ConfigStore config(directory.filePath(QStringLiteral("config.ini")));
    QSignalSpy quitting(qApp, &QCoreApplication::aboutToQuit);
    for (int cycle = 0; cycle < 8; ++cycle) {
        ui::PreferencesDialog dialog(config);
        QVERIFY(!dialog.testAttribute(Qt::WA_QuitOnClose));
        QTimer::singleShot(0, &dialog, [&] {
            // Editing the filename used to call lambdas referencing constructor locals.
            for (auto *edit : dialog.findChildren<QLineEdit *>()) {
                if (!edit->isReadOnly() && edit->text().endsWith(QStringLiteral(".png"))) {
                    edit->setText(QStringLiteral("Regression_$yyyy-MM-dd$.png"));
                    break;
                }
            }
            auto *tabs = dialog.findChild<QTabWidget *>();
            tabs->setCurrentIndex(4); // Queue a layout update just before closing.
            dialog.close(); // Same QWidget close path as the title-bar X.
        });
        QCOMPARE(dialog.exec(), int(QDialog::Accepted));
        QVERIFY(!dialog.isVisible());
    }
    bool eventLoopAlive = false;
    QTimer::singleShot(0, qApp, [&] { eventLoopAlive = true; });
    QTRY_VERIFY(eventLoopAlive);
    QCOMPARE(quitting.count(), 0);
    core::ConfigStore reread(config.filePath());
    QVERIFY(reread.load());
    QCOMPARE(reread.value(QStringLiteral("output/fileNamePattern")).toString(),
             QStringLiteral("Regression_$yyyy-MM-dd$.png"));
}

void UiRegressionTest::preferencesPendingCallbacksAreCancelled()
{
    QTemporaryDir directory;
    core::ConfigStore config(directory.filePath(QStringLiteral("config.ini")));
    for (int page = 0; page < 8; ++page) {
        auto *dialog = new ui::PreferencesDialog(config);
        dialog->findChild<QTabWidget *>()->setCurrentIndex(page);
        delete dialog;
        // Previously, queued singleShot callbacks dereferenced the destroyed tabs here.
        QCoreApplication::processEvents();
    }
    QVERIFY(true);
}

void UiRegressionTest::preferencesProfessionalReferencePages()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    core::ConfigStore config(directory.filePath(QStringLiteral("config.ini")));
    ui::PreferencesDialog dialog(config);
    dialog.show();
    QTest::qWait(20);
    auto *tabs = dialog.findChild<QTabWidget *>(QStringLiteral("preferencesTabs"));
    QVERIFY(tabs);
    QCOMPARE(tabs->count(), 8);
    QCOMPARE(tabs->tabText(0), QStringLiteral("常规"));
    QCOMPARE(tabs->tabText(1), QStringLiteral("界面"));
    const QString artifactDir = qEnvironmentVariable("SNIPASTE_TEST_ARTIFACTS");
    const auto save = [&](const QString &name) {
        if (artifactDir.isEmpty()) return true;
        QDir().mkpath(artifactDir);
        return dialog.grab().save(QDir(artifactDir).filePath(name));
    };
    QVERIFY(save(QStringLiteral("preferences-general.png")));
    tabs->setCurrentIndex(1);
    QTest::qWait(20);
    auto *interfaceTabs = tabs->currentWidget()->findChild<QTabWidget *>(QStringLiteral("interfaceTabs"));
    QVERIFY(interfaceTabs);
    QCOMPARE(interfaceTabs->count(), 3);
    QCOMPARE(interfaceTabs->tabText(0), QStringLiteral("常规"));
    QCOMPARE(interfaceTabs->tabText(1), QStringLiteral("放大镜"));
    QCOMPARE(interfaceTabs->tabText(2), QStringLiteral("工具条"));
    QVERIFY(tabs->currentWidget()->findChild<QPushButton *>(QStringLiteral("interfaceFont")));
    QVERIFY(interfaceTabs->findChild<QSpinBox *>(QStringLiteral("magnifierZoom")));
    QVERIFY(interfaceTabs->findChild<QSpinBox *>(QStringLiteral("penTipCount")));
    interfaceTabs->setCurrentIndex(0); QTest::qWait(20); QVERIFY(save(QStringLiteral("preferences-interface-general.png")));
    interfaceTabs->setCurrentIndex(1); QTest::qWait(20); QVERIFY(save(QStringLiteral("preferences-interface-magnifier.png")));
    interfaceTabs->setCurrentIndex(2); QTest::qWait(20); QVERIFY(save(QStringLiteral("preferences-interface-toolbar.png")));
    tabs->setCurrentIndex(2); QTest::qWait(20);
    auto *captureTabs = tabs->currentWidget()->findChild<QTabWidget *>(QStringLiteral("captureTabs"));
    QVERIFY(captureTabs);
    QCOMPARE(captureTabs->count(), 3);
    QCOMPARE(captureTabs->tabText(0), QStringLiteral("显示"));
    QCOMPARE(captureTabs->tabText(1), QStringLiteral("行为"));
    QCOMPARE(captureTabs->tabText(2), QStringLiteral("按键"));
    captureTabs->setCurrentIndex(0); QTest::qWait(20); QVERIFY(save(QStringLiteral("preferences-capture-display.png")));
    captureTabs->setCurrentIndex(1); QTest::qWait(20); QVERIFY(save(QStringLiteral("preferences-capture-behavior.png")));
    captureTabs->setCurrentIndex(2); QTest::qWait(20); QVERIFY(save(QStringLiteral("preferences-capture-keys.png")));
    QVERIFY(tabs->currentWidget()->findChild<QCheckBox *>(QStringLiteral("capture/disableWheel")));
    QVERIFY(tabs->currentWidget()->findChild<QComboBox *>(QStringLiteral("capture/doubleClickAction")));
    auto *maskColor = tabs->currentWidget()->findChild<QPushButton *>(QStringLiteral("captureMaskColor"));
    QVERIFY(maskColor);
    const QColor customMask(12, 34, 56, 128);
    maskColor->setProperty("color", customMask);
    tabs->setCurrentIndex(3);
    QTest::qWait(20);
    auto *pasteTabs = tabs->currentWidget()->findChild<QTabWidget *>(QStringLiteral("pasteTabs"));
    QVERIFY(pasteTabs);
    QCOMPARE(pasteTabs->count(), 4);
    QCOMPARE(pasteTabs->tabText(0), QStringLiteral("显示"));
    QCOMPARE(pasteTabs->tabText(1), QStringLiteral("行为"));
    QCOMPARE(pasteTabs->tabText(2), QStringLiteral("按键"));
    QCOMPARE(pasteTabs->tabText(3), QStringLiteral("转换"));
    pasteTabs->setCurrentIndex(0); QTest::qWait(20); QVERIFY(save(QStringLiteral("preferences-paste-display.png")));
    QVERIFY(tabs->currentWidget()->findChild<QComboBox *>(QStringLiteral("pasteBackgroundMode")));
    pasteTabs->setCurrentIndex(1); QTest::qWait(20); QVERIFY(save(QStringLiteral("preferences-paste-behavior.png")));
    QVERIFY(tabs->currentWidget()->findChild<QCheckBox *>(QStringLiteral("pasteVirtualDesktopCheck")));
    QVERIFY(tabs->currentWidget()->findChild<QCheckBox *>(QStringLiteral("pasteHideOnFileDialogCheck")));
    pasteTabs->setCurrentIndex(2); QTest::qWait(20); QVERIFY(save(QStringLiteral("preferences-paste-keys.png")));
    auto *escapeAction = tabs->currentWidget()->findChild<QComboBox *>(QStringLiteral("pasteEscapeAction"));
    QVERIFY(escapeAction);
    escapeAction->setCurrentText(QStringLiteral("销毁贴图"));
    pasteTabs->setCurrentIndex(3); QTest::qWait(20); QVERIFY(save(QStringLiteral("preferences-paste-transform.png")));
    pasteTabs->setCurrentIndex(0);
    auto *backgroundMode = tabs->currentWidget()->findChild<QComboBox *>(QStringLiteral("pasteBackgroundMode"));
    QVERIFY(backgroundMode);
    backgroundMode->setCurrentText(QStringLiteral("透明"));
    tabs->setCurrentIndex(4);
    QTest::qWait(20);
    auto *outputTabs = tabs->currentWidget()->findChild<QTabWidget *>(QStringLiteral("outputTabs"));
    QVERIFY(outputTabs);
    QCOMPARE(outputTabs->count(), 3);
    QCOMPARE(outputTabs->tabText(0), QStringLiteral("文件"));
    QCOMPARE(outputTabs->tabText(1), QStringLiteral("剪贴板"));
    QCOMPARE(outputTabs->tabText(2), QStringLiteral("文字识别"));
    auto *autoSave = dialog.findChild<QCheckBox *>(QStringLiteral("outputAutoSave"));
    auto *fileRestore = dialog.findChild<QPushButton *>(QStringLiteral("outputFileRestore"));
    auto *clipboardRestore = dialog.findChild<QPushButton *>(QStringLiteral("outputClipboardRestore"));
    QVERIFY(autoSave && fileRestore && clipboardRestore);
    autoSave->setChecked(true);
    fileRestore->click();
    QVERIFY(!autoSave->isChecked());
    autoSave->setChecked(true);
    clipboardRestore->click();
    QVERIFY(!autoSave->isChecked());
    autoSave->setChecked(true);
    outputTabs->setCurrentIndex(0); QTest::qWait(20); QVERIFY(save(QStringLiteral("preferences-output-file.png")));
    QVERIFY(tabs->currentWidget()->findChild<QSpinBox *>(QStringLiteral("outputImageQuality")));
    outputTabs->setCurrentIndex(1); QTest::qWait(20); QVERIFY(save(QStringLiteral("preferences-output-clipboard.png")));
    outputTabs->setCurrentIndex(2); QTest::qWait(20); QVERIFY(save(QStringLiteral("preferences-output-text.png")));
    auto *ocrEngine = tabs->currentWidget()->findChild<QComboBox *>(QStringLiteral("ocrEngine"));
    QVERIFY(ocrEngine);
    ocrEngine->setCurrentText(QStringLiteral("Tesseract"));
    tabs->setCurrentIndex(5);
    QTest::qWait(20);
    auto *controlTabs = tabs->currentWidget()->findChild<QTabWidget *>(QStringLiteral("controlTabs"));
    QVERIFY(controlTabs);
    QCOMPARE(controlTabs->count(), 4);
    QCOMPARE(controlTabs->tabText(0), QStringLiteral("全局快捷键"));
    QCOMPARE(controlTabs->tabText(1), QStringLiteral("应用内快捷键"));
    QCOMPARE(controlTabs->tabText(2), QStringLiteral("鼠标"));
    QCOMPARE(controlTabs->tabText(3), QStringLiteral("触发角"));
    controlTabs->setCurrentIndex(0); QTest::qWait(20); QVERIFY(save(QStringLiteral("preferences-control-global.png")));
    QVERIFY(controlTabs->currentWidget()->findChild<QKeySequenceEdit *>());
    QVERIFY(controlTabs->currentWidget()->findChild<QPushButton *>(QStringLiteral("addGlobalCommand")));
    controlTabs->setCurrentIndex(1); QTest::qWait(20); QVERIFY(save(QStringLiteral("preferences-control-application.png")));
    controlTabs->setCurrentIndex(2); QTest::qWait(20); QVERIFY(save(QStringLiteral("preferences-control-mouse.png")));
    QVERIFY(controlTabs->currentWidget()->findChild<QGroupBox *>());
    QVERIFY(controlTabs->currentWidget()->findChild<QLineEdit *>(QStringLiteral("trayCommand0")));
    controlTabs->setCurrentIndex(3); QTest::qWait(20); QVERIFY(save(QStringLiteral("preferences-control-corners.png")));
    auto *cornerEnabled = controlTabs->currentWidget()->findChild<QCheckBox *>(QStringLiteral("controlCornerEnabled"));
    auto *cornerSize = controlTabs->currentWidget()->findChild<QSpinBox *>(QStringLiteral("controlCornerSize"));
    auto *cornerIgnored = controlTabs->currentWidget()->findChild<QLineEdit *>(QStringLiteral("controlCornerIgnoredApplications"));
    QVERIFY(cornerEnabled && cornerSize && cornerIgnored);
    cornerEnabled->setChecked(true);
    cornerSize->setValue(14);
    cornerIgnored->setText(QStringLiteral("demo.exe, editor.exe"));
    tabs->setCurrentIndex(6);
    QTest::qWait(20);
    QVERIFY(save(QStringLiteral("preferences-update.png")));
    auto *includeBeta = dialog.findChild<QCheckBox *>(QStringLiteral("updateIncludeBeta"));
    auto *checkAtStartup = dialog.findChild<QCheckBox *>(QStringLiteral("updateCheckAtStartup"));
    auto *updateHours = dialog.findChild<QSpinBox *>(QStringLiteral("updateCheckIntervalHours"));
    auto *proxyType = dialog.findChild<QComboBox *>(QStringLiteral("updateProxyType"));
    auto *proxyAuth = dialog.findChild<QCheckBox *>(QStringLiteral("updateProxyRequiresAuth"));
    QVERIFY(includeBeta && checkAtStartup && updateHours && proxyType && proxyAuth);
    includeBeta->setChecked(true);
    checkAtStartup->setChecked(false);
    updateHours->setValue(48);
    proxyType->setCurrentText(QStringLiteral("HTTP 代理"));
    proxyAuth->setChecked(true);
    tabs->setCurrentIndex(7);
    QTest::qWait(20);
    QVERIFY(save(QStringLiteral("preferences-about.png")));
    QVERIFY(dialog.findChild<QLabel *>(QStringLiteral("aboutLogo")));
    QVERIFY(dialog.findChild<QPushButton *>(QStringLiteral("aboutOpenSourceButton")));
    QVERIFY(dialog.findChild<QPushButton *>(QStringLiteral("aboutActivationButton")));
    QVERIFY(dialog.findChild<QScrollArea *>(QStringLiteral("aboutSupportBrowser")));
    auto *customHotkey = dialog.findChild<QKeySequenceEdit *>(QStringLiteral("hotkeys/custom"));
    auto *trayCommand = dialog.findChild<QLineEdit *>(QStringLiteral("trayCommand0"));
    QVERIFY(customHotkey && trayCommand);
    customHotkey->setKeySequence(QKeySequence(QStringLiteral("Ctrl+Shift+F1")));
    trayCommand->setText(QStringLiteral("toggle-images"));
    auto *start = dialog.findChild<QCheckBox *>(QStringLiteral("startAtLogin"));
    auto *backup = dialog.findChild<QCheckBox *>(QStringLiteral("general/backup"));
    auto *font = dialog.findChild<QPushButton *>(QStringLiteral("interfaceFont"));
    QVERIFY(start && backup && font);
    backup->setChecked(false);
    QMetaObject::invokeMethod(&dialog, "accept", Qt::DirectConnection);
    QVERIFY(!config.value(QStringLiteral("general/backup")).toBool());
    QVERIFY(!config.value(QStringLiteral("interface/font")).toString().isEmpty());
    QCOMPARE(config.value(QStringLiteral("capture/maskColor")).toString(),
             customMask.name(QColor::HexArgb));
    QCOMPARE(config.value(QStringLiteral("paste/backgroundMode")).toString(), QStringLiteral("透明"));
    QCOMPARE(config.value(QStringLiteral("paste/escapeAction")).toString(), QStringLiteral("销毁贴图"));
    QCOMPARE(config.value(QStringLiteral("output/ocrEngine")).toString(), QStringLiteral("Tesseract"));
    QCOMPARE(config.value(QStringLiteral("capture/autoSave")).toBool(), true);
    QCOMPARE(config.value(QStringLiteral("control/cornerEnabled")).toBool(), true);
    QCOMPARE(config.value(QStringLiteral("control/cornerSize")).toInt(), 14);
    QCOMPARE(config.value(QStringLiteral("control/cornerIgnoredApplications")).toStringList(),
             QStringList({QStringLiteral("demo.exe"), QStringLiteral("editor.exe")}));
    QCOMPARE(config.value(QStringLiteral("update/includeBeta")).toBool(), true);
    QCOMPARE(config.value(QStringLiteral("update/checkAtStartup")).toBool(), false);
    QCOMPARE(config.value(QStringLiteral("update/checkIntervalHours")).toInt(), 48);
    QCOMPARE(config.value(QStringLiteral("update/proxyType")).toString(), QStringLiteral("HTTP 代理"));
    QCOMPARE(config.value(QStringLiteral("update/proxyRequiresAuth")).toBool(), true);
    QCOMPARE(config.value(QStringLiteral("hotkeys/custom")).toString(), QStringLiteral("Ctrl+Shift+F1"));
    QCOMPARE(config.value(QStringLiteral("control/trayLeft")).toString(), QStringLiteral("toggle-images"));
}

void UiRegressionTest::trayContextMenuMatchesReference()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    core::ConfigStore config(directory.filePath(QStringLiteral("config.ini")));
    core::CommandRegistry commands;
    ui::TrayController tray(commands, config);
    QMenu *menu = tray.contextMenu();
    QVERIFY(menu);
    QCOMPARE(menu->objectName(), QStringLiteral("trayContextMenu"));
    const QList<QAction *> actions = menu->actions();
    QCOMPARE(actions.size(), 18);
    QCOMPARE(actions.at(0)->text(), QStringLiteral("截屏"));
    QCOMPARE(actions.at(0)->shortcut(), QKeySequence(Qt::Key_F1));
    QCOMPARE(actions.at(1)->text(), QStringLiteral("截屏并自动复制"));
    QCOMPARE(actions.at(1)->shortcut(), snipaste::platform::primaryKeySequence(Qt::Key_F1));
    QCOMPARE(actions.at(2)->text(), QStringLiteral("自定义截屏"));
    QCOMPARE(actions.at(2)->shortcut(), QKeySequence(Qt::SHIFT | Qt::Key_F1));
    QCOMPARE(actions.at(3)->text(), QStringLiteral("贴图"));
    QCOMPARE(actions.at(4)->text(), QStringLiteral("隐藏/显示所有贴图"));
    QCOMPARE(actions.at(5)->text(), QStringLiteral("切换到另一贴图组"));
    QCOMPARE(actions.at(6)->text(), QStringLiteral("清空截屏历史"));
    QVERIFY(actions.at(7)->isSeparator());
    QCOMPARE(actions.at(8)->text(), QStringLiteral("禁用快捷键"));
    QVERIFY(actions.at(9)->isSeparator());
    QVERIFY(actions.at(9)->isSeparator());
    QCOMPARE(actions.at(10)->text(), QStringLiteral("贴图"));
    QVERIFY(actions.at(10)->menu());
    QVERIFY(actions.at(11)->isSeparator());
    QCOMPARE(actions.at(12)->text(), QStringLiteral("首选项…"));
    QCOMPARE(actions.at(13)->text(), QStringLiteral("帮助"));
    QCOMPARE(actions.at(14)->text(), QStringLiteral("检查更新…"));
    QVERIFY(actions.at(15)->isSeparator());
    QCOMPARE(actions.at(16)->text(), QStringLiteral("重新启动"));
    QCOMPARE(actions.at(17)->text(), QStringLiteral("退出"));
}

void UiRegressionTest::hotCornerDwellState()
{
    core::HotCornerState state;
    const QRect screen(0, 0, 100, 100);
    QCOMPARE(state.sample(QPoint(1, 1), screen, 10, 0, 300, true), -1);
    QCOMPARE(state.sample(QPoint(1, 1), screen, 10, 299, 300, true), -1);
    QCOMPARE(state.sample(QPoint(1, 1), screen, 10, 300, 300, true), 0);
    QCOMPARE(state.sample(QPoint(1, 1), screen, 10, 301, 300, true), -1);
    QCOMPARE(state.sample(QPoint(50, 50), screen, 10, 400, 300, true), -1);
    QCOMPARE(state.sample(QPoint(98, 98), screen, 10, 400, 300, true), -1);
    QCOMPARE(state.sample(QPoint(98, 98), screen, 10, 699, 300, true), -1);
    QCOMPARE(state.sample(QPoint(98, 98), screen, 10, 700, 300, true), 3);
    state.reset();
    QCOMPARE(state.sample(QPoint(1, 1), screen, 10, 0, 0, false), -1);
}

static ui::SnipOverlay *overlayForTest()
{
    auto *overlay = new ui::SnipOverlay(QGuiApplication::primaryScreen());
    overlay->show();
    return overlay;
}

void UiRegressionTest::hoverTargetLocksUntilReset()
{
    QPointer<ui::SnipOverlay> overlay = overlayForTest();
    const QRect icon(80, 90, 130, 95);
    const QRect window(320, 80, 300, 250);
    overlay->setTargetDetector([=](QPoint point, bool) {
        return point.x() < 300 ? icon : window;
    });
    auto *toolbar = overlay->findChild<QFrame *>(QStringLiteral("captureToolbar"));
    QTest::mouseMove(overlay, icon.center());
    QVERIFY(!toolbar->isVisible());
    QVERIFY(!overlay->findChild<ui::AnnotationCanvas *>());
    const QString artifactDir = qEnvironmentVariable("SNIPASTE_TEST_ARTIFACTS");
    if (!artifactDir.isEmpty()) {
        QDir().mkpath(artifactDir);
        QVERIFY(overlay->grab().save(QDir(artifactDir).filePath(QStringLiteral("hover-magnifier.png"))));
    }
    QTest::mouseClick(overlay, Qt::LeftButton, Qt::NoModifier, icon.center());
    auto *canvas = overlay->findChild<ui::AnnotationCanvas *>();
    QVERIFY(canvas);
    QCOMPARE(canvas->geometry(), icon);
    QVERIFY(toolbar->isVisible());
    QTest::mouseMove(overlay, window.center());
    // Moving and clicking elsewhere must not silently replace a locked region.
    QTest::mouseClick(overlay, Qt::LeftButton, Qt::NoModifier, window.center());
    QCOMPARE(canvas->geometry(), icon);
    QTest::mouseClick(overlay, Qt::RightButton, Qt::NoModifier, window.center());
    QVERIFY(!toolbar->isVisible());
    QVERIFY(!overlay->findChild<ui::AnnotationCanvas *>());
    QTest::mouseClick(overlay, Qt::LeftButton, Qt::NoModifier, window.center());
    QCOMPARE(overlay->findChild<ui::AnnotationCanvas *>()->geometry(), window);
    overlay->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void UiRegressionTest::selectionBorderRemainsVisibleAfterConfirm()
{
    QPointer<ui::SnipOverlay> overlay = overlayForTest();
    const QRect region(120, 100, 220, 160);
    overlay->setTargetDetector([=](QPoint, bool) {
        return region;
    });
    QTest::mouseMove(overlay, region.center());
    // Click to confirm the selection and create the annotation canvas
    QTest::mouseClick(overlay, Qt::LeftButton, Qt::NoModifier, region.center());
    auto *canvas = overlay->findChild<ui::AnnotationCanvas *>();
    QVERIFY(canvas);
    QCOMPARE(canvas->geometry(), region);

    // Grab the rendered result of the overlay
    const QPixmap pixmap = overlay->grab();
    const QImage image = pixmap.toImage();

    // Verify the blue selection frame (#2889ed = rgb(40, 137, 237)) is visible
    // on all four edges and not covered by the canvas
    const QColor blue(40, 137, 237);

    // Left edge
    const QColor leftPixel = image.pixelColor(region.left(), region.center().y());
    QCOMPARE(leftPixel, blue);

    // Top edge
    const QColor topPixel = image.pixelColor(region.center().x(), region.top());
    QCOMPARE(topPixel, blue);

    // Right edge: center of line is at region.right() - 1 due to pen width 2
    const QColor rightPixel = image.pixelColor(region.right() - 1, region.center().y());
    QCOMPARE(rightPixel, blue);

    // Bottom edge: center of line is at region.bottom() - 1
    const QColor bottomPixel = image.pixelColor(region.center().x(), region.bottom() - 1);
    QCOMPARE(bottomPixel, blue);

    overlay->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void UiRegressionTest::manualDragOverridesDetectedTarget()
{
    QPointer<ui::SnipOverlay> overlay = overlayForTest();
    overlay->setTargetDetector([=](QPoint, bool) { return overlay->rect(); });
    QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, QPoint(60, 80));
    QTest::mouseMove(overlay, QPoint(280, 190));
    QVERIFY(!overlay->findChild<QFrame *>(QStringLiteral("captureToolbar"))->isVisible());
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, QPoint(280, 190));
    QCOMPARE(overlay->findChild<ui::AnnotationCanvas *>()->geometry(),
             QRect(QPoint(60, 80), QPoint(280, 190)));
    QTest::mouseClick(overlay, Qt::RightButton, Qt::NoModifier, QPoint(30, 30));
    overlay->setDetectionOptions(false, false, false);
    QTest::mouseClick(overlay, Qt::LeftButton, Qt::NoModifier, QPoint(100, 100));
    QVERIFY(!overlay->findChild<ui::AnnotationCanvas *>());
    overlay->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void UiRegressionTest::detectionModesAndColorShortcuts()
{
    QPointer<ui::SnipOverlay> overlay = overlayForTest();
    bool elementMode = false;
    const QRect icon(80, 90, 130, 95);
    const QRect window(60, 60, 350, 250);
    overlay->setTargetDetector([&](QPoint, bool elements) {
        elementMode = elements;
        return elements ? icon : window;
    });
    QTest::mouseMove(overlay, icon.center());
    QVERIFY(elementMode);
    QTest::keyClick(overlay, Qt::Key_Tab);
    QVERIFY(!elementMode);
    QTest::keyClick(overlay, Qt::Key_C);
    const QString rgb = QApplication::clipboard()->text();
    const auto components = rgb.split(QStringLiteral(", "));
    QCOMPARE(components.size(), 3);
    QTest::keyClick(overlay, Qt::Key_Shift);
    QTest::keyClick(overlay, Qt::Key_C);
    const QColor color(components[0].toInt(), components[1].toInt(), components[2].toInt());
    QCOMPARE(QApplication::clipboard()->text(), color.name().toUpper());
    QVERIFY(!overlay->findChild<QFrame *>(QStringLiteral("captureToolbar"))->isVisible());
    QTest::mouseClick(overlay, Qt::LeftButton, Qt::NoModifier, icon.center());
    QCOMPARE(overlay->findChild<ui::AnnotationCanvas *>()->geometry(), window);
    QTest::mouseClick(overlay, Qt::RightButton, Qt::NoModifier, icon.center());
    overlay->setLastSelection(icon);
    QTest::keyClick(overlay, Qt::Key_R);
    QCOMPARE(overlay->findChild<ui::AnnotationCanvas *>()->geometry(), icon);
    QTest::keyClick(overlay, Qt::Key_A, snipaste::platform::primaryModifier());
    QCOMPARE(overlay->findChild<ui::AnnotationCanvas *>()->geometry(), overlay->rect());
    overlay->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void UiRegressionTest::resizeSelectionEdgesAndCorners_data()
{
    QTest::addColumn<QPoint>("handle");
    QTest::addColumn<QPoint>("delta");
    QTest::addColumn<QRect>("expected");
    const QRect initial(150, 140, 300, 220);
    QTest::newRow("left") << QPoint(150, 230) << QPoint(-40, 0) << QRect(110, 140, 340, 220);
    QTest::newRow("right") << QPoint(449, 230) << QPoint(50, 0) << QRect(150, 140, 350, 220);
    QTest::newRow("top") << QPoint(300, 140) << QPoint(0, 20) << QRect(150, 160, 300, 200);
    QTest::newRow("bottom") << QPoint(300, 359) << QPoint(0, -40) << QRect(150, 140, 300, 180);
    QTest::newRow("top-left") << initial.topLeft() << QPoint(-20, -30) << QRect(130, 110, 320, 250);
    QTest::newRow("top-right") << initial.topRight() << QPoint(20, -30) << QRect(150, 110, 320, 250);
    QTest::newRow("bottom-left") << initial.bottomLeft() << QPoint(-20, 30) << QRect(130, 140, 320, 250);
    QTest::newRow("bottom-right") << initial.bottomRight() << QPoint(20, 30) << QRect(150, 140, 320, 250);
    QTest::newRow("minimum-width") << QPoint(150, 230) << QPoint(400, 0) << QRect(448, 140, 2, 220);
    QTest::newRow("screen-edge") << initial.topLeft() << QPoint(-500, -500) << QRect(0, 0, 450, 360);
}

void UiRegressionTest::resizeSelectionEdgesAndCorners()
{
    QFETCH(QPoint, handle);
    QFETCH(QPoint, delta);
    QFETCH(QRect, expected);
    QPointer<ui::SnipOverlay> overlay = new ui::SnipOverlay(QGuiApplication::primaryScreen(),
        false, false, QRect(150, 140, 300, 220));
    overlay->show();
    auto *toolbar = overlay->findChild<QFrame *>(QStringLiteral("captureToolbar"));
    QTest::mouseMove(overlay, handle);
    QVERIFY(overlay->cursor().shape() != Qt::CrossCursor);
    QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, handle);
    QVERIFY(!toolbar->isVisible());
    QTest::mouseMove(overlay, handle + delta);
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, handle + delta);
    QCOMPARE(overlay->findChild<ui::AnnotationCanvas *>()->geometry(), expected);
    QVERIFY(toolbar->isVisible());
    QVERIFY(overlay->rect().contains(toolbar->geometry()));
    QSignalSpy captured(overlay, &ui::SnipOverlay::selectionCaptured);
    QTest::keyClick(overlay, Qt::Key_Return);
    QCOMPARE(captured.count(), 1);
    QCOMPARE(qvariant_cast<QRect>(captured.at(0).at(0)),
             expected.translated(QGuiApplication::primaryScreen()->geometry().topLeft()));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void UiRegressionTest::resizePreservesAnnotationsAndUndo()
{
    QPointer<ui::SnipOverlay> overlay = new ui::SnipOverlay(QGuiApplication::primaryScreen(),
        false, false, QRect(150, 140, 300, 220));
    overlay->show();
    auto *canvas = overlay->findChild<ui::AnnotationCanvas *>();
    QTest::mouseClick(overlay->findChild<QToolButton *>(QStringLiteral("形状")), Qt::LeftButton);
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(50, 50));
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(160, 130));
    const QImage before = canvas->renderedImage();
    // The active annotation canvas must forward its edge press to the overlay.
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(2, 90));
    QTest::mouseMove(canvas, QPoint(-28, 90));
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, QPoint(122, 230));
    QCOMPARE(canvas->geometry(), QRect(120, 140, 330, 220));
    const QImage expanded = canvas->renderedImage();
    QCOMPARE(expanded.copy(QRect(30, 0, 300, 220)), before);
    canvas->undo();
    QVERIFY(canvas->renderedImage() != expanded);
    canvas->redo();
    QCOMPARE(canvas->renderedImage(), expanded);
    overlay->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void UiRegressionTest::moveSelectionAndClampToScreen()
{
    QPointer<ui::SnipOverlay> overlay = new ui::SnipOverlay(QGuiApplication::primaryScreen(),
        false, false, QRect(150, 140, 300, 220));
    overlay->show();
    QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, QPoint(240, 220));
    QTest::mouseMove(overlay, QPoint(340, 280));
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, QPoint(340, 280));
    QCOMPARE(overlay->findChild<ui::AnnotationCanvas *>()->geometry(), QRect(250, 200, 300, 220));
    QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, QPoint(340, 280));
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, QPoint(-500, -500));
    QCOMPARE(overlay->findChild<ui::AnnotationCanvas *>()->geometry(), QRect(0, 0, 300, 220));
    overlay->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void UiRegressionTest::preparedOverlayReusesControlsAndReleasesPixels()
{
    std::unique_ptr<ui::SnipOverlay> overlay(ui::SnipOverlay::createPrepared(QGuiApplication::primaryScreen()));
    auto *toolbar = overlay->findChild<QFrame *>(QStringLiteral("captureToolbar"));
    auto *preparedCanvas = overlay->findChild<ui::AnnotationCanvas *>();
    auto *editor = overlay->findChild<QPlainTextEdit *>();
    QVERIFY(toolbar && preparedCanvas && editor);
    QVERIFY(!overlay->isVisible());
    QVERIFY(preparedCanvas->fullRenderedImage().isNull());
    const auto controlCount = overlay->findChildren<QWidget *>().size();
    QSignalSpy closed(overlay.get(), &ui::SnipOverlay::sessionClosed);
    for (int session = 0; session < 3; ++session) {
        overlay->beginCapture(QGuiApplication::primaryScreen());
        overlay->setExitOnFocusLoss(false);
        overlay->setDetectionOptions(false, false, false);
        QImage frame(overlay->size(), QImage::Format_RGB32);
        frame.fill(session == 0 ? Qt::red : Qt::blue);
        overlay->setBackgroundProvider([frame] { return frame; });
        overlay->show();
        QTest::keyClick(overlay.get(), Qt::Key_F5);
        QTRY_VERIFY(overlay->isVisible());
        QTest::mousePress(overlay.get(), Qt::LeftButton, Qt::NoModifier, QPoint(40, 40));
        QTest::mouseRelease(overlay.get(), Qt::LeftButton, Qt::NoModifier, QPoint(400, 300));
        QCOMPARE(overlay->findChild<ui::AnnotationCanvas *>(QStringLiteral("selectionCanvas")), preparedCanvas);
        QCOMPARE(overlay->findChild<QFrame *>(QStringLiteral("captureToolbar")), toolbar);
        QCOMPARE(preparedCanvas->fullRenderedImage(), frame);
        QVERIFY(!preparedCanvas->canUndo());
        QVERIFY(!preparedCanvas->canRedo());
        QVERIFY(toolbar->isVisible());
        preparedCanvas->setTool(ui::AnnotationCanvas::Tool::Text);
        QTest::mouseClick(preparedCanvas, Qt::LeftButton, Qt::NoModifier, QPoint(70, 70));
        editor->setPlainText(QStringLiteral("pending text from old capture"));
        overlay->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(closed.count(), session + 1);
        QVERIFY(!overlay->isVisible());
        QCOMPARE(overlay->size(), QSize(1, 1));
        if (overlay->backingStore())
            QCOMPARE(overlay->backingStore()->size(), QSize(1, 1));
        QVERIFY(!toolbar->isVisible());
        QVERIFY(preparedCanvas->fullRenderedImage().isNull());
        QVERIFY(!preparedCanvas->canUndo() && !preparedCanvas->canRedo());
        QVERIFY(editor->toPlainText().isEmpty());
        QCOMPARE(overlay->findChildren<QWidget *>().size(), controlCount);
    }
    // Initial selections and automatic output use the same prepared controls.
    overlay->beginCapture(QGuiApplication::primaryScreen(), true, false, QRect(40, 40, 120, 80));
    overlay->setExitOnFocusLoss(false);
    QImage frame(overlay->size(), QImage::Format_RGB32);
    frame.fill(Qt::green);
    overlay->setBackgroundProvider([frame] { return frame; });
    QSignalSpy captured(overlay.get(), &ui::SnipOverlay::screenshotCaptured);
    overlay->show();
    QTest::keyClick(overlay.get(), Qt::Key_F5);
    QTRY_VERIFY(overlay->isVisible());
    overlay->captureSelectionImmediately();
    QCOMPARE(captured.count(), 1);
    QCOMPARE(closed.count(), 4);
    QCOMPARE(captured.first().first().value<QImage>(), frame.copy(40, 40, 120, 80));
    // A refresh left pending at cancellation cannot revive a closed session.
    overlay->beginCapture(QGuiApplication::primaryScreen());
    overlay->setExitOnFocusLoss(false);
    overlay->setBackgroundProvider([frame] { return frame; });
    overlay->show();
    QTest::keyClick(overlay.get(), Qt::Key_F5);
    overlay->close();
    QTest::qWait(80);
    QVERIFY(!overlay->isVisible());
    QCOMPARE(closed.count(), 5);
}

void UiRegressionTest::annotationExportSharesCommittedCache()
{
    QImage frame(640, 420, QImage::Format_ARGB32_Premultiplied);
    frame.fill(Qt::white);
    ui::AnnotationCanvas canvas(frame);
    canvas.resize(frame.size());
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(40, 40));
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(300, 240));
    const QImage committed = canvas.fullRenderedImage(false);
    const QImage exported = canvas.fullRenderedImage();
    QCOMPARE(exported, committed);
    QCOMPARE(exported.constBits(), committed.constBits());
    canvas.show();
    canvas.setTool(ui::AnnotationCanvas::Tool::Text);
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(80, 80));
    auto *editor = canvas.findChild<QPlainTextEdit *>();
    QVERIFY(editor->isVisible());
    editor->setPlainText(QStringLiteral("pending"));
    QVERIFY(canvas.fullRenderedImage() != committed);
    QCOMPARE(canvas.fullRenderedImage(false), committed);
    canvas.finishTextInput(false);
    QCOMPARE(canvas.fullRenderedImage(), committed);
}

void UiRegressionTest::toolbarOnlyAfterSelection()
{
    QPointer<ui::SnipOverlay> overlay = overlayForTest();
    auto *toolbar = overlay->findChild<QFrame *>(QStringLiteral("captureToolbar"));
    QVERIFY(toolbar);
    QVERIFY(!toolbar->isVisible());
    QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, QPoint(80, 80));
    QTest::mouseMove(overlay, QPoint(650, 400));
    QVERIFY(!toolbar->isVisible());
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, QPoint(650, 400));
    QVERIFY(toolbar->isVisible());
    QCOMPARE(toolbar->geometry().right(), 650);
    QVERIFY(overlay->rect().contains(toolbar->geometry()));
    const QStringList labels{QStringLiteral("形状"), QStringLiteral("线条"),
        QStringLiteral("画笔"), QStringLiteral("荧光笔"), QStringLiteral("马赛克"),
        QStringLiteral("文字"), QStringLiteral("序号"), QStringLiteral("放大镜标注"), QStringLiteral("橡皮擦"),
        QStringLiteral("文字识别"), QStringLiteral("撤销"), QStringLiteral("重做"), QStringLiteral("取消"),
        QStringLiteral("分享"), QStringLiteral("贴图"), QStringLiteral("保存"), QStringLiteral("完成"), QStringLiteral("更多")};
    int lastX = -1;
    for (const auto &label : labels) {
        auto *button = toolbar->findChild<QToolButton *>(label);
        QVERIFY2(button, qPrintable(label));
        QVERIFY(button->x() > lastX);
        lastX = button->x();
    }
    const QString artifactDir = qEnvironmentVariable("SNIPASTE_TEST_ARTIFACTS");
    if (!artifactDir.isEmpty()) {
        QDir().mkpath(artifactDir);
        QVERIFY(overlay->grab().save(QDir(artifactDir).filePath(QStringLiteral("selection-toolbar.png"))));
        QTest::mouseClick(toolbar->findChild<QToolButton *>(QStringLiteral("马赛克")), Qt::LeftButton);
        QVERIFY(overlay->grab().save(QDir(artifactDir).filePath(QStringLiteral("mosaic-toolbar.png"))));
    }
    QTest::mouseClick(overlay, Qt::RightButton, Qt::NoModifier, QPoint(40, 40));
    QVERIFY(!toolbar->isVisible());
    QVERIFY(!overlay->findChild<ui::AnnotationCanvas *>());
    QTest::mouseClick(overlay, Qt::LeftButton, Qt::NoModifier, QPoint(40, 40));
    QVERIFY(!toolbar->isVisible()); // A click without an area is not a selection.
    QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, QPoint(70, 70));
    QTest::mouseMove(overlay, overlay->rect().bottomRight() - QPoint(5, 5));
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, overlay->rect().bottomRight() - QPoint(5, 5));
    QVERIFY(overlay->rect().contains(toolbar->geometry())); // Bottom-edge fallback.
    overlay->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(overlay.isNull());
}

void UiRegressionTest::annotationInvalidationReleasesPixels()
{
    QImage source(640, 420, QImage::Format_RGB32);
    source.fill(Qt::white);
    ui::AnnotationCanvas canvas(source);
    canvas.resize(source.size());
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(40, 40));
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(300, 240));
    QImage cached = canvas.fullRenderedImage(false);
    QVERIFY(cached != source);
    QVERIFY(!cached.isDetached());
    canvas.undo();
    QVERIFY(cached.isDetached());
    QCOMPARE(canvas.fullRenderedImage(false), source);
    canvas.redo();
    QCOMPARE(canvas.fullRenderedImage(false), cached);
    cached = canvas.fullRenderedImage(false);
    canvas.clearAnnotations();
    QVERIFY(cached.isDetached());
    QCOMPARE(canvas.fullRenderedImage(false), source);
}

void UiRegressionTest::magnifierRegionPreservesFullFramePixels()
{
    QImage source(640, 420, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < source.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(source.scanLine(y));
        for (int x = 0; x < source.width(); ++x)
            row[x] = qPremultiply(qRgba((x * 7 + y) % 256, (y * 13 + x) % 256,
                                      (x + y * 3) % 256, 80 + (x + y) % 176));
    }
    const QList<QRectF> samples{
        QRectF(120.25, 70.75, 83.5, 65.25), QRectF(-10.5, -5.25, 80.75, 60.5),
        QRectF(600.75, 385.5, 65.25, 55.5), QRectF(0, 0, 640, 420)};
    for (const QRectF &sample : samples) {
        for (const qreal zoom : {1.1, 1.75, 2.0, 7.9}) {
            const QRectF target(QPointF(210.5, 120.25), sample.size() * zoom);
            for (const bool ellipse : {false, true}) {
                QImage expected = source.copy();
                QImage actual = source.copy();
                const auto clip = [&](QPainter &painter) {
                    QPainterPath path;
                    if (ellipse) path.addEllipse(target);
                    else path.addRoundedRect(target, 12.5, 12.5);
                    painter.setClipPath(path);
                };
                {
                    QPainter painter(&expected);
                    clip(painter);
                    const QImage fullSnapshot = expected;
                    QTransform transform;
                    transform.translate(target.center().x(), target.center().y());
                    transform.scale(target.width() / sample.width(), target.height() / sample.height());
                    transform.translate(-sample.center().x(), -sample.center().y());
                    painter.setWorldTransform(transform, true);
                    painter.setRenderHint(QPainter::SmoothPixmapTransform);
                    painter.drawImage(QPointF(), fullSnapshot);
                }
                {
                    QPainter painter(&actual);
                    clip(painter);
                    ui::annotation::paintMagnifiedRegion(painter, actual, sample, target);
                }
                QVERIFY2(actual == expected, qPrintable(QStringLiteral("source=%1,%2 zoom=%3 ellipse=%4")
                    .arg(sample.x()).arg(sample.y()).arg(zoom).arg(ellipse)));
            }
        }
    }
}

void UiRegressionTest::historyDialogLoadsPreviewsAsynchronously()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    core::ScreenshotHistory history;
    const QList<QColor> colors{Qt::red, Qt::green, Qt::blue};
    for (const QColor &color : colors) {
        QImage image(1280, 960, QImage::Format_RGB32);
        image.fill(color);
        history.add(image);
    }
    QVERIFY(history.saveToDirectory(directory.path()));
    history.loadFromDirectory(directory.path());
    ui::ScreenshotHistoryDialog dialog(history);
    dialog.show();
    auto *list = dialog.findChild<QListWidget *>();
    auto *preview = dialog.findChild<QLabel *>();
    QVERIFY(list && preview);
    QCOMPARE(list->count(), 3);
    QTRY_VERIFY(!list->item(0)->icon().isNull());
    QTRY_VERIFY(!preview->pixmap().isNull());
    list->setCurrentRow(2);
    list->setCurrentRow(1);
    QTRY_COMPARE(preview->pixmap().toImage().pixelColor(0, 0), QColor(Qt::green));
    history.clear();
    dialog.refresh();
    QCOMPARE(list->count(), 0);
    QCOMPARE(preview->text(), QStringLiteral("暂无截屏历史"));
    dialog.close();
}

void UiRegressionTest::proGroupedToolsAndStyles()
{
    using Tool = ui::AnnotationCanvas::Tool;
    QPointer<ui::SnipOverlay> overlay = new ui::SnipOverlay(
        QGuiApplication::primaryScreen(), false, false, QRect(40, 70, 280, 200));
    overlay->show();
    auto *canvas = overlay->findChild<ui::AnnotationCanvas *>();
    auto *shapes = overlay->findChild<QToolButton *>(QStringLiteral("形状"));
    QVERIFY(canvas && shapes && shapes->menu());
    const QImage original = canvas->renderedImage();
    for (const auto &item : QList<QPair<QString, Tool>>{
             {QStringLiteral("椭圆"), Tool::Ellipse}, {QStringLiteral("圆角矩形"), Tool::RoundedRectangle}}) {
        auto *action = shapes->menu()->findChild<QAction *>(item.first);
        QVERIFY(action);
        action->trigger();
        QCOMPARE(canvas->tool(), item.second);
        QVERIFY(shapes->isChecked());
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(30, 30));
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(190, 140));
        const QImage drawn = canvas->renderedImage();
        QVERIFY(drawn != original);
        canvas->undo();
        QCOMPARE(canvas->renderedImage(), original);
        canvas->redo();
        QCOMPARE(canvas->renderedImage(), drawn);
        canvas->clearAnnotations();
    }
    auto *lines = overlay->findChild<QToolButton *>(QStringLiteral("线条"));
    QTest::mouseClick(lines, Qt::LeftButton);
    QCOMPARE(canvas->tool(), Tool::Line);
    QTest::keyClick(overlay, Qt::Key_Tab);
    QCOMPARE(canvas->tool(), Tool::Arrow);
    QTest::keyClick(overlay, Qt::Key_Tab);
    QCOMPARE(canvas->tool(), Tool::Line);
    canvas->setPenColor(Qt::blue);
    canvas->setPenWidth(7);
    QTest::mouseClick(overlay->findChild<QToolButton *>(QStringLiteral("荧光笔")), Qt::LeftButton);
    QCOMPARE(canvas->penColor(), QColor(255, 65, 65));
    QCOMPARE(canvas->penWidth(), 16.0);
    QCOMPARE(canvas->cursor().shape(), Qt::BitmapCursor);
    QCOMPARE(overlay->cursor().shape(), Qt::BitmapCursor);
    QTest::mouseClick(lines, Qt::LeftButton);
    QCOMPARE(canvas->penColor(), QColor(Qt::blue));
    QCOMPARE(canvas->penWidth(), 7.0);
    QTest::keyClick(overlay, Qt::Key_1);
    QCOMPARE(canvas->penWidth(), 8.0);
    QTest::keyClick(overlay, Qt::Key_2);
    QCOMPARE(canvas->penWidth(), 7.0);
    overlay->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void UiRegressionTest::proSizeRefreshAndOutput()
{
    QPointer<ui::SnipOverlay> overlay = new ui::SnipOverlay(
        QGuiApplication::primaryScreen(), false, false, QRect(40, 70, 260, 180));
    overlay->show();
    auto *canvas = overlay->findChild<ui::AnnotationCanvas *>();
    auto *sizeBar = overlay->findChild<QFrame *>(QStringLiteral("captureSizeToolbar"));
    auto *toolbar = overlay->findChild<QFrame *>(QStringLiteral("captureToolbar"));
    QVERIFY(canvas && sizeBar && sizeBar->isVisible());
    QVERIFY(overlay->rect().contains(sizeBar->geometry()));
    QVERIFY(overlay->rect().contains(toolbar->geometry()));
    int captures = 0;
    QImage source(overlay->size() * overlay->devicePixelRatioF(), QImage::Format_RGB32);
    source.fill(Qt::white);
    overlay->setBackgroundProvider([&] { ++captures; return source; });
    QTest::keyClick(overlay, Qt::Key_F5);
    QTRY_VERIFY(overlay->isVisible());
    QCOMPARE(captures, 1);

    QTest::mouseClick(overlay->findChild<QToolButton *>(QStringLiteral("captureSize")), Qt::LeftButton);
    auto *popup = overlay->findChild<QFrame *>(QStringLiteral("captureSizeEditor"));
    QVERIFY(popup && popup->isVisible());
    auto *width = popup->findChild<QSpinBox *>(QStringLiteral("captureWidth"));
    auto *height = popup->findChild<QSpinBox *>(QStringLiteral("captureHeight"));
    width->setValue(300); height->setValue(180);
    QTest::mouseClick(popup->findChild<QPushButton *>(QStringLiteral("applyCaptureSize")), Qt::LeftButton);
    QCOMPARE(canvas->renderedImage().size(), QSize(300, 180));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QTest::mouseClick(overlay->findChild<QToolButton *>(QStringLiteral("captureSize")), Qt::LeftButton);
    popup = overlay->findChild<QFrame *>(QStringLiteral("captureSizeEditor"));
    popup->findChild<QComboBox *>(QStringLiteral("captureSizeUnit"))->setCurrentIndex(1);
    popup->findChild<QSpinBox *>(QStringLiteral("captureWidth"))->setValue(240);
    popup->findChild<QSpinBox *>(QStringLiteral("captureHeight"))->setValue(160);
    QTest::mouseClick(popup->findChild<QPushButton *>(QStringLiteral("applyCaptureSize")), Qt::LeftButton);
    QCOMPARE(canvas->size(), QSize(240, 160));

    QTest::mouseClick(overlay->findChild<QToolButton *>(QStringLiteral("形状")), Qt::LeftButton);
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(30, 30));
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(160, 100));
    const QRect region = canvas->geometry();
    source.fill(QColor(20, 90, 180));
    QTest::keyClick(overlay, Qt::Key_F5);
    QTest::keyClick(overlay, Qt::Key_F5); // Repeated refresh must not queue another capture.
    QTRY_VERIFY(overlay->isVisible());
    QCOMPARE(captures, 2);
    QCOMPARE(canvas->geometry(), region);
    QVERIFY(canvas->canUndo());
    const QImage annotated = canvas->renderedImage();
    QCOMPARE(annotated.pixelColor(2, 2), QColor(20, 90, 180));
    canvas->undo();
    const QImage refreshed = canvas->renderedImage();
    QVERIFY(refreshed != annotated);
    QCOMPARE(refreshed.pixelColor(refreshed.rect().center()), QColor(20, 90, 180));
    canvas->redo();
    QCOMPARE(canvas->renderedImage(), annotated);
    QTest::keyClick(overlay, Qt::Key_Space);
    QVERIFY(!toolbar->isVisible());
    QTest::keyClick(overlay, Qt::Key_Space);
    QVERIFY(toolbar->isVisible());

    QTest::mouseClick(overlay->findChild<QToolButton *>(QStringLiteral("更多")), Qt::LeftButton);
    auto *more = overlay->findChild<QMenu *>(QStringLiteral("captureOutputMenu"));
    QVERIFY(more && more->isVisible());
    QVERIFY(more->actions().size() >= 8);
    more->close();
    QTest::mouseClick(overlay->findChild<QToolButton *>(QStringLiteral("分享")), Qt::LeftButton);
    auto *share = overlay->findChild<QMenu *>(QStringLiteral("captureShareMenu"));
    QVERIFY(share && share->isVisible());
    QCOMPARE(share->actions().size(), 3);
    share->actions().at(1)->trigger(); // Copy file, without launching an external application.
    const auto *mime = QApplication::clipboard()->mimeData();
    QVERIFY(mime->hasImage() && mime->hasUrls());
    QCOMPARE(QApplication::clipboard()->image(), annotated);
    const QString path = mime->urls().first().toLocalFile();
    const QImage saved = QImage(path).convertToFormat(annotated.format());
    QVERIFY(QFile::remove(path));
    QCOMPARE(saved, annotated);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(overlay.isNull());

    overlay = new ui::SnipOverlay(QGuiApplication::primaryScreen(), false, false, region);
    overlay->show();
    QSignalSpy ocr(overlay, &ui::SnipOverlay::ocrRequested);
    QTest::mouseClick(overlay->findChild<QToolButton *>(QStringLiteral("文字识别")), Qt::LeftButton);
    QCOMPARE(ocr.count(), 1);
    QVERIFY(!qvariant_cast<QImage>(ocr.first().first()).isNull());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void UiRegressionTest::proHistoryAndSelectionShortcuts()
{
    QPointer<ui::SnipOverlay> overlay = overlayForTest();
    overlay->resize(800, 600);
    const QRect element(60, 60, 100, 80), window(30, 40, 240, 220);
    overlay->setTargetDetector([=](QPoint, bool elements) { return elements ? element : window; });
    // QTest::mouseMove uses QCursor::setPos; offscreen can suppress it after popup tests.
    QMouseEvent hover(QEvent::MouseMove, QPointF(element.center()),
        QPointF(overlay->mapToGlobal(element.center())), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(overlay, &hover);
    QTest::keyClick(overlay, Qt::Key_1);
    QTest::keyClick(overlay, Qt::Key_2);
    QTest::keyClick(overlay, Qt::Key_1);
    QTest::mouseClick(overlay, Qt::LeftButton, Qt::NoModifier, element.center());
    auto *canvas = overlay->findChild<ui::AnnotationCanvas *>();
    QVERIFY(canvas);
    QCOMPARE(canvas->geometry(), window);
    QTest::keyClick(overlay, Qt::Key_Right);
    QCOMPARE(canvas->geometry(), window.translated(1, 0));
    QTest::keyClick(overlay, Qt::Key_1);
    QCOMPARE(canvas->geometry(), window.translated(1, 0).adjusted(-1, -1, 1, 1));
    QTest::keyClick(overlay, Qt::Key_2);
    QCOMPARE(canvas->geometry(), window.translated(1, 0));
    QTest::keyClick(overlay, Qt::Key_Down, snipaste::platform::primaryModifier());
    QCOMPARE(canvas->height(), window.height() + 1);
    QTest::keyClick(overlay, Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(canvas->height(), window.height());

    const QRect recent(50, 70, 200, 140), older(80, 100, 160, 100);
    overlay->setSelectionHistory({recent, older});
    QTest::keyClick(overlay, Qt::Key_R);
    QCOMPARE(overlay->findChild<ui::AnnotationCanvas *>()->geometry(), recent);
    QTest::keyClick(overlay, Qt::Key_R);
    QCOMPARE(overlay->findChild<ui::AnnotationCanvas *>()->geometry(), older);
    QTest::keyClick(overlay, Qt::Key_R, Qt::ShiftModifier);
    canvas = overlay->findChild<ui::AnnotationCanvas *>();
    QCOMPARE(canvas->geometry(), recent);
    const QImage live = canvas->renderedImage();
    const QRect historicalRegion(300, 160, 180, 120);
    QImage historyImage(historicalRegion.size(), QImage::Format_ARGB32_Premultiplied);
    historyImage.fill(Qt::green);
    overlay->setCaptureHistory({historyImage}, {{historicalRegion, overlay->size()}});
    QTest::keyClick(overlay, Qt::Key_Comma);
    canvas = overlay->findChild<ui::AnnotationCanvas *>();
    QCOMPARE(canvas->geometry(), historicalRegion);
    QCOMPARE(canvas->renderedImage(), historyImage);
    QTest::keyClick(overlay, Qt::Key_Period);
    canvas = overlay->findChild<ui::AnnotationCanvas *>();
    QCOMPARE(canvas->geometry(), recent);
    QCOMPARE(canvas->renderedImage(), live);
    QTest::keyClick(overlay, Qt::Key_A, snipaste::platform::primaryModifier());
    QCOMPARE(canvas->geometry(), overlay->rect());
    QVERIFY(overlay->rect().contains(overlay->findChild<QFrame *>(QStringLiteral("captureSizeToolbar"))->geometry()));
    overlay->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void UiRegressionTest::historyRestoresPairedGeometryAndBackground()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    core::ScreenshotHistory history;
    const QSize sourceDesktop(640, 480);
    const QList<QRect> regions{QRect(80, 80, 160, 80), QRect(320, 160, 160, 80),
                              QRect(80, 80, 160, 80)};
    const QList<QColor> colors{Qt::red, Qt::green, Qt::blue};
    for (int index = 0; index < regions.size(); ++index) {
        QImage background(sourceDesktop * 2, QImage::Format_RGB32);
        background.fill(colors.at(index));
        // A capture with annotations differs from the original desktop inside
        // its selection, but must not contaminate the stored clean background.
        QImage image(regions.at(index).size() * 2, QImage::Format_RGB32);
        image.fill(Qt::yellow);
        history.add(image, background, {regions.at(index), sourceDesktop});
    }
    QVERIFY(history.saveToDirectory(directory.path()));
    history.loadFromDirectory(directory.path());
    QCOMPARE(history.memoryUsageBytes(), 0);

    QPointer<ui::SnipOverlay> overlay = overlayForTest();
    overlay->resize(800, 600);
    overlay->setSelectionHistory({QRect(10, 20, 40, 50)});
    overlay->setCaptureHistory(history.images(), history.geometries(), history.backgrounds(),
        [&history](int index) { return history.backgroundAt(index); },
        [&history](int index) { return history.imageAt(index); });
    overlay->setMouseActions({}, false, {}, false, QStringLiteral("复制到剪贴板"), false);
    QSignalSpy captured(overlay, &ui::SnipOverlay::screenshotCapturedWithBackground);
    for (int index = 0; index < history.size(); ++index) {
        QTest::keyClick(overlay, Qt::Key_Comma);
        const QRect region = regions.at(2 - index);
        const QRect expected(qRound(region.x() * 1.25), qRound(region.y() * 1.25),
                             qRound(region.width() * 1.25), qRound(region.height() * 1.25));
        auto *canvas = overlay->findChild<ui::AnnotationCanvas *>();
        QVERIFY(canvas);
        QCOMPARE(canvas->geometry(), expected);
        QCOMPARE(canvas->renderedImage(), history.imageAt(index));
        QCOMPARE(overlay->captureGeometry().region, expected);
        QTest::keyClick(overlay, Qt::Key_Return);
        QCOMPARE(captured.count(), index + 1);
        const QImage restoredBackground = captured.last().at(1).value<QImage>();
        QCOMPARE(restoredBackground.size(), sourceDesktop * 2);
        QCOMPARE(restoredBackground.pixelColor(0, 0), colors.at(2 - index));
        QCOMPARE(restoredBackground.pixelColor(region.x() * 2, region.y() * 2), colors.at(2 - index));
        QCOMPARE(captured.last().at(0).value<QImage>(), history.imageAt(index));
    }
    overlay->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void UiRegressionTest::legacyHistoryUsesNeutralPreview()
{
    QPointer<ui::SnipOverlay> overlay = overlayForTest();
    overlay->resize(800, 600);
    overlay->setSelectionHistory({QRect(10, 20, 40, 50)});
    QTest::keyClick(overlay, Qt::Key_R);
    QImage image(QSize(160, 80), QImage::Format_RGB32);
    image.fill(Qt::green);
    int backgroundLoads = 0;
    overlay->setCaptureHistory({image}, {}, {}, [&backgroundLoads](int) {
        ++backgroundLoads;
        return QImage();
    });
    QTest::keyClick(overlay, Qt::Key_Comma);
    auto *canvas = overlay->findChild<ui::AnnotationCanvas *>();
    QVERIFY(canvas);
    QVERIFY(canvas->geometry() != QRect(10, 20, 40, 50));
    QCOMPARE(canvas->geometry().center(), overlay->rect().center());
    QCOMPARE(canvas->renderedImage(), image);
    QCOMPARE(backgroundLoads, 0);
    overlay->setMouseActions({}, false, {}, false, QStringLiteral("复制到剪贴板"), false);
    QSignalSpy captured(overlay, &ui::SnipOverlay::screenshotCapturedWithBackground);
    QTest::keyClick(overlay, Qt::Key_Return);
    QCOMPARE(captured.count(), 1);
    const QImage background = captured.first().at(1).value<QImage>();
    QCOMPARE(background.pixelColor(0, 0), QColor(32, 32, 32));
    QTest::keyClick(overlay, Qt::Key_Period);
    QCOMPARE(overlay->findChild<ui::AnnotationCanvas *>()->geometry(), QRect(10, 20, 40, 50));
    overlay->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void UiRegressionTest::proLayoutAndAnnotations()
{
    QPointer<ui::SnipOverlay> overlay = overlayForTest();
    // A larger synthetic desktop keeps the whole reference layout visible in QA artifacts.
    overlay->resize(1200, 720);
    QImage desktop(overlay->size() * overlay->devicePixelRatioF(), QImage::Format_RGB32);
    {
        QPainter p(&desktop);
        p.scale(overlay->devicePixelRatioF(), overlay->devicePixelRatioF());
        QLinearGradient gradient(0, 0, 1200, 720);
        gradient.setColorAt(0, QColor(30, 115, 230));
        gradient.setColorAt(1, QColor(5, 24, 65));
        p.fillRect(QRect(0, 0, 1200, 720), gradient);
        p.setPen(QColor(100, 180, 255, 65));
        for (int x = 0; x < 1200; x += 24) p.drawLine(x, 0, x, 720);
        for (int y = 0; y < 720; y += 24) p.drawLine(0, y, 1200, y);
    }
    overlay->setBackgroundProvider([desktop] { return desktop; });
    QTest::keyClick(overlay, Qt::Key_F5);
    QTRY_VERIFY(overlay->isVisible());
    const QRect region(280, 110, 840, 370);
    overlay->setTargetDetector([region](QPoint, bool) { return region; });
    QMouseEvent hover(QEvent::MouseMove, QPointF(710, 260),
        QPointF(overlay->mapToGlobal(QPoint(710, 260))), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(overlay, &hover);
    const QString artifactDir = qEnvironmentVariable("SNIPASTE_TEST_ARTIFACTS");
    const auto save = [&](const QString &name) {
        if (artifactDir.isEmpty()) return true;
        QDir().mkpath(artifactDir);
        return overlay->grab().save(QDir(artifactDir).filePath(
            name + QStringLiteral("-%1.png").arg(overlay->devicePixelRatioF())));
    };
    QVERIFY(save(QStringLiteral("pro-hover")));
    QTest::mouseClick(overlay, Qt::LeftButton, Qt::NoModifier, QPoint(710, 260));
    auto *canvas = overlay->findChild<ui::AnnotationCanvas *>();
    QVERIFY(canvas);
    auto *toolbar = overlay->findChild<QFrame *>(QStringLiteral("captureToolbar"));
    QVERIFY(overlay->rect().contains(toolbar->geometry()));
    QCOMPARE(toolbar->findChild<QFrame *>(QStringLiteral("captureActions"))->height(), 32);
    QVERIFY(save(QStringLiteral("pro-selection")));
    const auto draw = [&](const QString &tool, QPoint a, QPoint b) {
        QTest::mouseClick(overlay->findChild<QToolButton *>(tool), Qt::LeftButton);
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, a);
        QTest::mouseMove(canvas, b);
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, b);
    };
    draw(QStringLiteral("形状"), QPoint(25, 25), QPoint(230, 145));
    draw(QStringLiteral("荧光笔"), QPoint(280, 40), QPoint(480, 65));
    draw(QStringLiteral("画笔"), QPoint(290, 135), QPoint(510, 110));
    QTest::mouseClick(overlay->findChild<QToolButton *>(QStringLiteral("序号")), Qt::LeftButton);
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(60, 220));
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(120, 220));
    QTest::mouseClick(overlay->findChild<QToolButton *>(QStringLiteral("文字")), Qt::LeftButton);
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(180, 200));
    auto *editor = canvas->findChild<QPlainTextEdit *>();
    QVERIFY(editor->isVisible());
    editor->setPlainText(QStringLiteral("专业版界面 · 文字输入"));
    QTest::keyClick(editor, Qt::Key_Return, snipaste::platform::primaryModifier());
    const QImage beforeMagnifier = canvas->renderedImage();
    draw(QStringLiteral("放大镜标注"), QPoint(580, 90), QPoint(780, 270));
    const QImage enlarged = canvas->renderedImage();
    QVERIFY(enlarged != beforeMagnifier);
    const qreal scale = overlay->devicePixelRatioF();
    const QRect inner(qRound(640 * scale), qRound(140 * scale), qRound(50 * scale), qRound(50 * scale));
    QVERIFY(enlarged.copy(inner) != beforeMagnifier.copy(inner)); // Content is magnified, not just outlined.
    canvas->undo();
    QCOMPARE(canvas->renderedImage(), beforeMagnifier);
    canvas->redo();
    QCOMPARE(canvas->renderedImage(), enlarged);
    draw(QStringLiteral("马赛克"), QPoint(290, 20), QPoint(430, 90));
    QVERIFY(save(QStringLiteral("pro-annotations")));
    overlay->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void UiRegressionTest::inlineAnnotationsReachClipboard()
{
    QPointer<ui::SnipOverlay> overlay = overlayForTest();
    QSignalSpy captured(overlay, &ui::SnipOverlay::screenshotCaptured);
    QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, QPoint(50, 50));
    QTest::mouseMove(overlay, QPoint(650, 400));
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, QPoint(650, 400));
    auto *canvas = overlay->findChild<ui::AnnotationCanvas *>();
    QVERIFY(canvas);
    const QImage original = canvas->renderedImage();
    QTest::mouseClick(overlay->findChild<QToolButton *>(QStringLiteral("形状")), Qt::LeftButton);
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(30, 30));
    QTest::mouseMove(canvas, QPoint(180, 150));
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(180, 150));
    const QImage annotated = canvas->renderedImage();
    QVERIFY(annotated != original);
    auto *undo = overlay->findChild<QToolButton *>(QStringLiteral("撤销"));
    auto *redo = overlay->findChild<QToolButton *>(QStringLiteral("重做"));
    QVERIFY(undo->isEnabled());
    QTest::mouseClick(undo, Qt::LeftButton);
    QCOMPARE(canvas->renderedImage(), original);
    QVERIFY(redo->isEnabled());
    QTest::mouseClick(redo, Qt::LeftButton);
    QCOMPARE(canvas->renderedImage(), annotated);
    QTest::mouseClick(overlay->findChild<QToolButton *>(QStringLiteral("完成")), Qt::LeftButton);
    QCOMPARE(captured.count(), 1);
    QCOMPARE(qvariant_cast<QImage>(captured.at(0).at(0)), annotated);
    QCOMPARE(QApplication::clipboard()->image(), annotated);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(overlay.isNull());
}

void UiRegressionTest::autoCopySkipsToolbar()
{
    QPointer<ui::SnipOverlay> overlay = new ui::SnipOverlay(QGuiApplication::primaryScreen(), true);
    overlay->show();
    QSignalSpy captured(overlay, &ui::SnipOverlay::screenshotCaptured);
    bool toolbarVisibleOnCapture = true;
    connect(overlay, &ui::SnipOverlay::screenshotCaptured, this, [&] {
        toolbarVisibleOnCapture = overlay->findChild<QFrame *>(QStringLiteral("captureToolbar"))->isVisible();
    });
    QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, QPoint(40, 40));
    QTest::mouseMove(overlay, QPoint(200, 160));
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, QPoint(200, 160));
    QCOMPARE(captured.count(), 1);
    QVERIFY(!toolbarVisibleOnCapture);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(overlay.isNull());
}

void UiRegressionTest::annotationStylesAndEffectsSurviveUndo()
{
    QImage image(640, 420, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    ui::AnnotationCanvas canvas(image);
    canvas.resize(image.size());
    canvas.show();
    canvas.setPenColor(Qt::red);
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(30, 30));
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(180, 150));
    const QImage first = canvas.renderedImage();
    QTest::keyClick(&canvas, Qt::Key_Right, Qt::AltModifier);
    const QImage rotated = canvas.renderedImage();
    QVERIFY(rotated != first);
    QTest::keyClick(&canvas, Qt::Key_Left, Qt::AltModifier);
    QCOMPARE(canvas.renderedImage(), first);
    canvas.setPenColor(Qt::blue);
    canvas.setPenWidth(8);
    QCOMPARE(canvas.renderedImage(), first); // Styling a new tool must not recolor old marks.
    canvas.setTool(ui::AnnotationCanvas::Tool::Pen);
    QCOMPARE(canvas.cursor().shape(), Qt::BitmapCursor);
    canvas.setTool(ui::AnnotationCanvas::Tool::Text);
    QCOMPARE(canvas.cursor().shape(), Qt::CrossCursor);
    canvas.setTool(ui::AnnotationCanvas::Tool::Mosaic);
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(20, 20));
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(190, 160));
    const QImage mosaic = canvas.renderedImage();
    QVERIFY(mosaic != first);
    canvas.undo();
    QCOMPARE(canvas.renderedImage(), first);
    canvas.redo();
    QCOMPARE(canvas.renderedImage(), mosaic);
}

void UiRegressionTest::brushCursorsRemainDistinct()
{
    QImage image(640, 420, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    ui::AnnotationCanvas canvas(image);
    canvas.resize(image.size());
    canvas.show();

    canvas.setTool(ui::AnnotationCanvas::Tool::Pen);
    const QImage pencil = canvas.cursor().pixmap().toImage();
    canvas.setTool(ui::AnnotationCanvas::Tool::Marker);
    const QImage marker = canvas.cursor().pixmap().toImage();
    canvas.setTool(ui::AnnotationCanvas::Tool::Mosaic);
    const QImage mosaic = canvas.cursor().pixmap().toImage();
    canvas.setTool(ui::AnnotationCanvas::Tool::Blur);
    const QImage blur = canvas.cursor().pixmap().toImage();

    QVERIFY(!pencil.isNull());
    QVERIFY(!marker.isNull());
    QVERIFY(!mosaic.isNull());
    QVERIFY(!blur.isNull());
    QVERIFY(pencil != marker);
    QVERIFY(marker != mosaic);
    QVERIFY(mosaic != blur);

    const QImage blank = canvas.renderedImage(false);
    canvas.setTool(ui::AnnotationCanvas::Tool::Pen);
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(220, 160));
    QVERIFY(canvas.renderedImage(false) != blank);
    canvas.clearAnnotations();
    canvas.setTool(ui::AnnotationCanvas::Tool::Marker);
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(260, 180));
    QVERIFY(canvas.renderedImage(false) != blank);
    canvas.clearAnnotations();

    QImage patterned(240, 160, QImage::Format_ARGB32_Premultiplied);
    QPainter patternPainter(&patterned);
    for (int x = 0; x < patterned.width(); x += 8) {
        patternPainter.fillRect(QRect(x, 0, 4, patterned.height()), QColor(35, 35, 35));
        patternPainter.fillRect(QRect(x + 4, 0, 4, patterned.height()), QColor(220, 220, 220));
    }
    patternPainter.end();
    ui::AnnotationCanvas effects(patterned);
    effects.resize(patterned.size());
    effects.show();
    effects.setTool(ui::AnnotationCanvas::Tool::Mosaic);
    effects.setPenWidth(8.0);
    effects.setEffectSize(2);
    QTest::mousePress(&effects, Qt::LeftButton, Qt::NoModifier, QPoint(45, 80));
    QTest::mouseMove(&effects, QPoint(195, 80));
    QTest::mouseRelease(&effects, Qt::LeftButton, Qt::NoModifier, QPoint(195, 80));
    const QImage fineMosaic = effects.renderedImage(false);
    effects.clearAnnotations();
    effects.setEffectSize(30);
    QTest::mousePress(&effects, Qt::LeftButton, Qt::NoModifier, QPoint(45, 80));
    QTest::mouseMove(&effects, QPoint(195, 80));
    QTest::mouseRelease(&effects, Qt::LeftButton, Qt::NoModifier, QPoint(195, 80));
    const QImage coarseMosaic = effects.renderedImage(false);
    QVERIFY(fineMosaic != coarseMosaic);
    effects.clearAnnotations();
    effects.setEffectSize(8);
    effects.setPenWidth(5.0);
    QTest::mousePress(&effects, Qt::LeftButton, Qt::NoModifier, QPoint(120, 40));
    QTest::mouseMove(&effects, QPoint(120, 120));
    QTest::mouseRelease(&effects, Qt::LeftButton, Qt::NoModifier, QPoint(120, 120));
    const QImage narrowMosaic = effects.renderedImage(false);
    effects.clearAnnotations();
    effects.setPenWidth(24.0);
    QTest::mousePress(&effects, Qt::LeftButton, Qt::NoModifier, QPoint(120, 40));
    QTest::mouseMove(&effects, QPoint(120, 120));
    QTest::mouseRelease(&effects, Qt::LeftButton, Qt::NoModifier, QPoint(120, 120));
    const QImage wideMosaic = effects.renderedImage(false);
    QVERIFY(narrowMosaic != wideMosaic);

    QPointer<ui::SnipOverlay> overlay = new ui::SnipOverlay(
        QGuiApplication::primaryScreen(), false, false, QRect(40, 70, 280, 200));
    overlay->show();
    auto *overlayCanvas = overlay->findChild<ui::AnnotationCanvas *>();
    QVERIFY(overlayCanvas);
    const QList<ui::AnnotationCanvas::Tool> brushTools{
        ui::AnnotationCanvas::Tool::Pen,
        ui::AnnotationCanvas::Tool::Marker,
        ui::AnnotationCanvas::Tool::Mosaic};
    for (const auto brushTool : brushTools) {
        QToolButton *button = nullptr;
        for (auto *candidate : overlay->findChildren<QToolButton *>()) {
            if (candidate->property("annotationTool").toInt() == int(brushTool)) {
                button = candidate;
                break;
            }
        }
        QVERIFY(button);
        QTest::mouseClick(button, Qt::LeftButton);
        QCOMPARE(overlayCanvas->cursor().shape(), Qt::BitmapCursor);
        QCOMPARE(overlay->cursor().shape(), Qt::BitmapCursor);
        // The selection border is where the resize hit-test used to replace
        // the brush footprint with a horizontal/diagonal resize arrow.
        QTest::mouseMove(overlay, QPoint(40, 170));
        QCoreApplication::processEvents();
        QCOMPARE(overlay->cursor().shape(), Qt::BitmapCursor);
        QTest::mouseMove(overlay, QPoint(170, 170));
        QCoreApplication::processEvents();
        QCOMPARE(overlay->cursor().shape(), Qt::BitmapCursor);
    }
    // The blur option is a secondary toggle under the mosaic tool and used to
    // leave the parent overlay's resize cursor active until the next wheel
    // event.  Switching it must refresh both cursor owners immediately.
    QToolButton *blurOption = nullptr;
    for (auto *candidate : overlay->findChildren<QToolButton *>()) {
        if (candidate->property("effectToggle").toBool()) {
            blurOption = candidate;
            break;
        }
    }
    QVERIFY(blurOption);
    QTest::mouseClick(blurOption, Qt::LeftButton);
    QCoreApplication::processEvents();
    QCOMPARE(overlayCanvas->cursor().shape(), Qt::BitmapCursor);
    QCOMPARE(overlay->cursor().shape(), Qt::BitmapCursor);
    QTest::mouseClick(blurOption, Qt::LeftButton);
    QCoreApplication::processEvents();
    QCOMPARE(overlayCanvas->cursor().shape(), Qt::BitmapCursor);
    QCOMPARE(overlay->cursor().shape(), Qt::BitmapCursor);
    overlay->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    const QString artifactDir = qEnvironmentVariable("SNIPASTE_TEST_ARTIFACTS");
    if (!artifactDir.isEmpty()) {
        QDir().mkpath(artifactDir);
        QVERIFY(pencil.save(QDir(artifactDir).filePath(QStringLiteral("cursor-pencil.png"))));
        QVERIFY(marker.save(QDir(artifactDir).filePath(QStringLiteral("cursor-marker.png"))));
        QVERIFY(mosaic.save(QDir(artifactDir).filePath(QStringLiteral("cursor-mosaic.png"))));
        QVERIFY(blur.save(QDir(artifactDir).filePath(QStringLiteral("cursor-blur.png"))));
    }
}

void UiRegressionTest::mosaicCoverageMatchesBrushDiameter()
{
    QImage source(240, 160, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < source.height(); ++y) {
        for (int x = 0; x < source.width(); ++x)
            source.setPixelColor(x, y, QColor((x * 17 + y * 3) % 251,
                                              (x * 5 + y * 19) % 251,
                                              (x * 11 + y * 7) % 251));
    }

    const auto changedBounds = [](const QImage &before, const QImage &after) {
        QRect bounds;
        for (int y = 0; y < before.height(); ++y) {
            for (int x = 0; x < before.width(); ++x) {
                if (before.pixelColor(x, y) == after.pixelColor(x, y))
                    continue;
                const QRect pixel(x, y, 1, 1);
                bounds = bounds.isNull() ? pixel : bounds.united(pixel);
            }
        }
        return bounds;
    };

    for (const qreal width : {8.0, 64.0}) {
        ui::AnnotationCanvas canvas(source);
        canvas.resize(source.size());
        canvas.show();
        canvas.setTool(ui::AnnotationCanvas::Tool::Mosaic);
        canvas.setEffectSize(12);
        canvas.setPenWidth(width);

        const QImage before = canvas.renderedImage(false);
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(120, 80));
        const QImage after = canvas.renderedImage(false);
        const QRect changed = changedBounds(before, after);
        QVERIFY(!changed.isNull());
        // The filter may add at most one antialiased edge pixel.  Its changed
        // region must still track the actual circular brush diameter rather
        // than the effect block size or an oversized temporary rectangle.
        QVERIFY2(changed.width() <= qCeil(width) + 2,
                 qPrintable(QStringLiteral("width %1 changed region %2 exceeds brush")
                                .arg(width).arg(changed.width())));
        QVERIFY2(changed.height() <= qCeil(width) + 2,
                 qPrintable(QStringLiteral("width %1 changed region %2 exceeds brush")
                                .arg(width).arg(changed.height())));

        const QImage cursor = canvas.cursor().pixmap().toImage();
        QVERIFY(cursor.width() >= qCeil(width) + 8);
        QVERIFY(cursor.height() >= qCeil(width) + 8);
    }
}

void UiRegressionTest::mosaicRendersDuringDrag()
{
    QImage source(240, 160, QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&source);
    for (int x = 0; x < source.width(); x += 12)
        painter.fillRect(QRect(x, 0, 6, source.height()), QColor(30, 30, 30));
    painter.fillRect(QRect(0, 30, source.width(), 50), QColor(220, 220, 220));
    painter.end();

    ui::AnnotationCanvas canvas(source);
    canvas.resize(source.size());
    canvas.show();
    canvas.setTool(ui::AnnotationCanvas::Tool::Mosaic);
    canvas.setPenWidth(24.0);
    canvas.setEffectSize(8);
    QCoreApplication::processEvents();
    const QImage before = canvas.grab().toImage();

    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(70, 55));
    QTest::mouseMove(&canvas, QPoint(170, 55));
    QCoreApplication::processEvents();
    const QImage during = canvas.grab().toImage();
    QVERIFY2(during != before, "mosaic preview did not repaint while dragging");
    const QString artifactDir = qEnvironmentVariable("SNIPASTE_TEST_ARTIFACTS");
    if (!artifactDir.isEmpty()) {
        QDir().mkpath(artifactDir);
        QVERIFY(during.save(QDir(artifactDir).filePath(QStringLiteral("mosaic-stroke-preview.png"))));
    }
    // The uncommitted stroke remains outside the exported result until the
    // release, while the widget preview already shows it.
    QCOMPARE(canvas.renderedImage(false), source);

    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(170, 55));
    const QImage committed = canvas.renderedImage(false);
    QVERIFY(committed != source);
    if (!artifactDir.isEmpty()) {
        QDir().mkpath(artifactDir);
        QVERIFY(committed.save(QDir(artifactDir).filePath(QStringLiteral("mosaic-stroke.png"))));
    }
}

void UiRegressionTest::mosaicEffectShapesUseOneAlgorithm()
{
    QImage source(260, 180, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < source.height(); ++y) {
        for (int x = 0; x < source.width(); ++x) {
            source.setPixelColor(x, y, QColor((x * 13 + y * 5) % 251,
                                              (x * 7 + y * 17) % 251,
                                              (x * 19 + y * 3) % 251));
        }
    }

    const auto render = [&](ui::AnnotationCanvas::EffectShape shape) {
        ui::AnnotationCanvas canvas(source);
        canvas.resize(source.size());
        canvas.show();
        canvas.setTool(ui::AnnotationCanvas::Tool::Mosaic);
        canvas.setEffectShape(shape);
        canvas.setEffectSize(10);
        if (shape == ui::AnnotationCanvas::EffectShape::Brush)
            canvas.setPenWidth(42.0);
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(70, 50));
        if (shape == ui::AnnotationCanvas::EffectShape::Brush)
            QTest::mouseMove(&canvas, QPoint(190, 120));
        else
            QTest::mouseMove(&canvas, QPoint(190, 130));
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier,
                            shape == ui::AnnotationCanvas::EffectShape::Brush
                                ? QPoint(190, 120) : QPoint(190, 130));
        if (!canvas.canUndo())
            return QImage();
        return canvas.renderedImage(false);
    };

    const QImage brush = render(ui::AnnotationCanvas::EffectShape::Brush);
    const QImage rectangle = render(ui::AnnotationCanvas::EffectShape::Rectangle);
    const QImage ellipse = render(ui::AnnotationCanvas::EffectShape::Ellipse);
    QVERIFY(brush != source);
    QVERIFY(rectangle != source);
    QVERIFY(ellipse != source);

    // The mask changes only where the selected geometry covers the source.
    // The same pixelation kernel is used regardless of which mask produced
    // the selected pixels.
    const auto hasChange = [&](const QImage &image, const QRect &area) {
        int changed = 0;
        for (int y = area.top(); y <= area.bottom(); ++y)
            for (int x = area.left(); x <= area.right(); ++x)
                if (image.pixelColor(x, y) != source.pixelColor(x, y))
                    ++changed;
        return changed > 0;
    };
    QVERIFY(hasChange(rectangle, source.rect()));
    QVERIFY(hasChange(ellipse, source.rect()));
    QVERIFY(rectangle.pixelColor(10, 10) == source.pixelColor(10, 10));
    QVERIFY(ellipse.pixelColor(10, 10) == source.pixelColor(10, 10));

    ui::SnipOverlay overlay(QGuiApplication::primaryScreen(), false, false,
                            QRect(40, 70, 300, 220));
    overlay.show();
    auto *canvas = overlay.findChild<ui::AnnotationCanvas *>();
    QVERIFY(canvas);
    QToolButton *mosaicButton = nullptr;
    for (auto *button : overlay.findChildren<QToolButton *>()) {
        if (button->property("annotationTool").toInt()
            == int(ui::AnnotationCanvas::Tool::Mosaic)) {
            mosaicButton = button;
            break;
        }
    }
    QVERIFY(mosaicButton);
    QTest::mouseClick(mosaicButton, Qt::LeftButton);
    const auto shapeButtons = overlay.findChildren<QToolButton *>();
    int shapeCount = 0;
    for (auto *button : shapeButtons) {
        if (button->property("effectShapeOption").toBool())
            ++shapeCount;
    }
    QCOMPARE(shapeCount, 2);
    for (auto *button : shapeButtons) {
        if (button->property("effectShape").toInt()
            == int(ui::AnnotationCanvas::EffectShape::Ellipse)) {
            QTest::mouseClick(button, Qt::LeftButton);
            break;
        }
    }
    QCOMPARE(canvas->effectShape(), ui::AnnotationCanvas::EffectShape::Ellipse);
    QCOMPARE(canvas->cursor().shape(), Qt::CrossCursor);
    // The first three compact controls are brush presets; selecting one
    // returns from rectangle/ellipse coverage to the continuous brush.
    QToolButton *brushPreset = nullptr;
    for (auto *button : shapeButtons) {
        if (!button->property("effectShapeOption").toBool()
            && button->text() == QStringLiteral("4")) {
            brushPreset = button;
            break;
        }
    }
    QVERIFY(brushPreset);
    QTest::mouseClick(brushPreset, Qt::LeftButton);
    QCOMPARE(canvas->effectShape(), ui::AnnotationCanvas::EffectShape::Brush);
    QVERIFY(canvas->cursor().shape() == Qt::BitmapCursor);
    QToolButton *effectToggle = nullptr;
    for (auto *button : shapeButtons) {
        if (button->property("effectToggle").toBool()) {
            effectToggle = button;
            break;
        }
    }
    QVERIFY(effectToggle);
    QTest::mouseClick(effectToggle, Qt::LeftButton);
    QCOMPARE(canvas->tool(), ui::AnnotationCanvas::Tool::Blur);
    QTest::mouseClick(effectToggle, Qt::LeftButton);
    QCOMPARE(canvas->tool(), ui::AnnotationCanvas::Tool::Mosaic);
    overlay.close();
}

void UiRegressionTest::brushesContinueOverExistingStrokes_data()
{
    QTest::addColumn<int>("tool");
    QTest::addColumn<int>("shape");
    using Tool = ui::AnnotationCanvas::Tool;
    using Shape = ui::AnnotationCanvas::EffectShape;
    QTest::newRow("pencil") << int(Tool::Pen) << int(Shape::Brush);
    QTest::newRow("marker") << int(Tool::Marker) << int(Shape::Brush);
    QTest::newRow("mosaic-brush") << int(Tool::Mosaic) << int(Shape::Brush);
    QTest::newRow("blur-brush") << int(Tool::Blur) << int(Shape::Brush);
    QTest::newRow("mosaic-rectangle") << int(Tool::Mosaic) << int(Shape::Rectangle);
    QTest::newRow("mosaic-ellipse") << int(Tool::Mosaic) << int(Shape::Ellipse);
}

void UiRegressionTest::brushesContinueOverExistingStrokes()
{
    QFETCH(int, tool);
    QFETCH(int, shape);
    QImage source(640, 420, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < source.height(); ++y)
        for (int x = 0; x < source.width(); ++x)
            source.setPixelColor(x, y, QColor((x * 13 + y * 5) % 251,
                                               (x * 7 + y * 17) % 251,
                                               (x * 19 + y * 3) % 251));
    ui::AnnotationCanvas canvas(source);
    canvas.resize(source.size());
    canvas.show();
    canvas.setAutoSelectOnCreation(true);
    canvas.setTool(ui::AnnotationCanvas::Tool(tool));
    canvas.setEffectShape(ui::AnnotationCanvas::EffectShape(shape));
    canvas.setPenWidth(24);
    const auto stroke = [&](QPoint start, QPoint end, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QTest::mousePress(&canvas, Qt::LeftButton, modifiers, start);
        QMouseEvent move(QEvent::MouseMove, end, canvas.mapToGlobal(end),
                         Qt::NoButton, Qt::LeftButton, modifiers);
        QCoreApplication::sendEvent(&canvas, &move);
        QTest::mouseRelease(&canvas, Qt::LeftButton, modifiers, end);
    };
    stroke(QPoint(100, 100), QPoint(200, 160));
    const QImage first = canvas.renderedImage(false);
    QVERIFY(first != source);

    // Starting inside the previous mark must create another stroke rather
    // than translating the old mark. Undo must remove only the new stroke.
    stroke(QPoint(160, 140), QPoint(260, 240));
    const QImage second = canvas.renderedImage(false);
    QVERIFY(second != first);
    QCOMPARE(second.pixel(110, 106), first.pixel(110, 106));
    canvas.undo();
    QCOMPARE(canvas.renderedImage(false), first);
    canvas.redo();
    QCOMPARE(canvas.renderedImage(false), second);

    // Ctrl must not duplicate/move brush marks either.
    stroke(QPoint(160, 140), QPoint(260, 240), snipaste::platform::primaryModifier());
    canvas.undo();
    QCOMPARE(canvas.renderedImage(false), second);
}

void UiRegressionTest::eraserRemovesOnlyCoveredStrokeRegion()
{
    QImage image(640, 420, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    ui::AnnotationCanvas canvas(image);
    canvas.resize(image.size());
    canvas.show();

    canvas.setTool(ui::AnnotationCanvas::Tool::Pen);
    canvas.setPenColor(Qt::red);
    canvas.setPenWidth(6);
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(50, 210));
    QTest::mouseMove(&canvas, QPoint(590, 210));
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(590, 210));
    const QImage before = canvas.renderedImage(false);
    QVERIFY(before.pixelColor(180, 210).red() > 180);
    QVERIFY(before.pixelColor(460, 210).red() > 180);

    canvas.setTool(ui::AnnotationCanvas::Tool::Eraser);
    canvas.setPenWidth(24);
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(320, 210));
    const QImage after = canvas.renderedImage(false);
    QVERIFY(after.pixelColor(180, 210).red() > 180);
    QVERIFY(after.pixelColor(460, 210).red() > 180);
    QVERIFY(after.pixelColor(320, 210).green() > 180);
    QVERIFY(after.pixelColor(320, 210).blue() > 180);

    canvas.undo();
    QCOMPARE(canvas.renderedImage(false), before);
    canvas.redo();
    QCOMPARE(canvas.renderedImage(false), after);
}

void UiRegressionTest::eraserRemovesOnlyCoveredShapeRegion()
{
    QImage image(640, 420, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    ui::AnnotationCanvas canvas(image);
    canvas.resize(image.size());
    canvas.show();

    canvas.setTool(ui::AnnotationCanvas::Tool::Rectangle);
    canvas.setPenColor(Qt::red);
    canvas.setPenWidth(4);
    canvas.setShapeFilled(true);
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(100, 100));
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(300, 300));
    const QImage before = canvas.renderedImage(false);
    QVERIFY(before.pixelColor(200, 200).red() > 180);
    QVERIFY(before.pixelColor(120, 120).red() > 180);

    canvas.setTool(ui::AnnotationCanvas::Tool::Eraser);
    canvas.setPenWidth(40);
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(200, 200));
    const QImage after = canvas.renderedImage(false);
    QVERIFY(after.pixelColor(200, 200).green() > 180);
    QVERIFY(after.pixelColor(200, 200).blue() > 180);
    QVERIFY(after.pixelColor(120, 120).red() > 180);

    canvas.undo();
    QCOMPARE(canvas.renderedImage(false), before);
    canvas.redo();
    QCOMPARE(canvas.renderedImage(false), after);
}

void UiRegressionTest::eraserRemovesMagnifierInteriorLocally()
{
    QImage image(640, 420, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    ui::AnnotationCanvas canvas(image);
    canvas.resize(image.size());
    canvas.show();

    // Put a mark in the sampled area.  The magnifier enlarges it into the
    // target frame, where the eraser must be able to remove only the covered
    // part of the displayed content.
    canvas.setTool(ui::AnnotationCanvas::Tool::Pen);
    canvas.setPenColor(Qt::red);
    canvas.setPenWidth(6);
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(90, 100));
    QTest::mouseMove(&canvas, QPoint(130, 100));
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(130, 100));

    canvas.setTool(ui::AnnotationCanvas::Tool::Magnifier);
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(80, 80));
    QTest::mouseMove(&canvas, QPoint(140, 140));
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(140, 140));
    const QImage before = canvas.renderedImage(false);
    QVERIFY(before.pixelColor(90, 90).red() > 180); // source (100, 100), 2x
    QVERIFY(before.pixelColor(100, 100).red() > 180); // source remains intact

    canvas.setTool(ui::AnnotationCanvas::Tool::Eraser);
    canvas.setPenWidth(8);
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(90, 90));
    const QImage after = canvas.renderedImage(false);
    const QColor erasedTarget = after.pixelColor(90, 90);
    QVERIFY2(erasedTarget.green() > 180 && erasedTarget.blue() > 180,
             qPrintable(QStringLiteral("target pixel is %1,%2,%3")
                            .arg(erasedTarget.red()).arg(erasedTarget.green())
                            .arg(erasedTarget.blue())));
    QVERIFY(after.pixelColor(100, 100).red() > 180);

    canvas.undo();
    QCOMPARE(canvas.renderedImage(false), before);
    canvas.redo();
    QCOMPARE(canvas.renderedImage(false), after);
}

void UiRegressionTest::annotationHistoryMixedOperations()
{
    QImage source(640, 420, QImage::Format_ARGB32_Premultiplied);
    source.fill(Qt::white);
    ui::AnnotationCanvas canvas(source);
    canvas.resize(source.size());
    canvas.show();
    QVector<QImage> states{source};
    const auto save = [&] { states.append(canvas.renderedImage(false)); };
    canvas.setTool(ui::AnnotationCanvas::Tool::Pen);
    canvas.setPenWidth(8);
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(50, 210));
    QMouseEvent strokeMove(QEvent::MouseMove, QPoint(590, 210), canvas.mapToGlobal(QPoint(590, 210)),
                           Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &strokeMove);
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(590, 210));
    save();
    canvas.setTool(ui::AnnotationCanvas::Tool::Eraser);
    canvas.setPenWidth(24);
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(200, 210));
    save();
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(360, 210));
    for (const QPoint point : {QPoint(380, 210), QPoint(400, 210)}) {
        QMouseEvent move(QEvent::MouseMove, point, canvas.mapToGlobal(point),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&canvas, &move);
    }
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(420, 210));
    save();
    canvas.setTool(ui::AnnotationCanvas::Tool::Rectangle);
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(80, 80));
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(180, 140));
    save();
    QTest::keyClick(&canvas, Qt::Key_Delete);
    save();
    for (int index = states.size() - 2; index >= 0; --index) {
        QVERIFY(canvas.canUndo());
        QTest::keyClick(&canvas, Qt::Key_Z, platform::primaryModifier());
        QCOMPARE(canvas.renderedImage(false), states.at(index));
    }
    QVERIFY(!canvas.canUndo());
    for (int index = 1; index < states.size(); ++index) {
        QVERIFY(canvas.canRedo());
        QTest::keyClick(&canvas, Qt::Key_Y, platform::primaryModifier());
        QCOMPARE(canvas.renderedImage(false), states.at(index));
    }
    QVERIFY(!canvas.canRedo());
}

void UiRegressionTest::annotationHistoryShapeEdits()
{
    QImage source(640, 420, QImage::Format_ARGB32_Premultiplied);
    source.fill(Qt::white);
    ui::AnnotationCanvas canvas(source);
    canvas.resize(source.size());
    canvas.show();
    canvas.setPenWidth(4);
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(100, 100));
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(240, 200));
    QVector<QImage> states{source, canvas.renderedImage(false)};
    const auto drag = [&](QPoint from, QPoint middle, QPoint to) {
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, from);
        for (const QPoint point : {middle, to}) {
            QMouseEvent move(QEvent::MouseMove, point, canvas.mapToGlobal(point),
                             Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&canvas, &move);
        }
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, to);
        states.append(canvas.renderedImage(false));
        QVERIFY(states.constLast() != states.at(states.size() - 2));
    };
    drag(QPoint(170, 150), QPoint(190, 160), QPoint(210, 180));
    drag(QPoint(286, 236), QPoint(306, 246), QPoint(326, 266));
    drag(QPoint(230, 104), QPoint(275, 140), QPoint(321, 195));
    canvas.setShapeFilled(true);
    states.append(canvas.renderedImage(false));
    QTest::keyClick(&canvas, Qt::Key_Right, Qt::AltModifier);
    states.append(canvas.renderedImage(false));
    QTest::keyClick(&canvas, Qt::Key_C, platform::primaryModifier() | Qt::ShiftModifier);
    QTest::keyClick(&canvas, Qt::Key_V, platform::primaryModifier() | Qt::ShiftModifier);
    states.append(canvas.renderedImage(false));
    for (int index = states.size() - 2; index >= 0; --index) {
        canvas.undo();
        QCOMPARE(canvas.renderedImage(false), states.at(index));
    }
    QVERIFY(!canvas.canUndo());
    for (int index = 1; index < states.size(); ++index) {
        canvas.redo();
        QCOMPARE(canvas.renderedImage(false), states.at(index));
    }
    QVERIFY(!canvas.canRedo());
}

void UiRegressionTest::annotationHistoryClearAndBranch()
{
    QImage source(640, 420, QImage::Format_ARGB32_Premultiplied);
    source.fill(Qt::white);
    ui::AnnotationCanvas canvas(source);
    canvas.resize(source.size());
    canvas.show();
    canvas.setTool(ui::AnnotationCanvas::Tool::Pen);
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(80, 80));
    const QImage first = canvas.renderedImage(false);
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(200, 200));
    const QImage second = canvas.renderedImage(false);
    canvas.clearAnnotations();
    QCOMPARE(canvas.renderedImage(false), source);
    QVERIFY(canvas.canUndo());
    canvas.undo();
    QCOMPARE(canvas.renderedImage(false), second);
    canvas.redo();
    QCOMPARE(canvas.renderedImage(false), source);
    canvas.undo();
    canvas.undo();
    QCOMPARE(canvas.renderedImage(false), first);
    canvas.setTool(ui::AnnotationCanvas::Tool::Eraser);
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(500, 350));
    QVERIFY(canvas.canRedo()); // Erasing blank space must preserve the redo branch.
    canvas.setTool(ui::AnnotationCanvas::Tool::Rectangle);
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(400, 300));
    QVERIFY(canvas.canRedo()); // A cancelled/zero-size shape is not an edit.
    canvas.setTool(ui::AnnotationCanvas::Tool::Pen);
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(300, 300));
    QVERIFY(!canvas.canRedo());
    canvas.undo();
    QCOMPARE(canvas.renderedImage(false), first);
    canvas.resetCapture();
    QVERIFY(!canvas.canUndo() && !canvas.canRedo());
}

void UiRegressionTest::annotationHistoryCaptureOffset()
{
    QImage source(640, 420, QImage::Format_ARGB32_Premultiplied);
    source.fill(Qt::white);
    ui::AnnotationCanvas canvas(source);
    canvas.resize(source.size());
    canvas.show();
    canvas.setTool(ui::AnnotationCanvas::Tool::Pen);
    canvas.setPenWidth(8);
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(50, 210));
    QMouseEvent strokeMove(QEvent::MouseMove, QPoint(590, 210), canvas.mapToGlobal(QPoint(590, 210)),
                           Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &strokeMove);
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(590, 210));
    canvas.setTool(ui::AnnotationCanvas::Tool::Eraser);
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(200, 210));
    const QImage erased = canvas.renderedImage(false);
    canvas.undo();
    const QImage drawn = canvas.renderedImage(false);
    QVERIFY(erased != drawn);
    canvas.setCaptureImage(source, QPointF(0, 20));
    const auto translated = [&](const QImage &image) {
        QImage result = source;
        QPainter painter(&result);
        painter.drawImage(QPoint(0, 20), image);
        painter.end();
        return result;
    };
    QCOMPARE(canvas.renderedImage(false), translated(drawn));
    canvas.redo();
    QCOMPARE(canvas.renderedImage(false), translated(erased));
    canvas.undo();
    QCOMPARE(canvas.renderedImage(false), translated(drawn));
    canvas.undo();
    QCOMPARE(canvas.renderedImage(false), source);
}

void UiRegressionTest::annotationHistoryNumberSequence()
{
    QImage source(640, 420, QImage::Format_ARGB32_Premultiplied);
    source.fill(Qt::white);
    ui::AnnotationCanvas canvas(source);
    canvas.resize(source.size());
    canvas.show();
    canvas.setAutoSelectOnCreation(false);
    canvas.setTool(ui::AnnotationCanvas::Tool::Number);
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(100, 100));
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(200, 200));
    const QImage second = canvas.renderedImage(false);
    canvas.undo();
    canvas.redo();
    QCOMPARE(canvas.renderedImage(false), second);
    canvas.undo();
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(200, 200));
    QCOMPARE(canvas.renderedImage(false), second); // A new branch reuses number 2.
}

void UiRegressionTest::magnifierFramesMoveIndependently()
{
    QImage image(640, 420, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    ui::AnnotationCanvas canvas(image);
    canvas.resize(image.size());
    canvas.show();
    canvas.setTool(ui::AnnotationCanvas::Tool::Magnifier);

    // The initial gesture selects the small source region.  The magnifier
    // creates the larger display frame around that selection.
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(80, 80));
    QTest::mouseMove(&canvas, QPoint(140, 140));
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(140, 140));
    const QImage attached = canvas.renderedImage();

    QRect targetOutline;
    for (int y = 0; y < attached.height(); ++y) {
        for (int x = 0; x < attached.width(); ++x) {
            const QColor color = attached.pixelColor(x, y);
            if (color.red() > 210 && color.green() < 120 && color.blue() < 120) {
                const QRect pixel(x, y, 1, 1);
                targetOutline = targetOutline.isNull()
                    ? pixel : targetOutline.united(pixel);
            }
        }
    }
    // The selected 60x60 source is displayed in a 2x target frame.
    QVERIFY(!targetOutline.isNull());
    QVERIFY(targetOutline.width() >= 118 && targetOutline.width() <= 123);
    QVERIFY(targetOutline.height() >= 118 && targetOutline.height() <= 123);

    // Drag the outer portion of the target.  The source frame stays at the
    // original location and the detached connector becomes visible.
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(60, 60));
    QTest::mouseMove(&canvas, QPoint(290, 60));
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(290, 60));
    const QImage detached = canvas.renderedImage();
    QVERIFY(detached != attached);
    QVERIFY(detached.pixelColor(80, 110).red() > 180);  // source frame remains
    QVERIFY(detached.pixelColor(360, 50).red() > 180);  // target moved right
    QVERIFY(detached.pixelColor(230, 110).red() > 180); // connector line

    // Clicking the small source frame moves only the sampling range.
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(110, 110));
    QTest::mouseMove(&canvas, QPoint(140, 140));
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(140, 140));
    const QImage sourceMoved = canvas.renderedImage();
    QVERIFY(sourceMoved != detached);
    QVERIFY(sourceMoved.pixelColor(360, 50).red() > 180); // target stayed put

    // A detached connector must stop before the target frame, so it cannot
    // paint a white line through the magnified rectangle's interior.
    QImage dark(640, 420, QImage::Format_ARGB32_Premultiplied);
    dark.fill(Qt::black);
    ui::AnnotationCanvas detachedCanvas(dark);
    detachedCanvas.resize(dark.size());
    detachedCanvas.show();
    detachedCanvas.setTool(ui::AnnotationCanvas::Tool::Magnifier);
    QTest::mousePress(&detachedCanvas, Qt::LeftButton, Qt::NoModifier, QPoint(80, 80));
    QTest::mouseMove(&detachedCanvas, QPoint(140, 140));
    QTest::mouseRelease(&detachedCanvas, Qt::LeftButton, Qt::NoModifier, QPoint(140, 140));
    QTest::mousePress(&detachedCanvas, Qt::LeftButton, Qt::NoModifier, QPoint(60, 60));
    QTest::mouseMove(&detachedCanvas, QPoint(290, 60));
    QTest::mouseRelease(&detachedCanvas, Qt::LeftButton, Qt::NoModifier, QPoint(290, 60));
    const QImage darkDetached = detachedCanvas.renderedImage();
    for (int y = 52; y < 168; ++y) {
        for (int x = 302; x < 418; ++x) {
            const QColor color = darkDetached.pixelColor(x, y);
            QVERIFY2(!(color.red() > 220 && color.green() > 220 && color.blue() > 220),
                     "magnifier connector entered the target interior");
        }
    }
}

void UiRegressionTest::inlineTextInputAndCancel()
{
    QPointer<ui::SnipOverlay> overlay = new ui::SnipOverlay(QGuiApplication::primaryScreen(),
                                                         false, false, QRect(30, 30, 300, 220));
    overlay->show();
    QSignalSpy captured(overlay, &ui::SnipOverlay::screenshotCaptured);
    auto *canvas = overlay->findChild<ui::AnnotationCanvas *>();
    auto *editor = canvas->findChild<QPlainTextEdit *>(QStringLiteral("annotationTextEditor"));
    QVERIFY(editor);
    const QImage original = canvas->renderedImage();
    QTest::mouseClick(overlay->findChild<QToolButton *>(QStringLiteral("文字")), Qt::LeftButton);
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(25, 25));
    QVERIFY(editor->isVisible());
    QTRY_VERIFY(editor->hasFocus());
    QVERIFY(!QApplication::activeModalWidget());
    QTest::keyClicks(editor, "Alpha ");
    QInputMethodEvent composition(QStringLiteral("zhongwen"), {});
    QApplication::sendEvent(editor, &composition);
    QVERIFY(editor->isVisible());
    QInputMethodEvent input;
    input.setCommitString(QStringLiteral("中文"));
    QApplication::sendEvent(editor, &input);
    QTest::keyClick(editor, Qt::Key_Return);
    QTest::keyClicks(editor, "Beta");
    QCOMPARE(editor->toPlainText(), QStringLiteral("Alpha 中文\nBeta"));
    QCOMPARE(captured.count(), 0); // Enter inserts a line, not a screenshot copy/close.
    const QImage pending = canvas->renderedImage();
    QVERIFY(pending != original);
    const QString artifactDir = qEnvironmentVariable("SNIPASTE_TEST_ARTIFACTS");
    if (!artifactDir.isEmpty()) {
        QDir().mkpath(artifactDir);
        QVERIFY(overlay->grab().save(QDir(artifactDir).filePath(
            QStringLiteral("inline-text-%1.png").arg(overlay->devicePixelRatioF()))));
    }
    QTest::keyClick(editor, Qt::Key_Return, snipaste::platform::primaryModifier());
    QVERIFY(!editor->isVisible());
    QVERIFY(overlay->isVisible());
    QCOMPARE(canvas->renderedImage(), pending);
    QTest::keyClick(canvas, Qt::Key_Z, snipaste::platform::primaryModifier());
    QCOMPARE(canvas->renderedImage(), original);
    QTest::keyClick(canvas, Qt::Key_Y, snipaste::platform::primaryModifier());
    QCOMPARE(canvas->renderedImage(), pending);
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(25, 125));
    QTest::keyClicks(editor, "Discard this");
    QTest::keyClick(editor, Qt::Key_Escape);
    QVERIFY(!editor->isVisible());
    QVERIFY(overlay->isVisible());
    QCOMPARE(canvas->renderedImage(), pending);
    QTest::mouseClick(overlay->findChild<QToolButton *>(QStringLiteral("完成")), Qt::LeftButton);
    QCOMPARE(captured.count(), 1);
    QCOMPARE(QApplication::clipboard()->image(), pending);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(overlay.isNull());
}

void UiRegressionTest::ctrlASelectsCurrentScreenOnFirstPress()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);
    ui::SnipOverlay overlay(screen, false, false, QRect(20, 20, 160, 120));
    QSignalSpy selectionCaptured(&overlay, &ui::SnipOverlay::selectionCaptured);
    overlay.show();
    QTest::qWait(10);
    QKeyEvent press(QEvent::KeyPress, Qt::Key_A, snipaste::platform::primaryModifier());
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_A, snipaste::platform::primaryModifier());
    QCoreApplication::sendEvent(&overlay, &press);
    QCoreApplication::sendEvent(&overlay, &release);
    QKeyEvent confirmPress(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QKeyEvent confirmRelease(QEvent::KeyRelease, Qt::Key_Return, Qt::NoModifier);
    QCoreApplication::sendEvent(&overlay, &confirmPress);
    QCoreApplication::sendEvent(&overlay, &confirmRelease);
    QTRY_COMPARE(selectionCaptured.count(), 1);
    QCOMPARE(selectionCaptured.at(0).at(0).toRect(), screen->geometry());
}

void UiRegressionTest::inlineTextOutput_data()
{
    QTest::addColumn<QString>("action");
    QTest::newRow("copy-while-editing") << QStringLiteral("完成");
    QTest::newRow("pin-while-editing") << QStringLiteral("贴图");
    QTest::newRow("global-paste-while-editing") << QStringLiteral("global-paste");
}

void UiRegressionTest::inlineTextOutput()
{
    QFETCH(QString, action);
    QPointer<ui::SnipOverlay> overlay = new ui::SnipOverlay(QGuiApplication::primaryScreen(),
                                                         false, false, QRect(30, 30, 300, 220));
    overlay->show();
    QSignalSpy captured(overlay, &ui::SnipOverlay::screenshotCaptured);
    QSignalSpy pasted(overlay, &ui::SnipOverlay::pasteRequested);
    auto *canvas = overlay->findChild<ui::AnnotationCanvas *>();
    auto *editor = canvas->findChild<QPlainTextEdit *>(QStringLiteral("annotationTextEditor"));
    QVERIFY(editor);
    QTest::mouseClick(overlay->findChild<QToolButton *>(QStringLiteral("文字")), Qt::LeftButton);
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(25, 25));
    QTest::keyClicks(editor, "Keep pending text");
    const QImage pending = canvas->renderedImage();
    QVERIFY(editor->isVisible());
    if (action == QStringLiteral("global-paste"))
        overlay->pasteSelection();
    else
        QTest::mouseClick(overlay->findChild<QToolButton *>(action), Qt::LeftButton);
    QCOMPARE(captured.count(), 1);
    QCOMPARE(qvariant_cast<QImage>(captured.first().first()), pending);
    if (action == QStringLiteral("贴图") || action == QStringLiteral("global-paste")) {
        QCOMPARE(pasted.count(), 1);
        QCOMPARE(qvariant_cast<QImage>(pasted.first().first()), pending);
    } else {
        QCOMPARE(QApplication::clipboard()->image(), pending);
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(overlay.isNull());
}

void UiRegressionTest::textEditorCleanupAndToolSwitch()
{
    QImage image(640, 420, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPointer<ui::AnnotationCanvas> canvas = new ui::AnnotationCanvas(image);
    canvas->show();
    canvas->setTool(ui::AnnotationCanvas::Tool::Text);
    auto *editor = canvas->findChild<QPlainTextEdit *>(QStringLiteral("annotationTextEditor"));
    QVERIFY(editor);
    const QImage original = canvas->renderedImage();
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvas->rect().bottomRight() - QPoint(8, 8));
    QVERIFY(canvas->rect().contains(editor->geometry()));
    QTest::keyClicks(editor, "Edge text");
    QVERIFY(canvas->rect().contains(editor->geometry()));
    canvas->setTool(ui::AnnotationCanvas::Tool::Rectangle);
    QVERIFY(!editor->isVisible());
    QVERIFY(canvas->renderedImage() != original);
    canvas->undo();
    QCOMPARE(canvas->renderedImage(), original);
    canvas->setTool(ui::AnnotationCanvas::Tool::Text);
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(50, 50));
    QTest::keyClicks(editor, "Close with active input");
    delete canvas.data();
    QCoreApplication::processEvents();
    QVERIFY(canvas.isNull());
}

void UiRegressionTest::pastePreservesOriginalPixels_data()
{
    QTest::addColumn<QSize>("pixels");
    QTest::addColumn<qreal>("imageDpr");
    QTest::newRow("small-clipboard-without-dpr") << QSize(400, 240) << qreal(1);
    QTest::newRow("large-original") << QSize(1800, 1200) << qreal(1);
    QTest::newRow("image-with-dpr-metadata") << QSize(600, 360) << qreal(2);
}

void UiRegressionTest::pastePreservesOriginalPixels()
{
    QFETCH(QSize, pixels);
    QFETCH(qreal, imageDpr);
    QImage source(pixels, QImage::Format_ARGB32_Premultiplied);
    source.fill(Qt::cyan);
    source.setPixelColor(pixels.width() - 2, pixels.height() - 2, Qt::red);
    source.setDevicePixelRatio(imageDpr);
    ui::PasteManager manager;
    auto *pin = manager.createPaste(source);
    QVERIFY(pin);
    QCOMPARE(pin->sourceImage(), source);
    const qreal ratio = pin->devicePixelRatioF();
    const QString configuredScale = qEnvironmentVariable("QT_SCALE_FACTOR");
    if (!configuredScale.isEmpty())
        QCOMPARE(ratio, configuredScale.toDouble());
    QCOMPARE(pin->size(), QSize(qRound(pixels.width() / ratio), qRound(pixels.height() / ratio)));
    QCOMPARE(pin->displayedImage().devicePixelRatio(), qreal(1.0));
    // Inspect the actual rendered device-pixel extent, not just QWidget geometry.
    const QPixmap rendered = pin->grab();
    QVERIFY(qAbs(rendered.width() - pixels.width()) <= 1);
    QVERIFY(qAbs(rendered.height() - pixels.height()) <= 1);
    QCOMPARE(pin->scale(), 1.0);

    QTest::keyClick(pin, Qt::Key_1);
    QCOMPARE(pin->size(), QSize(qRound(pixels.height() / ratio), qRound(pixels.width() / ratio)));
    pin->restoreTransform(1, true, false, false, false, 0.5, false, true);
    pin->showExisting();
    QCOMPARE(pin->size(), QSize(qRound(pixels.height() / ratio / 2), qRound(pixels.width() / ratio / 2)));
    pin->restoreTransform(0, false, false, false, false, 1.0, false, true);
    pin->showExisting();
    QCOMPARE(pin->sourceImage(), source); // Zoom/rotation must never shrink the stored original.
    QCOMPARE(pin->size(), QSize(qRound(pixels.width() / ratio), qRound(pixels.height() / ratio)));
}

void UiRegressionTest::pasteDefaultsOpaque()
{
    ui::PasteManager manager;
    QImage image(96, 64, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::yellow);
    QPointer<ui::PasteWindow> pin = manager.createPaste(image);
    QVERIFY(pin);
    QCOMPARE(pin->windowOpacity(), 1.0);
}

void UiRegressionTest::pasteOutlineSurvivesFlashAndZoom_data()
{
    QTest::addColumn<bool>("shadow");
    QTest::newRow("without-shadow") << false;
    QTest::newRow("with-shadow") << true;
}

void UiRegressionTest::pasteOutlineSurvivesFlashAndZoom()
{
    QFETCH(bool, shadow);
    QImage source(301, 199, QImage::Format_ARGB32_Premultiplied);
    source.fill(QColor(57, 62, 72));
    ui::PasteManager manager;
    manager.setShadowEnabled(shadow);
    auto *pin = manager.createPaste(source);
    QVERIFY(pin);
    const QString artifactDir = qEnvironmentVariable("SNIPASTE_TEST_ARTIFACTS");
    const auto saveFrame = [&](const QString &stage) {
        if (!artifactDir.isEmpty()) {
            QDir().mkpath(artifactDir);
            if (QGuiApplication::platformName() == QStringLiteral("windows")) {
                pin->screen()->grabWindow(0, pin->x(), pin->y(), pin->width(), pin->height())
                    .save(QDir(artifactDir).filePath(
                        QStringLiteral("paste-outline-%1-%2-%3-screen.png")
                            .arg(pin->devicePixelRatioF()).arg(shadow).arg(stage)));
            }
            pin->grab().save(QDir(artifactDir).filePath(
                QStringLiteral("paste-outline-%1-%2-%3.png")
                    .arg(pin->devicePixelRatioF()).arg(shadow).arg(stage)));
        }
    };
    const auto outlineVisible = [&] {
        const auto *buffer = dynamic_cast<const QImage *>(pin->backingStore()->paintDevice());
        if (!buffer || buffer->isNull())
            return false;
        const int thickness = qCeil(pin->devicePixelRatioF());
        for (int side = 0; side < 4; ++side) {
            bool visible = false;
            for (int inset = 0; inset < thickness; ++inset) {
                const int x = side == 0 ? inset : side == 1 ? buffer->width() - 1 - inset
                                                                          : buffer->width() / 2;
                const int y = side == 2 ? inset : side == 3 ? buffer->height() - 1 - inset
                                                                           : buffer->height() / 2;
                const QColor color = buffer->pixelColor(x, y);
                visible |= color.red() > 220 && color.green() > 160 && color.blue() < 100;
            }
            if (!visible)
                return false;
        }
        return true;
    };
    QTest::qWait(20);
    saveFrame(QStringLiteral("initial"));
    QVERIFY(outlineVisible());
    QTRY_VERIFY_WITH_TIMEOUT(!pin->isFlashing(), 1500);
    QTest::qWait(20);
    saveFrame(QStringLiteral("idle"));
    QVERIFY2(outlineVisible(), "The permanent outline disappeared after the initial flash ended.");
    for (int step = 0; step < 26; ++step) {
        const QPointF point(pin->rect().center());
        QWheelEvent wheel(point, pin->mapToGlobal(point.toPoint()), {}, QPoint(0, -120),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(pin, &wheel);
        QTest::qWait(10);
        QVERIFY2(outlineVisible(), "Wheel zoom must retain a visible outline on all four edges.");
    }
    QCOMPARE(pin->scale(), qreal(0.1));
    saveFrame(QStringLiteral("small"));
    QCOMPARE(pin->sourceImage(), source);
}

void UiRegressionTest::pasteDisplayKeepsNativePixels_data()
{
    QTest::addColumn<bool>("shadow");
    QTest::newRow("without-shadow") << false;
    QTest::newRow("with-shadow") << true;
}

void UiRegressionTest::pasteDisplayKeepsNativePixels()
{
    QFETCH(bool, shadow);
    const qreal ratio = QGuiApplication::primaryScreen()->devicePixelRatio();
    QImage source(QSize(qRound(240 * ratio), qRound(160 * ratio)),
                  QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < source.height(); ++y)
        for (int x = 0; x < source.width(); ++x)
            source.setPixelColor(x, y, QColor((x * 43 + y * 17) % 251,
                                               (x * 23 + y * 61) % 251,
                                               (x * 37 + y * 7) % 251));
    ui::PasteManager manager;
    manager.setShadowEnabled(shadow);
    auto *pin = manager.createPaste(source);
    QVERIFY(pin);
    QCoreApplication::processEvents();
    const QImage display = pin->grab().toImage().convertToFormat(source.format());
    QCOMPARE(display.size(), source.size());
    const QRect content = source.rect().adjusted(8, 8, -8, -8);
    const QString artifactDir = qEnvironmentVariable("SNIPASTE_TEST_ARTIFACTS");
    if (!artifactDir.isEmpty()) {
        QDir().mkpath(artifactDir);
        const QString suffix = QStringLiteral("%1-%2").arg(ratio).arg(shadow);
        QVERIFY(display.save(QDir(artifactDir).filePath(QStringLiteral("paste-display-%1.png").arg(suffix))));
        QVERIFY(source.save(QDir(artifactDir).filePath(QStringLiteral("paste-source-%1.png").arg(suffix))));
    }
    QCOMPARE(display.copy(content), source.copy(content));
}

void UiRegressionTest::pasteSelectionClipboardSize_data()
{
    QTest::addColumn<QString>("action");
    QTest::newRow("clipboard-f3") << QStringLiteral("clipboard");
    QTest::newRow("direct-pin") << QStringLiteral("direct");
    QTest::newRow("auto-copy-f3") << QStringLiteral("auto");
}

void UiRegressionTest::pasteSelectionClipboardSize()
{
    QFETCH(QString, action);
    QWidget backdrop;
    backdrop.setGeometry(QGuiApplication::primaryScreen()->geometry());
    backdrop.setAutoFillBackground(true);
    backdrop.show();
    QCoreApplication::processEvents();
    const QRect selection(30, 30, 240, 160);
    QPointer<ui::SnipOverlay> overlay = new ui::SnipOverlay(QGuiApplication::primaryScreen(),
                                                         false, false, selection);
    ui::PasteManager manager;
    QRect captureRegion;
    QPointer<ui::PasteWindow> pin;
    connect(overlay, &ui::SnipOverlay::selectionCaptured, this,
            [&](const QRect &region) { captureRegion = region; });
    connect(overlay, &ui::SnipOverlay::screenshotCaptured, this,
            [&](const QImage &image) { manager.rememberCapture(image, captureRegion); });
    connect(overlay, &ui::SnipOverlay::pasteRequested, this,
            [&](const QImage &image) { pin = manager.createPaste(image); });
    overlay->show();
    auto *canvas = overlay->findChild<ui::AnnotationCanvas *>();
    QVERIFY(canvas);
    const QImage selected = canvas->renderedImage();
    QVERIFY(!selected.isNull());
    const qreal ratio = overlay->devicePixelRatioF();
    QCOMPARE(selected.size(), QSize(qRound(selection.width() * ratio), qRound(selection.height() * ratio)));
    if (action == QStringLiteral("direct"))
        QTest::mouseClick(overlay->findChild<QToolButton *>(QStringLiteral("贴图")), Qt::LeftButton);
    else if (action == QStringLiteral("auto"))
        overlay->captureSelectionImmediately();
    else
        QTest::mouseClick(overlay->findChild<QToolButton *>(QStringLiteral("完成")), Qt::LeftButton);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(overlay.isNull());
    if (action != QStringLiteral("direct")) {
        QCOMPARE(QApplication::clipboard()->image(), selected);
        pin = manager.pasteClipboard(QApplication::clipboard()->image());
    }
    QVERIFY(pin);
    QCOMPARE(pin->sourceImage(), selected);
    QCOMPARE(pin->geometry(), selection.translated(QGuiApplication::primaryScreen()->geometry().topLeft()));
}

void UiRegressionTest::pasteCaptureOriginAndClipboardIsolation()
{
    QImage screenshot(240, 160, QImage::Format_RGB32);
    screenshot.fill(Qt::red);
    QImage unrelated(screenshot.size(), screenshot.format());
    unrelated.fill(Qt::blue);
    const QRect firstRegion(31, 42, 240, 160);
    const QRect secondRegion(65, 85, 240, 160);
    ui::PasteManager manager;
    manager.rememberCapture(screenshot, firstRegion);
    auto *first = manager.pasteClipboard(screenshot);
    QCOMPARE(first->pos(), firstRegion.topLeft());
    first->move(95, 105);
    QTest::mouseDClick(first, Qt::LeftButton);
    QCOMPARE(manager.pasteClipboard(screenshot), first);
    QCOMPARE(first->pos(), QPoint(95, 105)); // Restoring keeps the manually moved position.
    QTest::mouseDClick(first, Qt::LeftButton);
    manager.rememberCapture(screenshot, secondRegion); // Same pixels, new screen location.
    auto *second = manager.pasteClipboard(screenshot);
    QVERIFY(second != first);
    QCOMPARE(second->pos(), secondRegion.topLeft());
    auto *external = manager.pasteClipboard(unrelated);
    QVERIFY(external->pos() != secondRegion.topLeft()); // Same dimensions are insufficient.
    manager.setUseCapturePosition(false);
    manager.rememberCapture(screenshot, firstRegion);
    auto *centered = manager.createPaste(screenshot);
    QVERIFY(centered->pos() != firstRegion.topLeft());
    manager.setUseCapturePosition(true);
    auto *atCapture = manager.createPaste(screenshot);
    QCOMPARE(atCapture->pos(), firstRegion.topLeft());
}

void UiRegressionTest::pasteClickAndDragCursor()
{
    QImage image(240, 160, QImage::Format_RGB32);
    image.fill(Qt::yellow);
    ui::PasteManager manager;
    auto *pin = manager.createPaste(image);
    const QPoint edge = QGuiApplication::primaryScreen()->availableGeometry().topLeft() + QPoint(5, 5);
    pin->move(edge);
    const QPoint hit(30, 30);
    QTest::mouseMove(pin, hit);
    QCOMPARE(pin->cursor().shape(), Qt::ArrowCursor);
    QTest::mousePress(pin, Qt::LeftButton, Qt::NoModifier, hit);
    QCOMPARE(pin->cursor().shape(), Qt::ArrowCursor);
    QTest::mouseMove(pin, hit + QPoint(1, 1));
    QTest::mouseRelease(pin, Qt::LeftButton, Qt::NoModifier, hit + QPoint(1, 1));
    QCOMPARE(pin->pos(), edge); // A click must not snap to the nearby screen edge.
    QTest::mouseDClick(pin, Qt::LeftButton, Qt::NoModifier, hit);
    QTest::mouseRelease(pin, Qt::LeftButton, Qt::NoModifier, hit);
    QVERIFY(!pin->isVisible());
    QCOMPARE(manager.pasteClipboard({}), pin);
    QCOMPARE(pin->pos(), edge);
    QTest::mousePress(pin, Qt::LeftButton, Qt::NoModifier, hit);
    const QPoint delta(QApplication::startDragDistance() + 20, 30);
    QTest::mouseMove(pin, hit + delta);
    QCOMPARE(pin->cursor().shape(), Qt::ArrowCursor);
    QCOMPARE(pin->pos(), edge + delta);
    QTest::mouseRelease(pin, Qt::LeftButton, Qt::NoModifier, hit);
    QCOMPARE(pin->cursor().shape(), Qt::ArrowCursor);
}

void UiRegressionTest::pasteDoubleClickRestores()
{
    QImage source(400, 240, QImage::Format_RGB32);
    source.fill(Qt::yellow);
    ui::PasteManager manager;
    QPointer<ui::PasteWindow> pin = manager.pasteClipboard(source);
    QSignalSpy quitting(qApp, &QCoreApplication::aboutToQuit);
    pin->restoreTransform(1, true, false, false, false, 0.5, false, true);
    pin->showExisting();
    pin->setWindowOpacity(0.75);
    pin->move(75, 85);
    const QRect geometry = pin->geometry();
    const qreal opacity = pin->windowOpacity();
    QTest::mouseClick(pin, Qt::LeftButton);
    QCOMPARE(manager.selectedCount(), 1);
    QTest::mouseDClick(pin, Qt::RightButton);
    QTest::mouseDClick(pin, Qt::LeftButton, Qt::ShiftModifier);
    QVERIFY(pin->isVisible()); // Shift+double-click is reserved for thumbnail mode.
    QTest::mouseDClick(pin, Qt::LeftButton);
    QTest::mouseRelease(pin, Qt::LeftButton);
    QVERIFY(!pin->isVisible());
    QCOMPARE(pin->state(), ui::PasteWindow::State::Closed);
    QCOMPARE(manager.selectedCount(), 0);
    manager.showAll();
    manager.toggleAll();
    const int secondGroup = manager.createGroup();
    manager.switchGroup(secondGroup);
    manager.switchGroup(0);
    QVERIFY(!pin->isVisible()); // Group/show-all operations cannot reopen a closed pin.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(pin);
    QCOMPARE(manager.pasteClipboard(source), pin.data());
    QVERIFY(pin->isVisible());
    QCOMPARE(pin->geometry(), geometry);
    QCOMPARE(pin->scale(), 0.5);
    QCOMPARE(pin->rotationQuarterTurns(), 1);
    QCOMPARE(pin->windowOpacity(), opacity);
    QCOMPARE(pin->cursor().shape(), Qt::ArrowCursor);
    QTest::keyClick(pin, Qt::Key_Escape);
    QCOMPARE(manager.pasteClipboard(source), pin.data());
    QTest::keyClick(pin, Qt::Key_W, snipaste::platform::primaryModifier());
    QCOMPARE(manager.pasteClipboard(source), pin.data());
    QCOMPARE(quitting.count(), 0);
}

void UiRegressionTest::pasteRecoveryLimitAndDestruction()
{
    ui::PasteManager manager;
    manager.setRestoreCount(2);
    QImage source(240, 160, QImage::Format_RGB32);
    source.fill(Qt::green);
    QPointer<ui::PasteWindow> oldest = manager.createPaste(source);
    QPointer<ui::PasteWindow> middle = manager.createPaste(source);
    QPointer<ui::PasteWindow> newest = manager.createPaste(source);
    QTest::mouseDClick(oldest, Qt::LeftButton);
    QTest::mouseDClick(middle, Qt::LeftButton);
    QTest::mouseDClick(newest, Qt::LeftButton);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(oldest.isNull());
    QCOMPARE(manager.pasteClipboard({}), newest.data());
    QCOMPARE(manager.pasteClipboard({}), middle.data());
    QVERIFY(!manager.pasteClipboard({}));
    QTest::keyClick(newest, Qt::Key_Escape, Qt::ShiftModifier);
    QVERIFY(!manager.pasteClipboard({})); // Cannot revive pending-deletion windows either.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(newest.isNull());
    QTest::mouseDClick(middle, Qt::LeftButton);
    manager.setRestoreCount(0);
    QVERIFY(!manager.pasteClipboard({}));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(middle.isNull());
    QPointer<ui::PasteWindow> noRecovery = manager.createPaste(source);
    QTest::mouseDClick(noRecovery, Qt::LeftButton);
    QVERIFY(!manager.pasteClipboard({}));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(noRecovery.isNull());
}

void UiRegressionTest::pasteNewClipboardTakesPriority()
{
    QImage first(240, 160, QImage::Format_RGB32);
    QImage second(300, 180, QImage::Format_RGB32);
    first.fill(Qt::red);
    second.fill(Qt::blue);
    ui::PasteManager manager;
    auto *oldPin = manager.pasteClipboard(first);
    oldPin->move(65, 75);
    QTest::mouseDClick(oldPin, Qt::LeftButton);
    auto *newPin = manager.pasteClipboard(second);
    QVERIFY(newPin != oldPin);
    QCOMPARE(newPin->sourceImage(), second);
    QVERIFY(!oldPin->isVisible());
    QCOMPARE(manager.pasteClipboard(second), oldPin);
    QCOMPARE(oldPin->pos(), QPoint(65, 75));
    // Direct "pin to screen" also recovers the same pin on the first F3.
    ui::PasteManager direct;
    first.setDevicePixelRatio(2);
    auto *directPin = direct.createPaste(first);
    QTest::mouseDClick(directPin, Qt::LeftButton);
    first.setDevicePixelRatio(1); // Simulate metadata stripped by a clipboard round trip.
    QCOMPARE(direct.pasteClipboard(first), directPin);
}

void UiRegressionTest::pasteGroupOrdering()
{
    ui::PasteManager manager;
    const int first = manager.createGroup(QStringLiteral("第一"));
    const int second = manager.createGroup(QStringLiteral("第二"));
    QCOMPARE(manager.groupNames(), QStringList({QStringLiteral("默认组"),
                                                QStringLiteral("第一"),
                                                QStringLiteral("第二")}));
    manager.switchGroup(second);
    manager.moveGroup(second, 0);
    QCOMPARE(manager.groupNames(), QStringList({QStringLiteral("第二"),
                                                QStringLiteral("默认组"),
                                                QStringLiteral("第一")}));
    QCOMPARE(manager.activeGroup(), 0);
    manager.moveGroup(0, 2);
    QCOMPARE(manager.groupNames(), QStringList({QStringLiteral("默认组"),
                                                QStringLiteral("第一"),
                                                QStringLiteral("第二")}));
    QCOMPARE(manager.activeGroup(), 2);
    Q_UNUSED(first);

    ui::PasteManager pins;
    QImage image(80, 60, QImage::Format_RGB32);
    image.fill(Qt::cyan);
    auto *one = pins.createPaste(image);
    auto *two = pins.createPaste(image);
    QCOMPARE(pins.windowsInGroup(0).size(), 2);
    pins.moveWindowInGroup(0, 1, 0);
    QCOMPARE(pins.windowsInGroup(0).at(0), two);
    QCOMPARE(pins.windowsInGroup(0).at(1), one);
}

void UiRegressionTest::pastePersistence()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString statePath = directory.filePath(QStringLiteral("pastes"));
    QImage source(1800, 1200, QImage::Format_RGB32);
    source.fill(Qt::magenta);
    ui::PasteManager manager;
    auto *hidden = manager.createPaste(source);
    hidden->restoreTransform(1, false, true, false, false, 0.5, false, true);
    hidden->move(60, 70);
    auto *closed = manager.createPaste(source);
    QTest::mouseDClick(closed, Qt::LeftButton);
    manager.hideAll();
    manager.saveState(statePath);
    QFile stateFile(QDir(statePath).filePath(QStringLiteral("state.json")));
    QVERIFY(stateFile.open(QIODevice::ReadOnly));
    const auto groups = QJsonDocument::fromJson(stateFile.readAll()).object().value(QStringLiteral("groups")).toArray();
    const auto windows = groups.first().toObject().value(QStringLiteral("windows")).toArray();
    QCOMPARE(windows.size(), 1); // Hidden pins persist; individually closed pins do not.
    const auto saved = windows.first().toObject();
    QVERIFY(!saved.value(QStringLiteral("visible")).toBool());
    QImage stored(QDir(statePath).filePath(saved.value(QStringLiteral("image")).toString()));
    QCOMPARE(stored, source);
    manager.destroyAll();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    ui::PasteManager restored;
    restored.loadState(statePath);
    QCOMPARE(restored.visibleCount(), 0);
    QVERIFY(!restored.pasteClipboard({})); // Group hiding does not populate the close queue.
    restored.showAll();
    QCOMPARE(restored.visibleCount(), 1);
    ui::PasteWindow *pin = nullptr;
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (auto *candidate = qobject_cast<ui::PasteWindow *>(widget))
            pin = candidate;
    }
    QVERIFY(pin);
    QCOMPARE(pin->sourceImage(), source);
    QCOMPARE(pin->pos(), QPoint(60, 70));
    QCOMPARE(pin->rotationQuarterTurns(), 1);
    QCOMPARE(pin->scale(), 0.5);
    QCOMPARE(pin->size(), QSize(qRound(600 / pin->devicePixelRatioF()), qRound(900 / pin->devicePixelRatioF())));
    QTest::mouseDClick(pin, Qt::LeftButton);
    QCOMPARE(restored.pasteClipboard({}), pin); // Loaded pins must be connected too.
}

void UiRegressionTest::pastePersistenceReusesPixelsAndHandlesFailure()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QDir dir(directory.path());
    const QString statePath = dir.filePath(QStringLiteral("state.json"));
    QImage source(320, 240, QImage::Format_RGB32);
    source.fill(Qt::magenta);
    ui::PasteManager manager;
    auto *first = manager.createPaste(source);
    auto *second = manager.createPaste(source);
    manager.hideAll();
    manager.saveState(directory.path());
    QStringList images = dir.entryList({QStringLiteral("paste-*.png")}, QDir::Files);
    QCOMPARE(images.size(), 1); // Two shared source images encode once.
    const QString originalPath = dir.filePath(images.first());
    QFile imageFile(originalPath);
    QVERIFY(imageFile.open(QIODevice::ReadWrite));
    QVERIFY(imageFile.setFileTime(QDateTime::currentDateTime().addDays(-1), QFileDevice::FileModificationTime));
    imageFile.close();
    const QDateTime originalTime = QFileInfo(originalPath).lastModified();
    first->move(60, 70);
    second->restoreTransform(1, true, false, true, false, 0.5, false, true);
    manager.saveState(directory.path());
    QCOMPARE(dir.entryList({QStringLiteral("paste-*.png")}, QDir::Files), images);
    QCOMPARE(QFileInfo(originalPath).lastModified(), originalTime);
    manager.destroyAll();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    ui::PasteManager restored;
    restored.loadState(directory.path());
    restored.saveState(directory.path());
    QCOMPARE(QFileInfo(originalPath).lastModified(), originalTime);
    restored.destroyAll();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    source.fill(Qt::cyan);
    restored.createPaste(source);
    restored.hideAll();
    // A directory at the index destination forces the atomic commit to fail.
    QVERIFY(QFile::remove(statePath));
    QVERIFY(dir.mkdir(QStringLiteral("state.json")));
    restored.saveState(directory.path());
    QVERIFY(QFileInfo::exists(originalPath));
    QCOMPARE(dir.entryList({QStringLiteral("paste-*.png")}, QDir::Files), images);
    QVERIFY(dir.rmdir(QStringLiteral("state.json")));
    restored.saveState(directory.path());
    images = dir.entryList({QStringLiteral("paste-*.png")}, QDir::Files);
    QCOMPARE(images.size(), 1);
    QVERIFY(!QFileInfo::exists(originalPath));
    QCOMPARE(QImage(dir.filePath(images.first())), source);
}

void UiRegressionTest::independentWidthPresetsAndCornerRadius()
{
    QPointer<ui::SnipOverlay> overlay = new ui::SnipOverlay(
        QGuiApplication::primaryScreen(), false, false, QRect(40, 70, 280, 200));
    overlay->show();
    auto *canvas = overlay->findChild<ui::AnnotationCanvas *>();
    auto *shapes = overlay->findChild<QToolButton *>(QStringLiteral("形状"));
    QVERIFY(canvas && shapes);
    QTest::mouseClick(shapes, Qt::LeftButton);

    auto *options = overlay->findChild<QFrame *>(QStringLiteral("captureOptions"));
    QVERIFY(options);
    auto *stack = options->findChild<QStackedLayout *>();
    QVERIFY(stack);
    auto *activePage = stack->currentWidget();
    QVERIFY(activePage);
    auto *preset1 = activePage->findChild<QToolButton *>(QStringLiteral("penWidth2"));
    auto *preset2 = activePage->findChild<QToolButton *>(QStringLiteral("penWidth4"));
    auto *preset3 = activePage->findChild<QToolButton *>(QStringLiteral("penWidth8"));
    QVERIFY(preset1 && preset2 && preset3);
    QVERIFY(preset1->isChecked());

    // Switch to preset 2
    QTest::mouseClick(preset2, Qt::LeftButton);
    QVERIFY(preset2->isChecked());
    QVERIFY(!preset1->isChecked());
    QCOMPARE(canvas->penWidth(), 4.0);

    // Adjust width while preset 2 is selected
    canvas->setPenWidth(6.0);
    QCOMPARE(preset2->property("presetWidth").toReal(), 6.0);

    // Switch to preset 1
    QTest::mouseClick(preset1, Qt::LeftButton);
    QVERIFY(preset1->isChecked());
    QVERIFY(!preset2->isChecked());
    QCOMPARE(canvas->penWidth(), 2.0);

    // Switch back to preset 2: remembers 6.0!
    QTest::mouseClick(preset2, Qt::LeftButton);
    QVERIFY(preset2->isChecked());
    QCOMPARE(canvas->penWidth(), 6.0);

    // Test corner radius: find rect button
    QToolButton *btnRect = nullptr;
    for (auto *b : options->findChildren<QToolButton *>()) {
        if (b->toolTip().contains(QStringLiteral("圆角半径"))) {
            btnRect = b;
            break;
        }
    }
    QVERIFY(btnRect);
    QVERIFY(btnRect->toolTip().contains(QStringLiteral("点击或悬停")));

    // Verify captureOptions geometry matches activePage with exactly 1px border and no gaps
    QCOMPARE(options->height(), activePage->height() + 2);
    QCOMPARE(options->width(), activePage->width() + 2);
    QCOMPARE(activePage->geometry().topLeft(), QPoint(1, 1));

    // Also switch to Mosaic tool and verify options bar dimensions
    auto *mosaic = overlay->findChild<QToolButton *>(QStringLiteral("马赛克"));
    QVERIFY(mosaic);
    QTest::mouseClick(mosaic, Qt::LeftButton);
    auto *mosaicPage = stack->currentWidget();
    QVERIFY(mosaicPage);
    auto *mosaicSlider = mosaicPage->findChild<QSlider *>();
    QVERIFY(mosaicSlider);
    QCOMPARE(mosaicSlider->value(), 6);
    QCOMPARE(options->height(), 26);
    QCOMPARE(options->height(), mosaicPage->height() + 2);
    QCOMPARE(options->width(), mosaicPage->width() + 2);
    QCOMPARE(mosaicPage->geometry().topLeft(), QPoint(1, 1));

    overlay->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
#ifdef Q_OS_WIN
    // The offscreen plugin does not load the Windows font database automatically.
    const int font = QFontDatabase::addApplicationFont(
        QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("Fonts/msyh.ttc")));
    if (font >= 0)
        app.setFont(QFont(QFontDatabase::applicationFontFamilies(font).first(), 9));
#endif
    UiRegressionTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "ui_regression_test.moc"
