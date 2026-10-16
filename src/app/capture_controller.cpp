#include "app/capture_controller.h"
#include "app/external_command.h"
#include "app/runtime_settings.h"
#include "app/screenshot_history_service.h"
#include "app/screenshot_output.h"
#include "core/config/config_store.h"
#include "core/recognition/barcode_service.h"
#include "platform/desktop_integration.h"
#include "ui/annotation/annotation_dialog.h"
#include "ui/clipboard_output.h"
#include "ui/output/image_output.h"
#include "ui/paste/paste_manager.h"
#include "ui/snip/capture_support.h"
#include "ui/snip/snip_overlay.h"
#include "ui/snip/window_capture_overlay.h"
#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDebug>
#include <QFileInfo>
#include <QMessageBox>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>

namespace snipaste::app
{

using snipaste::ui::ensureMacScreenCaptureAccess;

using snipaste::ui::printImage;

using snipaste::platform::trimIdleProcessMemory;

CaptureController::CaptureController(core::ConfigStore &config, ScreenshotHistoryService &history,
                                     ScreenshotOutput &output, ui::PasteManager &pastes, QObject *parent)
    : QObject(parent), m_config(config), m_history(history), m_output(output), m_pastes(pastes)
{
    m_lastSelection = config.value(QStringLiteral("capture/lastRegion")).toRect();
    for (const QVariant &region : config.value(QStringLiteral("capture/recentRegions")).toList())
        if (region.toRect().isValid())
            m_recentSelections.append(region.toRect());
    if (m_recentSelections.isEmpty() && m_lastSelection.isValid())
        m_recentSelections.append(m_lastSelection);
    applySettings();
    if (auto *screen = QGuiApplication::primaryScreen())
        m_preparedOverlay.reset(ui::SnipOverlay::createPrepared(screen));
}
CaptureController::~CaptureController()
{
    delete m_windowOverlay.data();
}
void CaptureController::applySettings()
{
    m_maxHistoryRegions =
        qBound(1, m_config.value(QStringLiteral("capture/maxHistoryRegions"), 8).toInt(), 99);
}
bool CaptureController::active() const
{
    return m_pendingCaptures > 0 || m_activeOverlay || m_windowOverlay;
}
bool CaptureController::pasteSelection()
{
    return m_activeOverlay && m_activeOverlay->isVisible() && m_activeOverlay->pasteSelection();
}
ui::SnipOverlay *CaptureController::overlay() const
{
    return m_activeOverlay.data();
}
ui::WindowCaptureOverlay *CaptureController::windowOverlay() const
{
    return m_windowOverlay.data();
}
void CaptureController::schedule(CaptureRequest request, int delayMs)
{
    if (delayMs <= 0)
    {
        start(request);
        return;
    }
    ++m_pendingCaptures;
    QTimer::singleShot(delayMs, this, [this, request] {
        --m_pendingCaptures;
        start(request);
    });
}
void CaptureController::start(const CaptureRequest &request)
{
    const auto autoCopy = request.autoCopy;
    const auto reuseLast = request.reuseLast;
    const auto fixedSize = request.fixedSize;
    const auto requestedSelection = request.selection;
    const auto pinAfterCapture = request.pin;
    const auto outputSpec = request.output;
    const auto hold = request.hold;
    const auto noAutoSave = request.noAutoSave;
    const auto keepAspect = request.keepAspect;
    const auto superCaptureMode = request.superCapture;
    if (m_activeOverlay)
    {
        m_activeOverlay->raise();
        m_activeOverlay->activateWindow();
        return;
    }
    m_history.history();
#if defined(Q_OS_MACOS)
    if (!ensureMacScreenCaptureAccess(QStringLiteral("截图")))
        return;
#endif

    QScreen *screen = requestedSelection.isValid() ? QGuiApplication::screenAt(requestedSelection.center())
                      : reuseLast && m_lastSelection.isValid()
                          ? QGuiApplication::screenAt(m_lastSelection.center())
                          : QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
        return;

    QRect initialSelection;
    if (requestedSelection.isValid() || (reuseLast && m_lastSelection.isValid()))
    {
        const QRect source = requestedSelection.isValid() ? requestedSelection : m_lastSelection;
        const QRect local = source.translated(-screen->geometry().topLeft());
        initialSelection = local.intersected(screen->geometry().translated(-screen->geometry().topLeft()));
    }

    if (!m_preparedOverlay)
        m_preparedOverlay.reset(snipaste::ui::SnipOverlay::createPrepared(screen));
    // Drop outgoing callbacks from the previous session (output spec,
    // autosave, pinning, etc.), keeping all child-control bindings.
    QObject::disconnect(m_preparedOverlay.get(), nullptr, nullptr, nullptr);
    m_activeOverlay = m_preparedOverlay.get();
    m_activeOverlay->beginCapture(
        screen, autoCopy, m_config.value(QStringLiteral("capture/cursor"), false).toBool(), initialSelection);
    QObject::connect(m_activeOverlay, &snipaste::ui::SnipOverlay::sessionClosed, this,
                     [this] { m_activeOverlay = nullptr; });
    m_activeOverlay->setFixedSelectionSize(fixedSize);
    m_activeOverlay->setHoldAfterCapture(hold);
    m_activeOverlay->setKeepAspectRatio(keepAspect);
    configureOverlay(superCaptureMode);
    m_activeOverlay->setLastSelection(m_lastSelection.translated(-screen->geometry().topLeft()));
    QList<QRect> localRegions;
    for (const QRect &region : m_recentSelections)
        localRegions.append(region.translated(-screen->geometry().topLeft()));
    m_activeOverlay->setSelectionHistory(localRegions);
    // Keep lazy loaders and geometry bound to the same immutable order.
    // Capturing without closing can prepend to the live history meanwhile.
    const auto captureHistory =
        std::make_shared<const snipaste::core::ScreenshotHistory>(m_history.history());
    m_activeOverlay->setCaptureHistory(
        captureHistory->images(), captureHistory->geometries(), captureHistory->backgrounds(),
        [captureHistory](int index) { return captureHistory->backgroundAt(index); },
        [captureHistory](int index) { return captureHistory->imageAt(index); });
    QObject::connect(m_activeOverlay, &snipaste::ui::SnipOverlay::selectionCaptured,
                     [this](const QRect &globalRect) {
                         m_lastSelection = globalRect;
                         m_recentSelections.removeAll(globalRect);
                         m_recentSelections.prepend(globalRect);
                         while (m_recentSelections.size() > m_maxHistoryRegions)
                             m_recentSelections.removeLast();
                         QVariantList regions;
                         for (const QRect &region : m_recentSelections)
                             regions.append(region);
                         m_config.setValue(QStringLiteral("capture/recentRegions"), regions);
                         m_config.setValue(QStringLiteral("capture/lastRegion"), globalRect);
                         m_config.save();
                     });
    QObject::connect(m_activeOverlay, &snipaste::ui::SnipOverlay::screenshotCapturedWithBackground,
                     [this, overlay = m_activeOverlay](const QImage &image, const QImage &background) {
                         m_pastes.rememberCapture(image, m_lastSelection);
                         m_history.record(image, background,
                                          background.isNull() ? snipaste::core::CaptureGeometry{}
                                                              : overlay->captureGeometry());
                     });
    if (!noAutoSave)
    {
        QObject::connect(m_activeOverlay, &snipaste::ui::SnipOverlay::screenshotCaptured, this,
                         [this](const QImage &image) { m_output.autoSave(image); });
    }
    QObject::connect(m_activeOverlay, &snipaste::ui::SnipOverlay::unsavedScreenshotCapturedWithBackground,
                     [this, overlay = m_activeOverlay](const QImage &image, const QImage &background) {
                         if (m_config.value(QStringLiteral("capture/includeUnsaved"), false).toBool())
                             m_history.record(image, background, overlay->captureGeometry());
                     });
    if (!outputSpec.isEmpty())
    {
        QObject::connect(m_activeOverlay, &snipaste::ui::SnipOverlay::screenshotCaptured,
                         [this, outputSpec](const QImage &image) { m_output.execute(image, outputSpec); });
    }
    if (pinAfterCapture)
    {
        QObject::connect(m_activeOverlay, &snipaste::ui::SnipOverlay::screenshotCaptured, this,
                         [this](const QImage &image) {
                             m_output.showPaste(image, m_activeOverlay->pasteSourceImage(), m_activeOverlay->pasteAnnotations());
                         });
    }
    QObject::connect(m_activeOverlay, &snipaste::ui::SnipOverlay::pasteRequested, this,
                     [this](const QImage &image) {
                         m_output.showPaste(image, m_activeOverlay->pasteSourceImage(), m_activeOverlay->pasteAnnotations());
                     });
    QObject::connect(
        m_activeOverlay, &snipaste::ui::SnipOverlay::annotateRequested, [this](const QImage &image) {
            const QRect captureRegion = m_lastSelection;
            snipaste::ui::AnnotationDialog dialog(image);
            dialog.setAnnotationOptions(
                m_config.value(QStringLiteral("interface/toolbar/rememberColors"), true).toBool(),
                m_config.value(QStringLiteral("interface/toolbar/freeSelect"), true).toBool(),
                m_config.value(QStringLiteral("interface/toolbar/disableEraserEditing"), true).toBool());
            if (dialog.exec() != QDialog::Accepted)
                return;
            const QImage annotated = dialog.resultImage();
            m_history.record(annotated);
            m_output.autoSave(annotated);
            m_pastes.rememberCapture(annotated, captureRegion);
            snipaste::ui::copyImageToClipboard(annotated, false,
                                               snipaste::ui::ClipboardImageSource::Screenshot);
            m_pastes.createPaste(annotated);
        });
    QObject::connect(m_activeOverlay, &snipaste::ui::SnipOverlay::ocrCompleted, this, [this] {
        if (m_config.value(QStringLiteral("output/ocrSound"), true).toBool())
            playConfiguredSound(m_config.value(QStringLiteral("capture/soundFile"),
                                                QStringLiteral("sound/snip.wav")).toString());
    });
    QObject::connect(m_activeOverlay, &snipaste::ui::SnipOverlay::barcodeRequested, [](const QImage &image) {
        const snipaste::core::BarcodeResult result = snipaste::core::BarcodeService::recognize(image);
        if (result.success)
        {
            QApplication::clipboard()->setText(result.text);
            QMessageBox::information(nullptr, QStringLiteral("二维码/条码结果"), result.text);
        }
        else
        {
            QMessageBox::warning(nullptr, QStringLiteral("二维码/条码识别失败"), result.error);
        }
    });
    QObject::connect(m_activeOverlay, &snipaste::ui::SnipOverlay::printRequested, [](const QImage &image) {
        if (!printImage(image))
            qInfo() << "printer output cancelled or unavailable";
    });
    m_activeOverlay->show();
    if (autoCopy && !initialSelection.isEmpty())
    {
        auto *overlay = m_activeOverlay.data();
        QTimer::singleShot(0, overlay, [overlay] { overlay->captureSelectionImmediately(); });
    }
}
void CaptureController::configureOverlay(bool superCaptureMode)
{
    m_activeOverlay->setOcrOptions(
        m_config.value(QStringLiteral("output/ocrEngine"), QStringLiteral("系统自带")).toString(),
        m_config.value(QStringLiteral("output/ocrLanguage"), QStringLiteral("系统默认")).toString());
    const bool captureDisabled = m_config.value(QStringLiteral("capture/disable"), false).toBool();
    m_activeOverlay->setDetectionOptions(
        !captureDisabled && m_config.value(QStringLiteral("capture/autoDetectWindow"), true).toBool(),
        !captureDisabled && m_config.value(QStringLiteral("capture/autoDetectElements"), true).toBool(),
        !captureDisabled && m_config.value(QStringLiteral("capture/shortcutHints"), true).toBool());
    m_activeOverlay->setCopyAsFileMode(m_config.value(QStringLiteral("output/copyAsFileMode"), 1).toInt());
    m_activeOverlay->setOutputFileNamePattern(m_config
                                                  .value(QStringLiteral("output/fileNamePattern"),
                                                         QStringLiteral("Snipaste_$yyyy-MM-dd_HH-mm-ss$.png"))
                                                  .toString());
    m_activeOverlay->setOutputImageQuality(m_config.value(QStringLiteral("output/imageQuality"), -1).toInt());
    m_activeOverlay->setRememberExtension(
        m_config.value(QStringLiteral("output/rememberExtension"), true).toBool());
    m_activeOverlay->setRememberedExtension(
        m_config.value(QStringLiteral("output/lastExtension")).toString());
    m_activeOverlay->setQuickSaveNotification(
        m_config.value(QStringLiteral("output/quickSaveNotification"), true).toBool());
    m_activeOverlay->setHideOnFileDialog(
        m_config.value(QStringLiteral("capture/hideOverlayOnFileDialog"), true).toBool());
    m_activeOverlay->setCaptureDisplayOptions(
        m_config.value(QStringLiteral("capture/showAnchor"), true).toBool(),
        QColor::fromString(
            m_config.value(QStringLiteral("capture/maskColor"), QStringLiteral("#78000000")).toString()),
        QColor::fromString(
            m_config.value(QStringLiteral("capture/anchorColor"), QStringLiteral("#ffffffff")).toString()),
        m_config.value(QStringLiteral("capture/borderWidth"), 3).toInt(),
        m_config.value(QStringLiteral("capture/magnifierMask"), true).toBool(),
        m_config.value(QStringLiteral("capture/magnifierBorder"), true).toBool(),
        m_config.value(QStringLiteral("capture/magnifierAnchor"), false).toBool(),
        m_config.value(QStringLiteral("capture/crosshair"), false).toBool(),
        m_config.value(QStringLiteral("capture/disableGuides"), false).toBool(),
        m_config.value(QStringLiteral("capture/hideOneByOne"), true).toBool());
    m_activeOverlay->setMagnifierOptions(
        m_config.value(QStringLiteral("interface/magnifier/automatic"), true).toBool(),
        m_config.value(QStringLiteral("interface/magnifier/super"), false).toBool(),
        m_config
            .value(QStringLiteral("interface/magnifier/formats"),
                   QStringList{QStringLiteral("RGB"), QStringLiteral("HEX")})
            .toStringList(),
        m_config.value(QStringLiteral("interface/magnifier/hideHash"), false).toBool(),
        m_config
            .value(QStringLiteral("interface/magnifier/uppercase"),
                   m_config.value(QStringLiteral("misc/uppercase_hex_color"), true))
            .toBool(),
        m_config.value(QStringLiteral("interface/magnifier/grid"), true).toBool(),
        m_config.value(QStringLiteral("interface/magnifier/coordinates"), true).toBool(),
        m_config.value(QStringLiteral("interface/magnifier/hints"), true).toBool(),
        m_config.value(QStringLiteral("interface/magnifier/zoom"), 9).toInt(), superCaptureMode);
    m_activeOverlay->setDefaultAnnotation(
        m_config.value(QStringLiteral("capture/defaultAnnotation"), QStringLiteral("无")).toString());
    m_activeOverlay->setCaptureSound(
        m_config.value(QStringLiteral("capture/soundEnabled"), true).toBool(),
        m_config.value(QStringLiteral("capture/soundFile"), QStringLiteral("sound/snip.wav")).toString(),
        m_config.value(QStringLiteral("capture/soundQuickSave"), false).toBool());
    m_activeOverlay->setDetectionProcessFilters(
        configuredProcessList(m_config, QStringLiteral("snip/exclude_from_detection"),
                              QStringLiteral("exclude_from_detection")),
        configuredProcessList(m_config, QStringLiteral("snip/crashes"), QStringLiteral("crashes")));
    m_activeOverlay->setExitOnFocusLoss(m_config.value(QStringLiteral("capture/exitOnFocus"), true).toBool());
    m_activeOverlay->setWheelEnabled(!m_config.value(QStringLiteral("capture/disableWheel"), false).toBool());
    m_activeOverlay->setConfirmEscape(m_config.value(QStringLiteral("capture/confirmEsc"), false).toBool());
    m_activeOverlay->setHistoryCycling(
        m_config.value(QStringLiteral("capture/historyCycle"), false).toBool());
    m_activeOverlay->setIncludeUnsavedHistory(
        m_config.value(QStringLiteral("capture/includeUnsaved"), false).toBool());
    m_activeOverlay->setAnchorThreshold(m_config
                                            .value(QStringLiteral("snip/anchors_threshold"),
                                                   m_config.value(QStringLiteral("anchors_threshold"), 80))
                                            .toInt());
    m_activeOverlay->setUppercaseHexColor(
        m_config
            .value(QStringLiteral("misc/uppercase_hex_color"),
                   m_config.value(QStringLiteral("uppercase_hex_color"), true))
            .toBool());
    m_activeOverlay->setHideOutputOptions(
        m_config
            .value(QStringLiteral("interface/hide_output_options"),
                   m_config.value(QStringLiteral("hide_output_options"), false))
            .toBool());
    m_activeOverlay->setShowShareButton(
        m_config.value(QStringLiteral("interface/toolbar/showShare"), true).toBool());
    m_activeOverlay->setQuickSaveDirectory(
        m_config
            .value(QStringLiteral("output/saveDirectory"),
                   QStandardPaths::writableLocation(QStandardPaths::PicturesLocation))
            .toString());
    QObject::connect(m_activeOverlay, &snipaste::ui::SnipOverlay::fileSaved, [this](const QString &path) {
        const QString extension = QFileInfo(path).suffix().trimmed().toLower();
        if (!extension.isEmpty())
        {
            m_config.setValue(QStringLiteral("output/lastExtension"), extension);
            m_config.save();
        }
    });
    m_activeOverlay->setAutoSelectAnnotations(
        m_config
            .value(QStringLiteral("annotate/auto_select_on_creation"),
                   m_config.value(QStringLiteral("auto_select_on_creation"), true))
            .toBool());
    m_activeOverlay->setRememberToolColors(
        m_config.value(QStringLiteral("interface/toolbar/rememberColors"), true).toBool());
    m_activeOverlay->setFreeSelect(
        m_config.value(QStringLiteral("interface/toolbar/freeSelect"), true).toBool());
    m_activeOverlay->setDisableEraserEditing(
        m_config.value(QStringLiteral("interface/toolbar/disableEraserEditing"), true).toBool());
    m_activeOverlay->setQuickResponse(m_config.value(QStringLiteral("general/quickResponse"), true).toBool());
    m_activeOverlay->setPenTipCount(m_config.value(QStringLiteral("interface/toolbar/penCount"), 3).toInt());
    const QStringList annotationPalette =
        m_config.value(QStringLiteral("interface/toolbar/palette"), QStringList()).toStringList();
    if (!annotationPalette.isEmpty())
        m_activeOverlay->setAnnotationPalette(annotationPalette);
    m_activeOverlay->setMouseActions(
        m_config.value(QStringLiteral("capture/doubleClickAction"), QStringLiteral("复制到剪贴板"))
            .toString(),
        m_config.value(QStringLiteral("capture/doubleClickExit"), true).toBool(),
        m_config.value(QStringLiteral("capture/middleClickAction"), QStringLiteral("贴到屏幕")).toString(),
        m_config.value(QStringLiteral("capture/middleClickExit"), true).toBool(),
        m_config.value(QStringLiteral("capture/enterAction"), QStringLiteral("复制到剪贴板")).toString(),
        m_config.value(QStringLiteral("capture/enterExit"), true).toBool());
    m_activeOverlay->setExchangeNavigationKeys(
        m_config.value(QStringLiteral("hotkeys/exchange_wasd_and_arrow_keys"), false).toBool());
}
void CaptureController::startWindow(bool elementMode, QString outputSpec, bool noAutoSave)
{
    if (m_windowOverlay)
    {
        m_windowOverlay->raise();
        m_windowOverlay->activateWindow();
        return;
    }
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
        return;
#if defined(Q_OS_MACOS)
    if (!ensureMacScreenCaptureAccess(QStringLiteral("窗口截图")))
        return;
#endif
    m_windowOverlay = new snipaste::ui::WindowCaptureOverlay(screen, elementMode);
    QObject::connect(m_windowOverlay, &QObject::destroyed, this,
                     [] { QTimer::singleShot(250, qApp, [] { trimIdleProcessMemory(); }); });
    QObject::connect(m_windowOverlay, &snipaste::ui::WindowCaptureOverlay::screenshotCaptured,
                     [this, outputSpec, noAutoSave](const QImage &image) {
                         m_history.record(image);
                         if (!noAutoSave)
                             m_output.autoSave(image);
                         if (outputSpec.isEmpty())
                         {
                             snipaste::ui::copyImageToClipboard(
                                 image, false, snipaste::ui::ClipboardImageSource::Screenshot);
                         }
                         else
                         {
                             m_output.execute(image, outputSpec);
                         }
                     });
    m_windowOverlay->show();
}

} // namespace snipaste::app
