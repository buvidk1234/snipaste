#include "ui/annotation/annotation_selection_geometry.h"
#include "ui/annotation/annotation_renderer.h"
#include <QLineF>
#include <QPainterPathStroker>

namespace snipaste::ui::annotation
{
using Tool = core::AnnotationTool;
using Shape = core::AnnotationShape;

QRectF selectionRect(const Shape &shape)
{
    if (shape.tool == Tool::Text)
        return textBoxRect(shape).adjusted(-4, -4, 4, 4);
    if (shape.tool == Tool::Number)
    {
        Shape unrotated = shape;
        unrotated.rotation = 0;
        return shapeBounds(unrotated);
    }
    return QRectF(shape.start, shape.end).normalized();
}

bool textInteriorContains(const Shape &shape, QPointF point, qreal edgeWidth)
{
    return selectionRect(shape).adjusted(edgeWidth, edgeWidth, -edgeWidth, -edgeWidth)
        .contains(selectionTransform(shape).inverted().map(point));
}

QTransform selectionTransform(const Shape &shape)
{
    Shape unrotated = shape;
    unrotated.rotation = 0;
    const QPointF center = shapeBounds(unrotated).center();
    return QTransform().translate(center.x(), center.y()).rotate(shape.rotation).translate(-center.x(), -center.y());
}

std::array<QPointF, 8> selectionHandles(const QRectF &rect)
{
    return {rect.topLeft(), QPointF(rect.center().x(), rect.top()), rect.topRight(),
            QPointF(rect.right(), rect.center().y()), rect.bottomRight(),
            QPointF(rect.center().x(), rect.bottom()), rect.bottomLeft(), QPointF(rect.left(), rect.center().y())};
}

bool hasEdgeHandles(const core::AnnotationShape &shape, qreal tolerance)
{
    if (shape.tool == Tool::Line || shape.tool == Tool::Arrow || shape.tool == Tool::Text)
        return false;
    const QRectF rect = selectionRect(shape);
    const qreal threshold = tolerance * 5.5;
    return rect.width() >= threshold && rect.height() >= threshold;
}

int selectionHandleAt(const Shape &shape, QPointF point, qreal tolerance)
{
    point = selectionTransform(shape).inverted().map(point);
    if (shape.tool == Tool::Line || shape.tool == Tool::Arrow)
    {
        if (QLineF(point, shape.start).length() <= tolerance) return 0;
        if (QLineF(point, shape.end).length() <= tolerance) return 4;
        return -1;
    }
    const QRectF rect = selectionRect(shape);
    const auto handles = selectionHandles(rect);
    const bool edgeHandles = hasEdgeHandles(shape, tolerance);
    for (int index : {0, 2, 4, 6, 1, 3, 5, 7})
    {
        if (index % 2 != 0 && !edgeHandles)
            continue;
        if (QLineF(point, handles[index]).length() <= tolerance)
            return index;
    }
    return -1;
}

bool hasRotationHandle(const Shape &shape)
{
    return shape.tool != Tool::Line && shape.tool != Tool::Arrow;
}

QPointF rotationHandle(const Shape &shape)
{
    const QRectF rect = selectionRect(shape);
    return selectionTransform(shape).map(QPointF(rect.center().x(), rect.top() - 16));
}

bool selectable(const Shape &shape)
{
    return shape.tool != Tool::Pen && shape.tool != Tool::Marker &&
           !((shape.tool == Tool::Mosaic || shape.tool == Tool::Blur) && shape.effectShape == core::EffectShape::Brush);
}

bool containsShape(const Shape &shape, QPointF point, qreal tolerance)
{
    if (!selectable(shape) || shape.erasedPath.contains(point))
        return false;
    point = selectionTransform(shape).inverted().map(point);
    if (shape.tool == Tool::Arrow)
    {
        const QPainterPath footprint = arrowPath(shape);
        QPainterPathStroker stroker;
        stroker.setWidth(tolerance * 2);
        return footprint.contains(point) ||
               (tolerance > 0 && stroker.createStroke(footprint).contains(point));
    }
    if (shape.tool == Tool::Line)
    {
        QPainterPath line;
        line.moveTo(shape.start);
        line.lineTo(shape.end);
        QPainterPathStroker stroker;
        stroker.setWidth(qMax(shape.width, tolerance * 2));
        return stroker.createStroke(line).contains(point);
    }
    if ((shape.tool == Tool::Rectangle || shape.tool == Tool::RoundedRectangle) && !shape.filled)
    {
        QPainterPath path;
        const QRectF rect = selectionRect(shape);
        if (shape.tool == Tool::RoundedRectangle)
        {
            const qreal r = shape.cornerRadius > 0 ? shape.cornerRadius : 12.0;
            path.addRoundedRect(rect, r, r);
        }
        else
        {
            path.addRect(rect);
        }
        QPainterPathStroker stroker;
        stroker.setWidth(qMax<qreal>(shape.width, 2.0) + tolerance * 2.0);
        return stroker.createStroke(path).contains(point);
    }
    if (shape.tool == Tool::Ellipse && !shape.filled)
    {
        QPainterPath path;
        path.addEllipse(selectionRect(shape));
        QPainterPathStroker stroker;
        stroker.setWidth(qMax<qreal>(shape.width, 2.0) + tolerance * 2.0);
        return stroker.createStroke(path).contains(point);
    }
    return selectionRect(shape).adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(point) ||
           (shape.tool == Tool::Magnifier && shape.sourceRect.contains(point));
}

void resizeShape(Shape &shape, int handle, QPointF delta)
{
    const QRectF oldRect = selectionRect(shape);
    const auto oldTransform = selectionTransform(shape);
    const QPointF localDelta = oldTransform.inverted().map(delta) - oldTransform.inverted().map(QPointF());
    if (shape.tool == Tool::Line || shape.tool == Tool::Arrow)
    {
        const QPointF anchor = oldTransform.map(handle == 0 ? shape.end : shape.start);
        if (handle == 0) shape.start += localDelta;
        else shape.end += localDelta;
        const auto nextTransform = selectionTransform(shape);
        core::translateShape(shape, anchor - nextTransform.map(handle == 0 ? shape.end : shape.start));
        return;
    }
    QRectF resized = oldRect;
    if (handle == 0 || handle == 6 || handle == 7) resized.setLeft(resized.left() + localDelta.x());
    if (handle == 0 || handle == 1 || handle == 2) resized.setTop(resized.top() + localDelta.y());
    if (handle == 2 || handle == 3 || handle == 4) resized.setRight(resized.right() + localDelta.x());
    if (handle == 4 || handle == 5 || handle == 6) resized.setBottom(resized.bottom() + localDelta.y());
    if (oldRect.width() <= 0 || oldRect.height() <= 0 ||
        (shape.tool != Tool::Text && (resized.width() < 2 || resized.height() < 2)))
        return;
    qreal sx = resized.width() / oldRect.width(), sy = resized.height() / oldRect.height();
    if (shape.tool == Tool::Text) {
        // Project the dragged corner onto the original diagonal to scale the
        // entire text proportionally, keeping its explicit line breaks.
        const auto handles = selectionHandles(oldRect);
        const QPointF axis = handles[handle] - handles[(handle + 4) % 8];
        const qreal factor = QPointF::dotProduct(axis + localDelta, axis) / QPointF::dotProduct(axis, axis);
        const qreal nextSize = qBound<qreal>(6, shape.fontSize * factor, 96);
        sx = sy = nextSize / shape.fontSize;
        shape.fontSize = nextSize;
    }
    const QTransform resize = QTransform().translate(resized.left(), resized.top()).scale(sx, sy)
                                     .translate(-oldRect.left(), -oldRect.top());
    shape.start = resize.map(shape.start);
    shape.end = resize.map(shape.end);
    for (auto &point : shape.points) point = resize.map(point);
    shape.sourceRect = resize.mapRect(shape.sourceRect);
    // Erasure paths are stored in image coordinates; preserve their placement
    // through the old rotation, local resize and new rotation.
    if (shape.tool == Tool::Number)
        shape.numberSize = qBound<qreal>(12, shape.numberSize * qMax(sx, sy), 96);
    const auto newTransform = selectionTransform(shape);
    shape.erasedPath = newTransform.map(resize.map(oldTransform.inverted().map(shape.erasedPath)));
    // The opposite handle stays fixed even when resizing a rotated object.
    const int opposite = (handle + 4) % 8;
    const QPointF before = oldTransform.map(selectionHandles(oldRect)[opposite]);
    const QPointF after = newTransform.map(selectionHandles(selectionRect(shape))[opposite]);
    core::translateShape(shape, before - after);
}
} // namespace snipaste::ui::annotation
