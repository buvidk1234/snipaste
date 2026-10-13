#pragma once

#include <QDialog>
#include <QColor>
#include <QImage>
#include <QHash>
#include <QPointF>
#include <QPainterPath>
#include <QRectF>
#include <QString>
#include <QVector>
#include <QWidget>

class QKeyEvent;
class QMouseEvent;
class QPainter;
class QPlainTextEdit;
class QWheelEvent;

namespace snipaste::ui {

class AnnotationCanvas final : public QWidget
{
    Q_OBJECT

public:
    enum class Tool { Rectangle, Ellipse, Line, Arrow, Pen, Marker, Text, Number, Mosaic, Blur, Eraser,
                      RoundedRectangle, Magnifier };
    enum class ArrowStyle { None, End, Start, Both, Flat, HollowEnd, HollowStart, HollowBoth };
    // Mosaic/blur share one renderer.  This only controls the geometry used
    // to select pixels for that renderer.
    enum class EffectShape { Brush, Rectangle, Ellipse };

    explicit AnnotationCanvas(QImage image, QWidget *parent = nullptr);
    ~AnnotationCanvas() override;

    void setTool(Tool tool);
    Tool tool() const { return m_tool; }
    QColor penColor() const { return m_penColor; }
    qreal penWidth() const { return m_penWidth; }
    void setPenColor(const QColor &color);
    void setPenWidth(qreal width);
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
    void setMagnifierCutout(bool enable) { m_magnifierCutout = enable; update(); }
    bool magnifierCutout() const { return m_magnifierCutout; }
    void setMagnifierConnectorStyle(int style) { m_magnifierConnectorStyle = style; update(); }
    int magnifierConnectorStyle() const { return m_magnifierConnectorStyle; }
    void setNumberStyle(int style);
    void setNumberSize(qreal size);
    void setArrowStyle(ArrowStyle style);
    ArrowStyle arrowStyle() const { return m_arrowStyle; }
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
    EffectShape effectShape() const { return m_effectShape; }
    void setAutoSelectOnCreation(bool enabled) { m_autoSelectOnCreation = enabled; }
    // Re-apply the cursor after the parent capture overlay has switched
    // mouse ownership from the selection handles to the annotation canvas.
    void refreshToolCursor() { updateToolCursor(); }
    void setCaptureImage(QImage image, QPointF annotationOffset = {},
                         QSizeF logicalSize = {});
    bool isDrawing() const { return m_dragging || m_textEditing; }
    void finishTextInput(bool commit = true);
    QImage renderedImage(bool includePendingText = true) const;
    QImage fullRenderedImage(bool includePendingText = true) const;
    void undo();
    void redo();
    void clearAnnotations();
    bool canUndo() const { return m_eraserUndoAvailable || !m_shapes.isEmpty(); }
    bool canRedo() const { return m_eraserRedoAvailable || !m_redoShapes.isEmpty(); }

signals:
    void historyChanged();
    void penWidthChanged(qreal width);
    void penColorChanged(const QColor &color);
    void penStyleChanged(Qt::PenStyle style);
    void arrowStyleChanged(ArrowStyle style);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    struct Shape {
        Tool tool = Tool::Rectangle;
        QPointF start;
        QPointF end;
        // The source area used by a magnifier.  Keeping it with the shape
        // lets the source outline remain stable while later annotations are
        // painted over the image.
        QRectF sourceRect;
        QString text;
        QVector<QPointF> points;
        QColor color;
        qreal width = 3.0;
        Qt::PenStyle penStyle = Qt::SolidLine;
        bool textBackground = false;
        ArrowStyle arrowStyle = ArrowStyle::End;
        QColor textOutlineColor;
        qreal textOutlineWidth = 0.0;
        QString fontFamily;
        qreal fontSize = 20.0;
        bool textBold = false;
        bool textItalic = false;
        qreal cornerRadius = 12.0;
        qreal magnification = 2.0;
        int numberStyle = 0;
        qreal numberSize = 24.0;
        bool filled = false;
        qreal rotation = 0.0;
        int effectSize = 6;
        EffectShape effectShape = EffectShape::Brush;
        bool magnifierCutout = true;
        int magnifierConnectorStyle = 0;
        // Parts covered by the eraser are kept as a path on the shape.  The
        // path is in image coordinates, so it remains correct when the
        // shape is moved or rotated and can be undone with the shape state.
        QPainterPath erasedPath;
    };
    struct ToolStyle {
        QColor color = QColor(237, 28, 36);
        qreal width = 2;
        Qt::PenStyle penStyle = Qt::SolidLine;
    };

    enum class MagnifierPart { None, Target, Source };

    QRectF imageRect() const;
    QRect outputPixelRect() const;
    QPointF toImagePoint(const QPointF &point) const;
    void updateToolCursor();
    void applyErasedClip(QPainter &painter, const Shape &shape) const;
    void drawShape(QPainter &painter, const Shape &shape, qreal scale) const;
    void drawEffect(QPainter &painter, const QImage &sourceFrame, const Shape &shape) const;
    QPainterPath effectMask(const Shape &shape) const;
    Shape currentShape() const;
    void beginTextInput(const QPointF &position);
    void updateTextEditorGeometry();
    int shapeAt(const QPointF &point) const;
    QRectF shapeBounds(const Shape &shape) const;
    void translateShape(Shape &shape, const QPointF &delta) const;
    QRectF magnifierTargetRect(const Shape &shape) const;
    void translateMagnifierPart(Shape &shape, MagnifierPart part, const QPointF &delta) const;
    void rotateSelected(qreal degrees);
    void copySelectedShape();
    void pasteShape();
    void applyEraserSegment(const QPointF &from, const QPointF &to);
    void invalidateEraserHistory();
    void invalidateRenderCache();

    QImage m_image;
    QSizeF m_logicalImageSize;
    bool m_hasFixedLogicalSize = false;
    QPoint m_coordinateOrigin;
    QRect m_outputViewport;
    QVector<Shape> m_shapes;
    QVector<Shape> m_redoShapes;
    QVector<Shape> m_eraserUndoSnapshot;
    QVector<Shape> m_eraserRedoSnapshot;
    bool m_eraserUndoAvailable = false;
    bool m_eraserRedoAvailable = false;
    bool m_lastOperationWasEraser = false;
    QVector<Shape> m_shapeClipboard;
    // The committed annotation layer is independent from the in-progress
    // preview.  Keep one rasterized copy so pointer moves do not redraw every
    // existing shape (effects can be particularly expensive).
    mutable QImage m_renderCache;
    mutable bool m_renderCacheValid = false;
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
    bool m_textEditing = false;
    QVector<QPointF> m_currentPoints;
    int m_nextNumber = 1;
    QColor m_penColor = QColor(237, 28, 36);
    qreal m_penWidth = 2.0;
    Qt::PenStyle m_penStyle = Qt::SolidLine;
    bool m_textBackground = false;
    bool m_shapeFilled = false;
    ArrowStyle m_arrowStyle = ArrowStyle::End;
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
};

class AnnotationDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit AnnotationDialog(const QImage &image, QWidget *parent = nullptr);

    QImage resultImage() const;

private:
    AnnotationCanvas *m_canvas = nullptr;
};

} // namespace snipaste::ui
