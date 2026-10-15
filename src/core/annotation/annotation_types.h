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
enum class ArrowHead {
    None = 0,
    Flat = 1,
    Circle = 2,
    Diamond = 3,
    Triangle = 4,
    Classic = 5,
    Stealth = 6,
    Hollow = 7,
    Solid = Classic
};

inline ArrowHead startArrowHead(ArrowStyle style) {
    switch (style) {
    case ArrowStyle::Start:
    case ArrowStyle::Both:
        return ArrowHead::Solid;
    case ArrowStyle::HollowStart:
    case ArrowStyle::HollowBoth:
        return ArrowHead::Hollow;
    default:
        return ArrowHead::None;
    }
}

inline ArrowHead endArrowHead(ArrowStyle style) {
    switch (style) {
    case ArrowStyle::End:
    case ArrowStyle::Both:
        return ArrowHead::Solid;
    case ArrowStyle::HollowEnd:
    case ArrowStyle::HollowBoth:
        return ArrowHead::Hollow;
    case ArrowStyle::Flat:
        return ArrowHead::Flat;
    default:
        return ArrowHead::None;
    }
}

inline ArrowStyle combineArrowHeads(ArrowHead start, ArrowHead end) {
    if (start == ArrowHead::None && end == ArrowHead::None)
        return ArrowStyle::None;
    if (start == ArrowHead::None) {
        if (end == ArrowHead::Hollow) return ArrowStyle::HollowEnd;
        if (end == ArrowHead::Flat) return ArrowStyle::Flat;
        return ArrowStyle::End;
    }
    if (end == ArrowHead::None) {
        if (start == ArrowHead::Hollow) return ArrowStyle::HollowStart;
        if (start == ArrowHead::Flat) return ArrowStyle::Flat;
        return ArrowStyle::Start;
    }
    if (start == ArrowHead::Hollow && end == ArrowHead::Hollow)
        return ArrowStyle::HollowBoth;
    return ArrowStyle::Both;
}

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
    ArrowHead startArrowHead = ArrowHead::None;
    ArrowHead endArrowHead = ArrowHead::Classic;
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
