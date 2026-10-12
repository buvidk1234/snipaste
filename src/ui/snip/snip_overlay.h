#pragma once

#include <QImage>
#include <QList>
#include <QPointer>
#include <QRect>
#include <QSize>
#include <QStringList>
#include <QWidget>
#include <functional>

class QScreen;
class QFrame;
class QPainter;
class QMouseEvent;
class QToolButton;
class QStackedLayout;

namespace snipate::ui {

class AnnotationCanvas;
class SnipBorderOverlay;

class SnipOverlay final : public QWidget
{
    Q_OBJECT
    friend class SnipBorderOverlay;

public:
    explicit SnipOverlay(QScreen *screen, bool autoCopy = false, QWidget *parent = nullptr);
    SnipOverlay(QScreen *screen,
                bool autoCopy,
                bool captureCursor,
                QRect initialSelection,
                QWidget *parent = nullptr);

    void captureSelectionImmediately();
    void setFixedSelectionSize(const QSize &size);
    using TargetDetector = std::function<QRect(QPoint, bool)>;
    void setTargetDetector(TargetDetector detector);
    void setDetectionOptions(bool detectWindows, bool detectElements, bool showHints);
    void setDetectionProcessFilters(const QStringList &excludedProcesses,
                                    const QStringList &unstableProcesses)
    { m_excludedDetectionProcesses = excludedProcesses; m_unstableDetectionProcesses = unstableProcesses; }
    void setExchangeNavigationKeys(bool enabled) { m_exchangeNavigationKeys = enabled; }
    void setLastSelection(QRect selection);
    void setSelectionHistory(const QList<QRect> &regions);
    void setCaptureHistory(const QList<QImage> &images,
                           const QList<QRect> &regions = {},
                           const QList<QImage> &backgrounds = {});
    void setBackgroundProvider(std::function<QImage()> provider);
    void setHoldAfterCapture(bool enabled) { m_holdAfterCapture = enabled; }
    void setKeepAspectRatio(bool enabled) { m_keepAspectRatio = enabled; }
    void setExitOnFocusLoss(bool enabled) { m_exitOnFocusLoss = enabled; }
    void setWheelEnabled(bool enabled) { m_wheelEnabled = enabled; }
    void setConfirmEscape(bool enabled) { m_confirmEscape = enabled; }
    void setHistoryCycling(bool enabled) { m_historyCycle = enabled; }
    void setIncludeUnsavedHistory(bool enabled) { m_includeUnsavedHistory = enabled; }
    void setAutoSelectAnnotations(bool enabled) { m_autoSelectAnnotations = enabled; }
    void setAnchorThreshold(int pixels) { m_anchorThreshold = qMax(0, pixels); }
    void setUppercaseHexColor(bool enabled) { m_uppercaseHexColor = enabled; }
    void setHideOutputOptions(bool enabled);
    void setQuickSaveDirectory(const QString &directory) { m_quickSaveDirectory = directory; }
    void setPenTipCount(int count) { m_penTipCount = qBound(1, count, 5); }
    void setAnnotationPalette(const QStringList &palette) { m_annotationPalette = palette; }
    void setMouseActions(const QString &doubleClickAction, bool doubleClickExit,
                         const QString &middleClickAction, bool middleClickExit,
                         const QString &enterAction, bool enterExit);

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
    void screenshotCaptured(const QImage &image);
    void screenshotCapturedWithBackground(const QImage &image, const QImage &background);
    void unsavedScreenshotCaptured(const QImage &image);
    void unsavedScreenshotCapturedWithBackground(const QImage &image, const QImage &background);
    void selectionCaptured(const QRect &globalRect);
    void pasteRequested(const QImage &image);
    void annotateRequested(const QImage &image);
    void ocrRequested(const QImage &image);
    void barcodeRequested(const QImage &image);
    void printRequested(const QImage &image);

private:
    QRect imageSelection() const;
    QImage selectionImage() const;
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
    void copySelection(bool closeAfter = true);
    void saveSelection(bool closeAfter = true);
    void quickSaveSelection(bool closeAfter = true);
    void printSelection(bool closeAfter = true);
    void pasteSelection(bool closeAfter = true);
    void executeConfiguredAction(const QString &action, bool closeAfter);
    void annotateSelection();
    void ocrSelection();
    void barcodeSelection();
    QRect globalSelection() const;
    void drawCapturedCursor(const QScreen *screen);
    void drawMagnifier(QPainter &painter) const;
    void drawMagnifierGuides(QPainter &painter) const;
    void drawShortcutHints(QPainter &painter) const;
    void updateHoverTarget();
    void updateDragSelection(QPoint position, Qt::KeyboardModifiers modifiers);
    QPoint sampledPixel() const;
    QString sampledColorText() const;
    Qt::Edges resizeEdgesAt(QPoint position) const;
    bool beginSelectionTransform(QPoint position);
    void updateSelectionTransform(QPoint position);
    void updateSelectionCursor(QPoint position);
    void refreshAnnotationCursor();
    void updateDynamicWidthButtons(qreal width);
    void updateAllColorWatchers(const QColor &color);
    void showCornerRadiusPopup(QWidget *sourceBtn);
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
    QList<QRect> m_captureHistoryRegions;
    int m_historyIndex = -1;
    QRect m_liveSelection;
    std::function<QImage()> m_backgroundProvider;
    QList<QRect> m_targetHierarchy;
    int m_targetLevel = 0;
    QPoint m_targetPoint = QPoint(-1, -1);
    QRect m_selection;
    QPoint m_dragOrigin;
    QRect m_hoverSelection;
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
    QList<QToolButton *> m_colorWatchers;
    QList<QPair<QToolButton *, QString>> m_paletteButtons;
    QList<QToolButton *> m_dynamicWidthButtons;
    QPointer<QFrame> m_cornerRadiusPopup;
    QColor m_currentAnnotationColor = QColor(QStringLiteral("#ed1c24"));
    QToolButton *m_lineStyleButton = nullptr;
    QToolButton *m_arrowStyleButton = nullptr;
    QToolButton *m_curveStyleButton = nullptr;
    QFrame *m_sizeToolbar = nullptr;
    QToolButton *m_sizeButton = nullptr;
    QToolButton *m_roundedButton = nullptr;
    QWidget *m_radiusStepperWidget = nullptr;
    QToolButton *m_outputOptionsButton = nullptr;
    AnnotationCanvas *m_canvas = nullptr;
    QWidget *m_selectionBorder = nullptr;
    QPoint m_cursorPosition;
    bool m_showMagnifier = true;
    bool m_showMagnifierGuides = false;
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
    int m_anchorThreshold = 80;
    bool m_uppercaseHexColor = true;
    bool m_hideOutputOptions = false;
    QString m_quickSaveDirectory;
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
};

} // namespace snipate::ui
