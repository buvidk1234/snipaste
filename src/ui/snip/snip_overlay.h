#pragma once

#include "core/history/screenshot_history.h"
#include "core/annotation/annotation_document.h"
#include "core/scroll_stitcher.h"
#include "core/scroll_capture_stability.h"

#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QList>
#include <QPointer>
#include <QRect>
#include <QRegion>
#include <QSize>
#include <QStringList>
#include <QWidget>
#include <functional>

class QScreen;
class QFrame;
class QPainter;
class QMouseEvent;
class QTimer;
class QToolButton;
class QStackedLayout;
class QVariantAnimation;

namespace snipaste::ui
{

class AnnotationCanvas;
class AnnotationOptions;
class SnipBorderOverlay;
class ScrollCapturePanel;
class ScrollCapturePreview;
class OcrOverlay;
class AiDialog;
class CaptureTargetWorker;

class SnipOverlay final : public QWidget
{
    Q_OBJECT
    friend class SnipBorderOverlay;

  public:
    explicit SnipOverlay(QScreen *screen, bool autoCopy = false, QWidget *parent = nullptr);
    SnipOverlay(QScreen *screen, bool autoCopy, bool captureCursor, QRect initialSelection,
                QWidget *parent = nullptr);
    ~SnipOverlay() override;

    // Construct the hidden controls at application startup, without capturing
    // the desktop. The caller owns this reusable overlay.
    static SnipOverlay *createPrepared(QScreen *screen);
    void beginCapture(QScreen *screen, bool autoCopy = false, bool captureCursor = false,
                      QRect initialSelection = {});

