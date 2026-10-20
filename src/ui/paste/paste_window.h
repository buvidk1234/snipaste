#pragma once

#include <QColor>
#include <QImage>
#include <QSize>
#include <QStringList>
#include <QTimer>
#include <QTransform>
#include <QWidget>
#include <array>

class QContextMenuEvent;
class QDragEnterEvent;
class QDropEvent;
class QKeyEvent;
class QMouseEvent;
class QWheelEvent;
class QUrl;
class QFrame;
class QGraphicsView;
class QGraphicsProxyWidget;
class QMenu;
class QPainter;

namespace snipaste::core
{
struct PasteWindowState;
class AnnotationDocument;
}
namespace snipaste::ui
{

class PasteManager;
class AnnotationCanvas;
class AnnotationOptions;

class PasteWindow final : public QWidget
{
    Q_OBJECT
  public:
    enum class State
    {
        Active,
        Closed,
        Destroyed
    };

    explicit PasteWindow(QImage image, QWidget *parent = nullptr);
    ~PasteWindow() override;

    void showAtCursor();
    void showAtPosition(const QPoint &globalPosition);
    void showExisting();
    void setActivateOnShow(bool enabled) { m_activateOnShow = enabled; }
    void setFlashTransparency(bool enabled);
    void setKeepOnDrag(bool enabled) { m_keepOnDrag = enabled; }
    void flash();
    bool isFlashing() const { return m_flashActive; }
    State state() const;
    void restoreClosed();
    const QImage &sourceImage() const;
    const QImage &displayedImage() const;
    void setAnnotations(const core::AnnotationDocument &document, const QImage &source = {});
    void setToolbarVisible(bool visible);
    bool toolbarVisible() const { return m_showToolbar; }
    void setAnnotationsVisible(bool visible);
    bool annotationsVisible() const { return m_showAnnotations; }
    void setSourceText(const QString &text);
    const QString &sourceText() const;
    bool clickThrough() const;
    void setClickThrough(bool enabled);
    void setShadowEnabled(bool enabled);
    bool shadowEnabled() const;
    void setSelected(bool selected);
    bool selected() const { return m_selected; }
    void setResizeEnabled(bool enabled);
    bool resizeEnabled() const;
    void setExchangeNavigationKeys(bool enabled);
    void setMaxNetworkImageBytes(qint64 bytes);
    void setMaxSize(int pixels);
    void setThumbnailSize(const QSize &size);
    void setWheelCenter(bool enabled);
    void setResetTransform(bool enabled);
    void setDisableDpiScaling(bool enabled);
    void setHelpKeys(bool enabled) { m_helpKeys = enabled; }
    void setCloseConfirm(bool enabled) { m_closeConfirm = enabled; }
    void setDestroyConfirm(bool enabled) { m_destroyConfirm = enabled; }
    bool isSoloHidden() const;
    void setSoloHidden(bool hidden);
    int rotationQuarterTurns() const;
    bool flipHorizontal() const;
    bool flipVertical() const;
    bool grayscale() const;
    bool inverted() const;
    qreal scale() const;
    bool transparentBackground() const;
    QString backgroundMode() const;
    void setBackgroundMode(const QString &mode);
    void setSmoothScale(bool enabled);
    void setEscapeActions(const QString &escapeAction, const QString &shiftEscapeAction);
    void setMouseActions(const QStringList &actions);
    void setVirtualDesktopPinned(bool enabled);
    bool virtualDesktopPinned() const;
    void setPasteManager(PasteManager *manager);
    void setCopyAsFileShortcutEnabled(bool enabled) { m_copyAsFileShortcutEnabled = enabled; }
    bool alwaysOnTop() const;
    bool thumbnailMode() const;
    void setThumbnailMode(bool enabled);
    void scaleBy(qreal factor);
    void adjustOpacity(qreal delta);
    void restoreTransform(int rotationQuarterTurns, bool flipHorizontal, bool flipVertical, bool grayscale,
                          bool inverted, qreal scale, bool transparentBackground, bool alwaysOnTop);

  signals:
    void activated(snipaste::ui::PasteWindow *window, Qt::KeyboardModifiers modifiers);
    void pasteClosed(snipaste::ui::PasteWindow *window);
    void selectAllRequested(snipaste::ui::PasteWindow *window);
    void copyRequested(snipaste::ui::PasteWindow *window);
    void copyAsFileRequested(snipaste::ui::PasteWindow *window);
    void moveRequested(snipaste::ui::PasteWindow *window, QPoint delta);
    void positionChanged(snipaste::ui::PasteWindow *window, Qt::KeyboardModifiers modifiers);
    void wheelTransformRequested(snipaste::ui::PasteWindow *window, qreal scaleFactor, qreal opacityDelta);

