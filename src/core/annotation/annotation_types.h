#pragma once

#include <QColor>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

namespace snipaste::core {

enum class AnnotationTool { Rectangle, Ellipse, Line, Arrow, Pen, Marker, Text, Number, Mosaic, Blur, Eraser,
                            RoundedRectangle, Magnifier };
enum class ArrowStyle { None, End, Start, Both, Flat, HollowEnd, HollowStart, HollowBoth };
// Mosaic/blur share one renderer.  This only controls the geometry used
// to select pixels for that renderer.
enum class EffectShape { Brush, Rectangle, Ellipse };

struct AnnotationShape {
    AnnotationTool tool = AnnotationTool::Rectangle;
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

    bool operator==(const AnnotationShape &) const = default;
};

bool isNonMovableBrush(AnnotationTool tool);
void translateShape(AnnotationShape &shape, const QPointF &delta);

} // namespace snipaste::core
