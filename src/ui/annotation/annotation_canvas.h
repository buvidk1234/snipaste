#pragma once

#include "core/annotation/annotation_document.h"
#include <QColor>
#include <QHash>
#include <QImage>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>
#include <QWidget>
#include <functional>
#include <memory>

class QKeyEvent;
class QMouseEvent;
class QPainter;
class QPlainTextEdit;
class QWheelEvent;

namespace snipaste::ui
{
namespace annotation
{
class EffectCache;
class StrokePreview;
}

class AnnotationCanvas final : public QWidget
{
    Q_OBJECT

  public:
    using Tool = core::AnnotationTool;
    using ArrowStyle = core::ArrowStyle;
    using EffectShape = core::EffectShape;

    explicit AnnotationCanvas(QImage image, QWidget *parent = nullptr);
    ~AnnotationCanvas() override;

    void setTool(Tool tool);
    Tool tool() const { return m_tool; }
    void setRememberToolColors(bool enabled) { m_rememberToolColors = enabled; }
    void setFreeSelect(bool enabled) { m_freeSelect = enabled; }
    void setDisableEraserEditing(bool enabled) { m_disableEraserEditing = enabled; }
    QColor penColor() const { return m_penColor; }
    qreal penWidth() const { return m_penWidth; }
    void setPenColor(const QColor &color);
    void setPenWidth(qreal width);
    void adjustToolSize(int steps);
    void setPenStyle(Qt::PenStyle style);
    void setTextBackground(bool enabled);
    void setShapeFilled(bool enabled);
    bool shapeFilled() const { return m_shapeFilled; }
    void setTextBold(bool enabled);
    void setTextItalic(bool enabled);
    void setTextFontFamily(const QString &family);
    void setTextFontSize(qreal size);
    void setCornerRadius(qreal radius);
    qreal cornerRadius() const { return m_cornerRadius; }
    void setMagnification(qreal factor);
    void setMagnifierCutout(bool enable);
    bool magnifierCutout() const { return m_magnifierCutout; }
    void setMagnifierConnectorStyle(int style);
    int magnifierConnectorStyle() const { return m_magnifierConnectorStyle; }
    void setNumberStyle(int style);
    void setNumberSize(qreal size);
    void setArrowStyle(ArrowStyle style);
    ArrowStyle arrowStyle() const { return m_arrowStyle; }
    void setStartArrowHead(core::ArrowHead head);
    core::ArrowHead startArrowHead() const { return m_startArrowHead; }
    void setEndArrowHead(core::ArrowHead head);
    core::ArrowHead endArrowHead() const { return m_endArrowHead; }
    void setTextOutlineColor(const QColor &color);
    void setTextOutlineWidth(qreal width);
    QColor textOutlineColor() const { return m_textOutlineColor; }
    qreal textOutlineWidth() const { return m_textOutlineWidth; }
    void setEffectSize(int size);
    void setEffectShape(EffectShape shape);
    // The inline snipping canvas can keep the selection widget geometry for
    // resize-hit testing while its image coordinate space spans the complete
    // captured desktop.  This lets strokes start outside the selection and
    // remain available if the selection is later expanded.
    void setCanvasFrame(QPoint origin, QSizeF logicalSize, QRect outputViewport);
    // The snipping overlay already draws the committed desktop and marks.
    // Painting that background again in a child can change fractional-DPI sampling.
    void setBackgroundVisible(bool visible) { m_backgroundVisible = visible; update(); }
    EffectShape effectShape() const { return m_effectShape; }
    void setAutoSelectOnCreation(bool enabled) { m_autoSelectOnCreation = enabled; }
    // Re-apply the cursor after the parent capture overlay has switched
    // mouse ownership from the selection handles to the annotation canvas.
    void refreshToolCursor() { updateToolCursor(); }
    void updatePointerCursor(const QPointF &position);
    bool ownsPointerAt(const QPointF &position) const;
    void setCaptureImage(QImage image, QPointF annotationOffset = {}, QSizeF logicalSize = {});
    // Reset session state and release image buffers, keeping the text editor
    // and tool preferences ready for the next capture.
    void resetCapture();
    bool isDrawing() const
    {
        return m_dragging || m_textEditing || m_editingShape || m_editingRotation || m_resizingHandle >= 0;
    }
    void finishTextInput(bool commit = true);
    QImage renderedImage(bool includePendingText = true) const;
    QImage unannotatedImage() const;
    core::AnnotationDocument selectionDocument() const;
    QImage fullRenderedImage(bool includePendingText = true) const;
    // Background shared by the canvas and its parent during text editing.
    QImage displayImage() const;
    core::AnnotationShape annotationStyle() const { return currentShape(); }
    void undo();
    void redo();
    void clearAnnotations();
    bool canUndo() const { return m_document.canUndo(); }
    bool canRedo() const { return m_document.canRedo(); }
    const core::AnnotationDocument &document() const { return m_document; }
    void setDocument(const core::AnnotationDocument &document);
    int selectedShapeIndex() const { return m_selectedShape; }

  signals:
    void historyChanged();
    void penWidthChanged(qreal width);
    void penColorChanged(const QColor &color);
    void penStyleChanged(Qt::PenStyle style);
    void arrowStyleChanged(ArrowStyle style);
    void textStyleChanged();
    void textSelected();
    void selectionChanged();
    void annotationStyleChanged();

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