  protected:
    bool event(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

  private:
    friend class PasteManager;
    friend class PasteEditorView;
    core::PasteWindowState snapshot() const;
    void restore(const core::PasteWindowState &state);
    void downloadDroppedImage(const QUrl &url);
    qint64 m_savedImageKey = 0;
    QString m_savedImagePath;
    void setSourceImage(const QImage &image);
    void applyDroppedImage(const QImage &image);
    void rebuildImage();
    void updateDisplaySize();
    void hidePaste();
    void changeScale(qreal factor);
    void setAlwaysOnTop(bool enabled);
    void toggleBackgroundMode();
    void toggleThumbnailMode();
    void cropImage();
    void applySelectionThumbnail(const QRect &selection);
    void editSize();
    void editColorCard();
    void createEditor();
    void updateEditorGeometry();
    void updateToolbarPosition();
    void updateShadow();
    void paintBorder(QPainter &painter) const;
    void updateBorder();
    void copyImage();
    void saveImage();
    void extractText();
    void populateOutputMenu(QMenu *menu);
    void showShareMenu(const QPoint &position);
    void startScrollCapture();
    Qt::Edges resizeEdgesAt(const QPoint &position) const;
    void resizeFromPointer(const QPoint &globalPosition);
    void updateResizeCursor(const QPoint &position);
    QString mouseAction(int index, const QString &fallback) const;
    bool mouseModifierMatches(const QString &value, Qt::KeyboardModifiers modifiers) const;
    bool mouseButtonMatches(const QString &value, Qt::MouseButton button, bool doubleClick) const;

    QImage m_sourceImage;
    QImage m_image;
    QString m_sourceText;
    State m_state = State::Active;
    int m_rotationQuarterTurns = 0;
    bool m_flipHorizontal = false;
    bool m_flipVertical = false;
    bool m_grayscale = false;
    bool m_inverted = false;
    qreal m_scale = 1.0;
    bool m_transparentBackground = false;
    QString m_backgroundMode = QStringLiteral("伪透明");
    bool m_alwaysOnTop = true;
    bool m_clickThrough = false;
    bool m_shadowEnabled = true;
    bool m_selected = false;
    bool m_resizeEnabled = true;
    bool m_exchangeNavigationKeys = false;
    bool m_smoothScale = true;
    QString m_escapeAction = QStringLiteral("关闭贴图");
    QString m_shiftEscapeAction = QStringLiteral("销毁贴图");
    QStringList m_mouseActions;
    qint64 m_maxNetworkImageBytes = 20LL * 1024 * 1024;
    int m_maxSize = 12000;
    QSize m_thumbnailSize = QSize(62, 62);
    bool m_wheelCenter = false;
    bool m_resetTransform = true;
    bool m_disableDpiScaling = false;
    bool m_helpKeys = false;
    bool m_closeConfirm = true;
    bool m_destroyConfirm = true;
    bool m_soloHidden = false;
    bool m_virtualDesktopPinned = false;
    bool m_activateOnShow = true;
    bool m_flashTransparency = true;
    bool m_keepOnDrag = false;
    bool m_flashActive = false;
    bool m_flashBorder = false;
    int m_flashStep = 0;
    QTimer m_flashTimer;
    bool m_thumbnailMode = false;
    qreal m_scaleBeforeThumbnail = 1.0;
    PasteManager *m_pasteManager = nullptr;
    bool m_copyAsFileShortcutEnabled = false;
    bool m_showToolbar = false;
    bool m_showAnnotations = true;
    AnnotationCanvas *m_canvas = nullptr;
    QGraphicsView *m_editor = nullptr;
    QGraphicsProxyWidget *m_canvasProxy = nullptr;
    QFrame *m_toolbar = nullptr;
    AnnotationOptions *m_annotationOptions = nullptr;
    std::array<QWidget *, 4> m_shadowWindows{};
    // The outline is a screen decoration controlled by the shadow switch.
    bool m_borderEnabled = true;
    int m_shadowSize = 32;
    QColor m_shadowColor = QColor(60, 68, 80, 100);
    int m_borderWidth = 1;
    // Keep the visible outline after the introductory flash has finished.
    QColor m_borderColor = QColor(220, 225, 232, 220);
    int m_previousBorderUpdateWidth = 0;
    QColor m_customBackgroundColor = QColor(255, 255, 255, 245);
    QPoint m_dragOffset;
    QPoint m_pressPosition;
    bool m_pressed = false;
    bool m_dragging = false;
    bool m_resizing = false;
    bool m_selectionThumbnailCandidate = false;
    bool m_selectionThumbnailDragging = false;
    bool m_selectionThumbnailMoved = false;
    bool m_suppressContextMenu = false;
    QPoint m_selectionThumbnailOrigin;
    QPoint m_selectionThumbnailCurrent;
    Qt::Edges m_resizeEdges;
    QRect m_resizeStartGeometry;
    QPoint m_resizeStartGlobal;
};

} // namespace snipaste::ui