    void captureSelectionImmediately();
    void startScrollCapture();
    void showAiDialog(AiDialog *dialog);
    // The global paste command uses the current selection while this
    // capture session is open, before it has entered clipboard/history.
    bool pasteSelection(bool closeAfter = true);
    QImage pasteSourceImage() const;
    core::AnnotationDocument pasteAnnotations() const;
    void setFixedSelectionSize(const QSize &size);
    using TargetDetector = std::function<QRect(QPoint, bool)>;
    // Asynchronous element callbacks must capture values, not GUI objects.
    void setTargetDetector(TargetDetector detector, bool asynchronous = false);
    void setDetectionOptions(bool detectWindows, bool detectElements, bool showHints);
    void setDetectionProcessFilters(const QStringList &excludedProcesses,
                                    const QStringList &unstableProcesses)
    {
        m_excludedDetectionProcesses = excludedProcesses;
        m_unstableDetectionProcesses = unstableProcesses;
        invalidateTargetDetection();
    }
    void setExchangeNavigationKeys(bool enabled) { m_exchangeNavigationKeys = enabled; }
    void setLastSelection(QRect selection);
    void setSelectionHistory(const QList<QRect> &regions);
    core::CaptureGeometry captureGeometry() const { return {m_selection, size()}; }
    void setCaptureHistory(const QList<QImage> &images, const QList<core::CaptureGeometry> &geometries = {},
                           const QList<QImage> &backgrounds = {},
                           std::function<QImage(int)> backgroundLoader = {},
                           std::function<QImage(int)> imageLoader = {});
    void setBackgroundProvider(std::function<QImage()> provider);
    void setHoldAfterCapture(bool enabled) { m_holdAfterCapture = enabled; }
    void setKeepAspectRatio(bool enabled) { m_keepAspectRatio = enabled; }
    // Capture display options are kept on the overlay so each new capture
    // reflects the preferences without relying on process globals.
    void setCaptureDisplayOptions(bool showAnchor, const QColor &maskColor, const QColor &anchorColor,
                                  int borderWidth, bool magnifierMask, bool magnifierBorder,
                                  bool magnifierAnchor, bool crosshair, bool disableGuides,
                                  bool hideOneByOne);
    void setMagnifierOptions(bool automatic, bool superCapture, const QStringList &colorFormats,
                             bool hideHexPrefix, bool uppercaseHex, bool grid, bool coordinates, bool hints,
                             int zoom, bool superMode);
    void setDefaultAnnotation(const QString &annotation) { m_defaultAnnotation = annotation.trimmed(); }
    void setCaptureSound(bool enabled, const QString &file, bool onQuickSave)
    {
        m_captureSoundEnabled = enabled;
        m_captureSoundFile = file.trimmed();
        m_captureSoundQuickSave = onQuickSave;
    }
    void setExitOnFocusLoss(bool enabled) { m_exitOnFocusLoss = enabled; }
    void setWheelEnabled(bool enabled) { m_wheelEnabled = enabled; }
    void setConfirmEscape(bool enabled) { m_confirmEscape = enabled; }
    void setHistoryCycling(bool enabled) { m_historyCycle = enabled; }
    void setIncludeUnsavedHistory(bool enabled) { m_includeUnsavedHistory = enabled; }
    void setAutoSelectAnnotations(bool enabled) { m_autoSelectAnnotations = enabled; }
    void setRememberToolColors(bool enabled);
    void setFreeSelect(bool enabled);
    void setDisableEraserEditing(bool enabled);
    void setQuickResponse(bool enabled);
    void setOcrOptions(const QString &engine, const QString &language);
    void setAnchorThreshold(int pixels) { m_anchorThreshold = qMax(0, pixels); }
    void setUppercaseHexColor(bool enabled) { m_uppercaseHexColor = enabled; }
    void setHideOutputOptions(bool enabled);
    void setShowShareButton(bool enabled);
    // 0 = always copy images as files, 1 = only the copy-as-file shortcut,
    // 2 = copy image data.  These values match output/copyAsFileMode.
    void setCopyAsFileMode(int mode) { m_copyAsFileMode = mode >= 0 && mode <= 2 ? mode : 2; }
    void setQuickSaveDirectory(const QString &directory) { m_quickSaveDirectory = directory; }
    // Output settings are supplied by the application when a capture session
    // is created.  Keeping them on the overlay makes both the toolbar's
    // manual save and quick-save paths follow the same preferences.
    void setOutputFileNamePattern(const QString &pattern) { m_outputFileNamePattern = pattern; }
    void setOutputImageQuality(int quality) { m_outputImageQuality = qBound(-1, quality, 100); }
    void setRememberExtension(bool enabled) { m_rememberExtension = enabled; }
    void setRememberedExtension(const QString &extension) { m_lastSavedExtension = extension.trimmed(); }
    void setQuickSaveNotification(bool enabled) { m_quickSaveNotification = enabled; }
    void setHideOnFileDialog(bool enabled) { m_hideOnFileDialog = enabled; }
    void setPenTipCount(int count) { m_penTipCount = qBound(1, count, 5); }
    void setAnnotationPalette(const QStringList &palette) { m_annotationPalette = palette; }
    void setMouseActions(const QString &doubleClickAction, bool doubleClickExit,
                         const QString &middleClickAction, bool middleClickExit, const QString &enterAction,
                         bool enterExit);

  protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void changeEvent(QEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    bool focusNextPrevChild(bool) override { return false; }
    bool eventFilter(QObject *watched, QEvent *event) override;

  signals:
    void sessionClosed();
    void screenshotCaptured(const QImage &image);
    void screenshotCapturedWithBackground(const QImage &image, const QImage &background);
    void unsavedScreenshotCaptured(const QImage &image);
    void unsavedScreenshotCapturedWithBackground(const QImage &image, const QImage &background);
    void selectionCaptured(const QRect &globalRect);
    void pasteRequested(const QImage &image);
    void annotateRequested(const QImage &image);
    void ocrRequested(const QImage &image);
    void aiRequested(const QImage &image);
    void ocrCompleted();
    void barcodeRequested(const QImage &image);
    void printRequested(const QImage &image);
    // Emitted after a file has been written successfully.  The application
    // can use it to persist the selected extension for the next dialog.
    void fileSaved(const QString &path);
    void quickSaveCompleted(const QString &path);

  private:
    SnipOverlay(QScreen *screen, QWidget *parent, bool reusable);
    QRect imageSelection() const;
    QImage selectionImage(bool annotations = true) const;
    void createToolbar();
    void createSizeToolbar();
    void updateSizeToolbar();
    void showSizeEditor();
    void selectCurrentScreen();
    void selectVirtualDesktop();
    void resizeFromCenter(int delta);
    bool handleProShortcut(QKeyEvent *event);
    void refreshBackground();
    void toggleCapturedCursor();
    void navigateHistory(int direction);
    void cycleTarget(int direction);
    void showOutputMenu(const QPoint &globalPosition, bool shareOnly = false);
    void updateToolbarGeometry();
    void confirmSelection();
    void resetSelection();
    void copySelection(bool closeAfter = true, bool copyAsFileShortcut = false);
    void saveSelection(bool closeAfter = true);
    void quickSaveSelection(bool closeAfter = true);
    void printSelection(bool closeAfter = true);
    void executeConfiguredAction(const QString &action, bool closeAfter);
    void annotateSelection();
    void ocrSelection();
    void leaveOcrMode();
    void clearOcrOverlay();
    void barcodeSelection();
    void captureScrollFrame();
    void pollScrollCaptureFrame(quint64 generation);
    void completeScrollCaptureFrame(const QImage &frame, bool settled);
    // A manual wheel/scrollbar gesture in the target application is observed
    // outside Qt's event loop. Sample until the viewport has settled; new
    // input also invalidates an in-flight stability observation.
    Q_INVOKABLE void scheduleManualScrollCapture();
    void stopScrollInputMonitoring();
    void requestAutoScroll(bool enabled);
    void autoScrollStep();
    void finishScrollCapture();
    void cancelScrollCapture();
    void editScrollCapture();
    void saveScrollCapture();
    void cleanupScrollCapture();
    void updateScrollCaptureLayout();
    void applyScrollCaptureWindowRegion();
    void restoreWindowRegion();
    QImage captureScrollViewport() const;
    bool sendScrollStep() const;
    void restoreScrollCaptureCursor();
    QRect globalSelection() const;
    void drawCapturedCursor(const QScreen *screen);
    void drawMagnifier(QPainter &painter) const;
    QRect magnifierRect(QPoint position, bool includeText = false) const;
    QRect shortcutHintsRect() const;
    void updateHoverCursor(QPoint previousPosition);
    void drawMagnifierGuides(QPainter &painter) const;
    void drawShortcutHints(QPainter &painter) const;
    void playCaptureSound() const;
    // Release the full-screen capture as soon as the capture session ends.
    // WA_DeleteOnClose is asynchronous, and the image can otherwise remain
    // resident while a popup or a nested event loop is finishing.
    void releaseCaptureResources();
    // Window hit testing is cheap, while element hit testing may enter the
    // Windows accessibility/UI Automation stack.  Callers that are trying to
    // get the first frame on screen can explicitly skip the latter.
    void updateHoverTarget(bool allowElements = true);
    void invalidateTargetDetection();
    void queryHoverElements();
    void setHoverSelection(QRect selection, bool animate = true, bool provisional = false);
    void startHoverTransition();
    void updateHoverDisplay(QRectF selection);
    QRect selectionLabelRect(QRect selection, const QString &text) const;
    QString selectionSizeText() const;
    void updateDragSelection(QPoint position, Qt::KeyboardModifiers modifiers);
    QPoint sampledPixel() const;
    QString sampledColorText() const;
    Qt::Edges resizeEdgesAt(QPoint position) const;
    bool beginSelectionTransform(QPoint position);
    void updateSelectionTransform(QPoint position);
    void updateSelectionCursor(QPoint position);
    void refreshAnnotationCursor();
    void forwardCanvasMouseEvent(QMouseEvent *event);
    void changeSelection(QRect selection, bool moveAnnotations);
    void paintSelectionFrame(QPainter &painter);
    QImage captureVirtualDesktop() const;

    QImage m_background;
    QImage m_cleanBackground;
    // History images keep their original pixel dimensions while the overlay
    // may be running at a fractional device-pixel ratio.
    QImage m_selectionImageOverride;
    QImage m_liveBackground;
    QList<QImage> m_captureHistory;
    QList<QImage> m_captureHistoryBackgrounds;
    QList<core::CaptureGeometry> m_captureHistoryGeometries;
    std::function<QImage(int)> m_captureHistoryBackgroundLoader;
    std::function<QImage(int)> m_captureHistoryImageLoader;
    int m_historyIndex = -1;
    QRect m_liveSelection;
    std::function<QImage()> m_backgroundProvider;
    QList<QRect> m_targetHierarchy;
    QList<QRect> m_windowHierarchy;
    quintptr m_targetWindow = 0;
    quint64 m_targetRequest = 0;
    QTimer *m_targetTimer = nullptr;
    QPointer<CaptureTargetWorker> m_targetWorker;
    int m_targetLevel = 0;
    QPoint m_targetPoint = QPoint(-1, -1);
    QRect m_selection;
    QPoint m_dragOrigin;
    QRect m_hoverSelection;
    QRectF m_hoverDisplaySelection;
    QRegion m_hoverDecorationRegion;
    QVariantAnimation *m_hoverAnimation = nullptr;
    QTimer *m_hoverFrameTimer = nullptr;
    QElapsedTimer m_hoverFrameClock;
    int m_hoverFrameInterval = 16;
    QTimer *m_hoverFallbackTimer = nullptr;
    QRect m_pressedTarget;
    QRect m_lastSelection;
    QList<QRect> m_regionHistory;
    int m_regionHistoryIndex = -1;
    QRect m_transformOriginSelection;
    QPoint m_transformOrigin;
    Qt::Edges m_resizeEdges;
    bool m_transformingSelection = false;
    bool m_movingSelection = false;
    TargetDetector m_targetDetector;
    bool m_nativeTargetDetector = true;
    bool m_asynchronousTargetDetector = false;
    bool m_detectWindows = true;
    bool m_detectElements = true;
    bool m_elementMode = true;
    bool m_showShortcutHints = true;
    bool m_hexColor = false;
    bool m_dragMoved = false;
    bool m_dragging = false;
    bool m_externalDrawing = false;
    bool m_selectionConfirmed = false;
    bool m_toolbarHidden = false;
    bool m_refreshing = false;
    bool m_autoCopy = false;
    int m_copyAsFileMode = 2;
    bool m_captureCursor = false;
    QRect m_initialSelection;
    QSize m_fixedSelectionSize;
    QRect m_screenGeometry;
    QRect m_singleScreenGeometry;
    QImage m_singleScreenBackground;
    std::function<QImage()> m_singleScreenBackgroundProvider;
    bool m_virtualDesktopMode = false;
    QFrame *m_toolbar = nullptr;
    QFrame *m_toolOptions = nullptr;
    QStackedLayout *m_toolOptionsStack = nullptr;
    AnnotationOptions *m_annotationOptions = nullptr;
    QFrame *m_sizeToolbar = nullptr;
    QToolButton *m_sizeButton = nullptr;
    QToolButton *m_roundedButton = nullptr;
    QWidget *m_radiusStepperWidget = nullptr;
    QToolButton *m_outputOptionsButton = nullptr;
    QToolButton *m_shareButton = nullptr;
    bool m_showShareButton = true;
    AnnotationCanvas *m_canvas = nullptr;
    AnnotationCanvas *m_preparedCanvas = nullptr;
    OcrOverlay *m_ocrOverlay = nullptr;
    QWidget *m_aiLayer = nullptr;
    AiDialog *m_aiDialog = nullptr;
    QString m_ocrEngine;
    QString m_ocrLanguage;
    bool m_reusable = false;
    bool m_captureActive = false;
    quint64 m_sessionGeneration = 0;
    QWidget *m_selectionBorder = nullptr;
    QPoint m_cursorPosition;
    bool m_showMagnifier = true;
    bool m_showMagnifierGuides = false;
    bool m_magnifierGrid = true;
    bool m_magnifierCoordinates = true;
    bool m_magnifierHints = true;
    bool m_magnifierHideHexPrefix = false;
    bool m_magnifierUppercaseHex = true;
    int m_magnifierZoom = 9;
    QStringList m_magnifierColorFormats{QStringLiteral("RGB"), QStringLiteral("HEX")};
    bool m_showAnchor = true;
    QColor m_maskColor = QColor(0, 0, 0, 120);
    QColor m_anchorColor = Qt::white;
    int m_captureBorderWidth = 2;
    bool m_magnifierMask = true;
    bool m_magnifierBorder = true;
    bool m_magnifierAnchor = false;
    bool m_showCrosshair = false;
    bool m_disableGuides = false;
    bool m_hideOneByOne = true;
    QString m_defaultAnnotation;
    bool m_captureSoundEnabled = true;
    bool m_captureSoundQuickSave = false;
    QString m_captureSoundFile = QStringLiteral("sound/snip.wav");
    bool m_roundedCorners = false;
    int m_captureCornerRadius = 8;
    bool m_outputShadow = false;
    int m_outputShadowBlur = 32;
    QColor m_outputShadowColor = QColor(128, 128, 128);
    bool m_outputBorder = false;
    int m_outputBorderWidth = 1;
    QColor m_outputBorderColor = Qt::black;
    bool m_exchangeNavigationKeys = false;
    bool m_holdAfterCapture = false;
    bool m_keepAspectRatio = false;
    qreal m_aspectRatio = 0.0;
    bool m_exitOnFocusLoss = true;
    bool m_wheelEnabled = true;
    bool m_confirmEscape = false;
    bool m_historyCycle = false;
    bool m_includeUnsavedHistory = false;
    bool m_autoSelectAnnotations = true;
    bool m_rememberToolColors = true;
    bool m_freeSelect = true;
    bool m_disableEraserEditing = true;
    bool m_quickResponse = true;
    int m_anchorThreshold = 80;
    bool m_uppercaseHexColor = true;
    bool m_hideOutputOptions = false;
    QString m_quickSaveDirectory;
    QString m_outputFileNamePattern = QStringLiteral("Snipaste_$yyyy-MM-dd_HH-mm-ss$.png");
    QString m_lastSavedExtension;
    int m_outputImageQuality = -1;
    bool m_rememberExtension = true;
    bool m_quickSaveNotification = true;
    bool m_hideOnFileDialog = true;
    int m_penTipCount = 3;
    QStringList m_annotationPalette;
    QStringList m_excludedDetectionProcesses;
    QStringList m_unstableDetectionProcesses;
    bool m_outputEmitted = false;
    QString m_doubleClickAction = QStringLiteral("复制到剪贴板");
    QString m_middleClickAction = QStringLiteral("贴到屏幕");
    QString m_enterAction = QStringLiteral("复制到剪贴板");
    bool m_doubleClickExit = true;
    bool m_middleClickExit = true;
    bool m_enterExit = true;
    QPointer<ScrollCapturePanel> m_scrollPanel;
    QPointer<ScrollCapturePreview> m_scrollPreview;
    snipaste::core::VerticalScrollStitcher m_scrollStitcher;
    QPointer<QScreen> m_scrollScreen;
    QRect m_scrollGlobalSelection;
    quintptr m_scrollWindow = 0;
    QPoint m_scrollOriginalCursor;
    bool m_scrollCaptureActive = false;
    bool m_scrollAutoRunning = false;
    bool m_scrollCaptureBusy = false;
    int m_scrollCaptureCount = 0;
    int m_scrollUnchangedAttempts = 0;
    quint64 m_scrollCaptureGeneration = 0;
    QElapsedTimer m_scrollSettleElapsed;
    core::ScrollCaptureStability m_scrollStability;
    QTimer *m_scrollManualTimer = nullptr;
};

} // namespace snipaste::ui