  private:
    using Shape = core::AnnotationShape;
    struct ToolStyle
    {
        QColor color = QColor(237, 28, 36);
        qreal width = 2;
        Qt::PenStyle penStyle = Qt::SolidLine;
    };

    enum class MagnifierPart
    {
        None,
        Target,
        Source
    };

    QRectF imageRect() const;
    QRect outputPixelRect() const;
    QPointF toImagePoint(const QPointF &point) const;
    void updateToolCursor();
    Shape currentShape() const;
    void beginTextInput(const QPointF &position);
    void editTextShape(int index);
    void showTextEditor();
    void syncTextStyle(const Shape &shape);
    void syncSelectedStyle();
    bool editSelectedStyle(const std::function<void(Shape &)> &edit);
    void editTextStyle(const std::function<void(Shape &)> &edit);
    void updateTextEditorStyle();
    void updateTextEditorGeometry();
    bool handleTextEditorPointer(QObject *watched, QEvent *event);
    int shapeAt(const QPointF &point) const;
    void translateMagnifierPart(Shape &shape, MagnifierPart part, const QPointF &delta) const;
    void rotateSelected(qreal degrees);
    void copySelectedShape();
    void pasteShape();
    void applyEraserSegment(const QPointF &from, const QPointF &to);
    void recordHistory();
    void finishHistoryGesture();
    void documentRestored();
    void invalidateRenderCache();
    QImage renderDocument(int hiddenTextIndex = -1) const;
    QImage renderShapes(const QVector<Shape> &shapes) const;
    void updateImageRegion(const QRectF &imageRegion);
    void paintBackground(QPainter &painter, const QImage &display, const QRectF &frame) const;
    bool usesScaledBackground() const;
    bool canCacheBackground() const;

    QImage m_image;
    bool m_backgroundVisible = true;
    QSizeF m_logicalImageSize;
    bool m_hasFixedLogicalSize = false;
    QPoint m_coordinateOrigin;
    QRect m_outputViewport;
    core::AnnotationDocument m_document;
    QVector<Shape> m_shapeClipboard;
    // The committed annotation layer is independent from the in-progress
    // preview.  Keep one rasterized copy so pointer moves do not redraw every
    // existing shape (effects can be particularly expensive).
    mutable QImage m_renderCache;
    mutable std::unique_ptr<annotation::EffectCache> m_effectCache;
    std::unique_ptr<annotation::StrokePreview> m_strokePreview;
    mutable bool m_renderCacheValid = false;
    mutable quint64 m_renderCacheRevision = 0;
    mutable int m_renderCacheHiddenText = -1;
    // Only the visible, scaled preview is cached, with a 16 MiB upper bound.
    // Native-pixel canvases continue to paint the shared source directly.
    mutable QImage m_previewBackground;
    mutable qint64 m_previewSourceKey = 0;
    mutable QRectF m_previewFrame;
    mutable qreal m_previewDpr = 0;
    Tool m_tool = Tool::Rectangle;
    QHash<int, ToolStyle> m_toolStyles;
    QPointF m_start;
    QPointF m_current;
    bool m_dragging = false;
    bool m_editingShape = false;
    bool m_editingRotation = false;
    int m_resizingHandle = -1;
    MagnifierPart m_editingMagnifierPart = MagnifierPart::None;
    int m_selectedShape = -1;
    QPlainTextEdit *m_textEditor = nullptr;
    Shape m_textShape;
    int m_textShapeIndex = -1;
    int m_hoveredText = -1;
    int m_hoveredShape = -1;
    bool m_textEditing = false;
    bool m_movingTextBox = false;
    int m_textBoxHandle = -1;
    QPointF m_textBoxPointer;
    QVector<QPointF> m_currentPoints;
    QColor m_penColor = QColor(237, 28, 36);
    qreal m_penWidth = 2.0;
    Qt::PenStyle m_penStyle = Qt::SolidLine;
    bool m_textBackground = false;
    bool m_shapeFilled = false;
    ArrowStyle m_arrowStyle = ArrowStyle::End;
    core::ArrowHead m_startArrowHead = core::ArrowHead::None;
    core::ArrowHead m_endArrowHead = core::ArrowHead::Classic;
    QColor m_textOutlineColor = QColor(0, 0, 0, 0);
    qreal m_textOutlineWidth = 0.0;
    QString m_textFontFamily = QStringLiteral("Microsoft YaHei UI");
    qreal m_textFontSize = 20.0;
    bool m_textBold = false;
    bool m_textItalic = false;
    qreal m_cornerRadius = 12.0;
    qreal m_magnification = 2.0;
    int m_numberStyle = 0;
    qreal m_numberSize = 24.0;
    int m_effectSize = 6;
    EffectShape m_effectShape = EffectShape::Brush;
    bool m_magnifierCutout = true;
    int m_magnifierConnectorStyle = 0;
    bool m_autoSelectOnCreation = true;
    bool m_rememberToolColors = true;
    bool m_freeSelect = true;
    bool m_disableEraserEditing = true;
};

} // namespace snipaste::ui
