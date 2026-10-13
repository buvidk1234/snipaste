#pragma once

#include <QImage>
#include <QColor>
#include <QTransform>
#include <QStringList>
#include <QWidget>

class QContextMenuEvent;
class QDragEnterEvent;
class QDropEvent;
class QKeyEvent;
class QMouseEvent;
class QWheelEvent;

namespace snipaste::ui {

class PasteManager;

class PasteWindow final : public QWidget
{
    Q_OBJECT
public:
    enum class State { Active, Closed, Destroyed };

    explicit PasteWindow(QImage image, QWidget *parent = nullptr);

    void showAtCursor();
    void showAtPosition(const QPoint &globalPosition);
    void showExisting();
    State state() const;
    void restoreClosed();
    const QImage &sourceImage() const;
    const QImage &displayedImage() const;
    void setSourceText(const QString &text);
    const QString &sourceText() const;
    bool clickThrough() const;
    void setClickThrough(bool enabled);
    void setShadowEnabled(bool enabled);
    bool shadowEnabled() const;
    void setResizeEnabled(bool enabled);
    bool resizeEnabled() const;
    void setExchangeNavigationKeys(bool enabled);
    void setMaxNetworkImageBytes(qint64 bytes);
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
    bool alwaysOnTop() const;
    bool thumbnailMode() const;
    void setThumbnailMode(bool enabled);
    void scaleBy(qreal factor);
    void adjustOpacity(qreal delta);
    void restoreTransform(int rotationQuarterTurns,
                          bool flipHorizontal,
                          bool flipVertical,
                          bool grayscale,
                          bool inverted,
                          qreal scale,
                          bool transparentBackground,
                          bool alwaysOnTop);

signals:
    void activated(snipaste::ui::PasteWindow *window, Qt::KeyboardModifiers modifiers);
    void pasteClosed(snipaste::ui::PasteWindow *window);
    void selectAllRequested(snipaste::ui::PasteWindow *window);
    void copyRequested(snipaste::ui::PasteWindow *window);
    void moveRequested(snipaste::ui::PasteWindow *window, QPoint delta);
    void positionChanged(snipaste::ui::PasteWindow *window);
    void wheelTransformRequested(snipaste::ui::PasteWindow *window,
                                 qreal scaleFactor, qreal opacityDelta);

protected:
    bool event(QEvent *event) override;
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
    void setSourceImage(const QImage &image);
    void rebuildImage();
    void updateDisplaySize();
    void hidePaste();
    void changeScale(qreal factor);
    void setAlwaysOnTop(bool enabled);
    void toggleBackgroundMode();
    void toggleClickThrough();
    void toggleThumbnailMode();
    void cropImage();
    void applySelectionThumbnail(const QRect &selection);
    void editSize();
    void editColorCard();
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
    bool m_resizeEnabled = true;
    bool m_exchangeNavigationKeys = false;
    bool m_smoothScale = true;
    QString m_escapeAction = QStringLiteral("关闭贴图");
    QString m_shiftEscapeAction = QStringLiteral("销毁贴图");
    QStringList m_mouseActions;
    qint64 m_maxNetworkImageBytes = 20LL * 1024 * 1024;
    bool m_soloHidden = false;
    bool m_virtualDesktopPinned = false;
    bool m_thumbnailMode = false;
    qreal m_scaleBeforeThumbnail = 1.0;
    PasteManager *m_pasteManager = nullptr;
    bool m_showToolbar = true;
    bool m_showAnnotations = true;
    // A pin should remain distinguishable from the desktop even when its
    // shadow is disabled or the captured pixels are dark.  Keep this as a
    // lightweight one-pixel edge; the per-pin context menu can still turn it
    // off when an unobstructed export is needed.
    bool m_borderEnabled = true;
    int m_shadowSize = 32;
    QColor m_shadowColor = QColor(0, 0, 0, 150);
    int m_borderWidth = 1;
    QColor m_borderColor = QColor(70, 70, 70, 180);
    QColor m_customBackgroundColor = QColor(255, 255, 255, 245);
    QPoint m_dragOffset;
    QPoint m_pressPosition;
    bool m_pressed = false;
    bool m_dragging = false;
    bool m_resizing = false;
    bool m_selectionThumbnailCandidate = false;
    bool m_selectionThumbnailDragging = false;
    bool m_selectionThumbnailMoved = false;
    QPoint m_selectionThumbnailOrigin;
    QPoint m_selectionThumbnailCurrent;
    Qt::Edges m_resizeEdges;
    QRect m_resizeStartGeometry;
    QPoint m_resizeStartGlobal;
};

} // namespace snipaste::ui
