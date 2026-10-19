#include "platform/keyboard.h"
#include "ui/annotation/annotation_canvas.h"
#include "ui/annotation/annotation_renderer.h"
#include "ui/annotation/annotation_selection_geometry.h"
#include "ui/annotation/annotation_stroke_preview.h"
#include <QCursor>
#include <QKeyEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPathStroker>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QTransform>
#include <QWheelEvent>
#include <cmath>
#include <utility>

namespace snipaste::ui
{
using core::isNonMovableBrush;
using core::translateShape;

namespace
{
QPointF constrainedDrawPoint(core::AnnotationTool tool, core::EffectShape effectShape, const QPointF &start,
                             const QPointF &raw, bool constrained)
{
    using Tool = core::AnnotationTool;
    using EffectShape = core::EffectShape;
    QPointF point = raw;
    if (constrained)
    {
        if (tool == Tool::Rectangle || tool == Tool::RoundedRectangle || tool == Tool::Ellipse ||
            ((tool == Tool::Mosaic || tool == Tool::Blur) && effectShape != EffectShape::Brush))
        {
            const qreal side = qMin(std::abs(raw.x() - start.x()), std::abs(raw.y() - start.y()));
            point = QPointF(start.x() + (raw.x() < start.x() ? -side : side),
                            start.y() + (raw.y() < start.y() ? -side : side));
        }
        else if (tool == Tool::Line || tool == Tool::Arrow || tool == Tool::Pen || tool == Tool::Marker ||
                 ((tool == Tool::Mosaic || tool == Tool::Blur) && effectShape == EffectShape::Brush))
        {
            const QPointF delta = raw - start;
            const qreal length = std::hypot(delta.x(), delta.y());
            if (length > 0.0)
            {
                constexpr qreal pi = 3.14159265358979323846;
                const qreal angle = std::atan2(delta.y(), delta.x());
                const qreal snapped = std::round(angle / (pi / 4.0)) * (pi / 4.0);
                point = start + QPointF(std::cos(snapped) * length, std::sin(snapped) * length);
            }
        }
    }
    return point;
}
} // namespace

int AnnotationCanvas::shapeAt(const QPointF &point) const
{
    for (int index = m_document.shapes().size() - 1; index >= 0; --index)
    {
        if (annotation::containsShape(m_document.shapes().at(index), point,
                                      5 * m_image.width() / qMax<qreal>(1, imageRect().width())))
            return index;
    }
    return -1;
}

void AnnotationCanvas::finishHistoryGesture()
{
    m_dragging = m_editingShape = m_editingRotation = false;
    m_resizingHandle = -1;
    m_editingMagnifierPart = MagnifierPart::None;
    m_currentPoints.clear();
    m_strokePreview.reset();
    // Mouse moves only update the preview. One completed gesture creates one
    // history entry, and selecting or erasing empty space creates none.
    recordHistory();
}

void AnnotationCanvas::applyEraserSegment(const QPointF &from, const QPointF &to)
{
    const qreal radius = qMax<qreal>(0.5, m_penWidth * 0.5);
    QPainterPath centerline;
    centerline.moveTo(from);
    if (QLineF(from, to).length() > 0.001)
        centerline.lineTo(to);
    QPainterPath eraserPath;
    QPainterPathStroker stroker;
    stroker.setWidth(radius * 2.0);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    eraserPath = stroker.createStroke(centerline);
    if (eraserPath.isEmpty())
        eraserPath.addEllipse(QRectF(from - QPointF(radius, radius), QSizeF(radius * 2.0, radius * 2.0)));

    const QRectF eraseBounds = eraserPath.boundingRect();
    bool changed = false;
    m_document.editShapes([&](Shape &shape) {
        if (!annotation::shapeBounds(shape).intersects(eraseBounds) || shape.erasedPath.contains(eraserPath))
            return false;
        // Resolve overlaps as each small segment arrives. Appending every
        // capsule makes each repaint resolve the entire stroke again.
        // Keep the original vector shape and accumulate covered regions.
        // Rendering subtracts this path from this shape only, so a later
        // annotation is still painted on top of an earlier erased area.
        shape.erasedPath = shape.erasedPath.united(eraserPath);
        changed = true;
        return true;
    });
    if (!changed)
        return;
    invalidateRenderCache();
    update();
}

void AnnotationCanvas::translateMagnifierPart(Shape &shape, MagnifierPart part, const QPointF &delta) const
{
    if (shape.tool != Tool::Magnifier || part == MagnifierPart::None)
    {
        translateShape(shape, delta);
        return;
    }
    if (part == MagnifierPart::Source)
    {
        shape.sourceRect.translate(delta);
        return;
    }
    shape.start += delta;
    shape.end += delta;
    shape.erasedPath.translate(delta);
}

void AnnotationCanvas::rotateSelected(qreal degrees)
{
    if (m_selectedShape < 0 || m_selectedShape >= m_document.shapes().size())
        return;
    if (qFuzzyIsNull(degrees))
        return;
    m_document.editShape(m_selectedShape, [&](Shape &shape) {
        Shape unrotated = shape;
        unrotated.rotation = 0.0;
        const QPointF center = annotation::shapeBounds(unrotated).center();
        const QTransform transform = QTransform()
                                         .translate(center.x(), center.y())
                                         .rotate(degrees)
                                         .translate(-center.x(), -center.y());
        shape.erasedPath = transform.map(shape.erasedPath);
        shape.rotation = std::fmod(shape.rotation + degrees + 360.0, 360.0);
        if (shape.rotation > 180.0)
            shape.rotation -= 360.0;
    });
    invalidateRenderCache();
    if (!m_editingRotation)
        recordHistory();
    update();
}

void AnnotationCanvas::updateToolCursor()
{
    // Shape and text tools use the normal crosshair.  Brush-like tools get a
    // footprint cursor so the pointer communicates both the tool and its
    // current thickness before the user starts drawing.
    if (m_tool == Tool::Number)
    {
        const qreal scale = m_image.isNull() ? 1.0 : imageRect().width() / m_image.width();
        const int cursorSize = qCeil(m_numberSize * scale) + 8;
        const qreal dpr = devicePixelRatioF();
        QPixmap pixmap(qCeil(cursorSize * dpr), qCeil(cursorSize * dpr));
        pixmap.setDevicePixelRatio(dpr);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.translate(cursorSize / 2, cursorSize / 2);
        painter.scale(scale, scale);
        Shape preview = currentShape();
        preview.start = preview.end = {};
        preview.text = QString::number(m_document.nextNumber());
        // Use the committed badge's renderer, including its font and rings.
        annotation::AnnotationRenderer(m_image.size()).drawShape(painter, preview);
        painter.end();
        setCursor(QCursor(pixmap, cursorSize / 2, cursorSize / 2));
        return;
    }
    if (m_tool != Tool::Pen && m_tool != Tool::Marker && m_tool != Tool::Mosaic && m_tool != Tool::Blur &&
        m_tool != Tool::Eraser)
    {
        setCursor(Qt::CrossCursor);
        return;
    }

    const qreal scale = m_image.isNull() || m_image.width() <= 0
                            ? 1.0
                            : qBound<qreal>(0.25, imageRect().width() / m_image.width(), 4.0);
    // Keep the cursor footprint in the same coordinate space as the rendered
    // brush.  The previous 24px cap made a 64px mosaic stroke look two to
    // three times larger than its cursor after using the wheel to increase it.
    const qreal visibleWidth = qMax<qreal>(1.0, m_penWidth * scale);
    // Leave a small margin for the outline/cross and keep the cursor within a
    // practical Windows cursor size while still representing the full brush
    // range (the brush itself is capped at 64 logical pixels).
    const int canvasSize = qBound(32, qCeil(visibleWidth) + 8, 256);
    QPixmap pixmap(canvasSize, canvasSize);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QPointF center(canvasSize / 2.0, canvasSize / 2.0);

    if (m_tool == Tool::Eraser)
    {
        const qreal radius = qMax<qreal>(0.5, visibleWidth / 2.0);
        painter.setPen(QPen(Qt::white, 2.0));
        painter.setBrush(QColor(30, 30, 30, 80));
        painter.drawEllipse(center, radius, radius);
        painter.setPen(QPen(QColor(20, 20, 20, 220), 1.0, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(center, radius, radius);
    }
    else if (m_tool == Tool::Pen)
    {
        // A fine cross and a coloured centre point stay recognisable on both
        // light and dark screenshots.
        QPen outline(QColor(0, 0, 0, 190), 3.0, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
        painter.setPen(outline);
        painter.drawLine(QPointF(3, center.y()), QPointF(canvasSize - 3, center.y()));
        painter.drawLine(QPointF(center.x(), 3), QPointF(center.x(), canvasSize - 3));
        QPen cross(m_penColor, 1.0, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
        painter.setPen(cross);
        painter.drawLine(QPointF(3, center.y()), QPointF(canvasSize - 3, center.y()));
        painter.drawLine(QPointF(center.x(), 3), QPointF(center.x(), canvasSize - 3));
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_penColor);
        painter.drawEllipse(center, qMax<qreal>(2.0, visibleWidth / 2.0),
                            qMax<qreal>(2.0, visibleWidth / 2.0));
    }
    else if (m_tool == Tool::Marker)
    {
        // The marker is a wide, red, opaque footprint.  Keep it visibly
        // different from the pencil's fine cross and from the effect tools.
        const QColor markerColor(235, 55, 55, 235);
        const qreal radius = qMax<qreal>(0.5, visibleWidth / 2.0);
        painter.setPen(QPen(QColor(255, 255, 255, 230), 1.0));
        painter.setBrush(markerColor);
        painter.drawEllipse(center, radius, radius);
        painter.setPen(QPen(QColor(125, 20, 20, 220), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(center, radius, radius);
    }
    else if ((m_tool == Tool::Mosaic || m_tool == Tool::Blur) && m_effectShape != EffectShape::Brush)
    {
        // Rectangle/ellipse effects are drag-to-select modes.  Use the same
        // crosshair as the other geometric annotation tools instead of a
        // brush footprint cursor, which suggests painting before the drag.
        painter.end();
        setCursor(Qt::CrossCursor);
        return;
    }
    else if (m_tool == Tool::Mosaic)
    {
        // Mosaic uses a neutral grey, blocky footprint.  The small checker
        // marks make it distinguishable from blur even at a glance.
        const qreal radius = qMax<qreal>(0.5, visibleWidth / 2.0);
        painter.save();
        QPainterPath clip;
        clip.addEllipse(center, radius, radius);
        painter.setClipPath(clip);
        painter.fillRect(QRectF(center.x() - radius, center.y() - radius, radius * 2.0, radius * 2.0),
                         QColor(145, 145, 145, 235));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(95, 95, 95, 210));
        const qreal cell = qMax<qreal>(3.0, radius / 2.4);
        for (qreal y = center.y() - radius; y < center.y() + radius; y += cell)
        {
            for (qreal x = center.x() - radius; x < center.x() + radius; x += cell)
            {
                if ((qRound((x - center.x()) / cell) + qRound((y - center.y()) / cell)) % 2 == 0)
                    painter.drawRect(QRectF(x, y, cell, cell));
            }
        }
        painter.restore();
        painter.setPen(QPen(QColor(45, 45, 45, 210), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(center, radius, radius);
    }
    else
    {
        // Blur keeps a grey footprint but has a soft inner halo instead of
        // the mosaic checker pattern.
        const qreal radius = qMax<qreal>(0.5, visibleWidth / 2.0);
        painter.setPen(QPen(QColor(255, 255, 255, 210), 1.0));
        painter.setBrush(QColor(155, 155, 155, 185));
        painter.drawEllipse(center, radius, radius);
        painter.setPen(QPen(QColor(75, 75, 75, 180), 1.0, Qt::DashLine));
        painter.setBrush(QColor(205, 205, 205, 90));
        painter.drawEllipse(center, radius * qBound<qreal>(0.35, 1.0 - m_effectSize / 60.0, 0.75),
                            radius * qBound<qreal>(0.35, 1.0 - m_effectSize / 60.0, 0.75));
    }
    painter.end();
    setCursor(QCursor(pixmap, canvasSize / 2, canvasSize / 2));
}

void AnnotationCanvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton)
    {
        if (m_textEditing)
            finishTextInput();
        finishHistoryGesture();
        update();
        setFocus();
        event->accept();
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;
    finishTextInput();
    finishHistoryGesture();
    setFocus();
    const QPointF imagePoint = toImagePoint(event->position());
    if (!(m_tool == Tool::Eraser && m_disableEraserEditing) && m_selectedShape >= 0 &&
        m_selectedShape < m_document.shapes().size())
    {
        const Shape &sel = m_document.shapes().at(m_selectedShape);
        const qreal tolerance = 7 * m_image.width() / qMax<qreal>(1, imageRect().width());
        if (annotation::hasRotationHandle(sel) &&
            QLineF(imagePoint, annotation::rotationHandle(sel)).length() <= tolerance)
        {
            m_editingRotation = true;
            m_start = imagePoint;
            event->accept();
            return;
        }
        const int handle = annotation::selectionHandleAt(sel, imagePoint, tolerance);
        if (handle >= 0)
        {
            m_resizingHandle = handle;
            m_start = imagePoint;
            updatePointerCursor(event->position());
            event->accept();
            return;
        }
    }
    // Freehand tools must always start a new stroke, even over an existing
    // stroke or effect.  Their wide bounds are not selection handles.
    const bool drawingBrush = m_tool == Tool::Pen || m_tool == Tool::Marker ||
        ((m_tool == Tool::Mosaic || m_tool == Tool::Blur) && (m_effectShape == EffectShape::Brush || !m_autoSelectOnCreation));
    const int hit = drawingBrush ? -1 : shapeAt(imagePoint);
    if (hit >= 0 && m_document.shapes().at(hit).tool == Tool::Text && m_tool != Tool::Eraser &&
        m_tool != Tool::Number && (m_freeSelect || m_tool == Tool::Text ||
                                 event->modifiers().testFlag(snipaste::platform::primaryModifier())))
    {
        m_selectedShape = hit;
        const qreal edgeWidth = 5 * m_image.width() / qMax<qreal>(1, imageRect().width());
        if (annotation::textInteriorContains(m_document.shapes().at(hit), imagePoint, edgeWidth))
        {
            editTextShape(hit);
            const QPoint local = m_textEditor->viewport()->mapFrom(this, event->position().toPoint());
            m_textEditor->setTextCursor(m_textEditor->cursorForPosition(local));
            event->accept();
            return;
        }
        m_editingShape = true;
        m_editingMagnifierPart = MagnifierPart::None;
        m_start = m_current = imagePoint;
        syncTextStyle(m_document.shapes().at(hit));
        setCursor(Qt::SizeAllCursor);
        update();
        event->accept();
        return;
    }
    if (hit >= 0 && event->modifiers().testFlag(snipaste::platform::primaryModifier()) &&
        m_tool != Tool::Eraser && m_tool != Tool::Text && m_tool != Tool::Number)
    {
        // Ctrl-drag duplicates an existing annotation.  The small initial
        // offset makes the duplicate visible even when the pointer does not
        // move before release.
        Shape duplicate = m_document.shapes().at(hit);
        const Tool duplicatedTool = duplicate.tool;
        translateShape(duplicate, QPointF(12.0, 12.0));
        m_document.append(std::move(duplicate));
        invalidateRenderCache();
        m_selectedShape = m_document.shapes().size() - 1;
        m_editingShape = true;
        m_editingMagnifierPart =
            duplicatedTool == Tool::Magnifier ? MagnifierPart::Target : MagnifierPart::None;
        m_start = imagePoint;
        m_current = imagePoint;
        syncSelectedStyle();
        emit historyChanged();
        update();
        event->accept();
        return;
    }
    if (hit >= 0 &&
        (hit == m_selectedShape || m_freeSelect || event->modifiers().testFlag(snipaste::platform::primaryModifier()) ||
         m_document.shapes().at(hit).tool == m_tool) &&
        m_tool != Tool::Eraser && m_tool != Tool::Text &&
        (m_tool != Tool::Number || m_document.shapes().at(hit).tool != Tool::Number))
    {
        m_selectedShape = hit;
        syncSelectedStyle();
        m_editingShape = true;
        m_editingMagnifierPart = MagnifierPart::None;
        if (m_document.shapes().at(hit).tool == Tool::Magnifier && m_tool == Tool::Magnifier)
        {
            const Shape &magnifier = m_document.shapes().at(hit);
            const QRectF target = annotation::magnifierTargetRect(magnifier);
            const QRectF source = magnifier.sourceRect.normalized();
            const bool inSource = !source.isEmpty() && source.contains(imagePoint);
            const bool inTarget = target.contains(imagePoint);
            // The source frame is smaller and sits inside the target by
            // default.  Clicking in that small frame selects the sampling
            // range; clicking the surrounding part of the target moves the
            // large display frame.
            if (inSource)
                m_editingMagnifierPart = MagnifierPart::Source;
            else if (inTarget)
                m_editingMagnifierPart = MagnifierPart::Target;
        }
        m_start = imagePoint;
        m_current = imagePoint;
        update();
        event->accept();
        return;
    }
    if (m_selectedShape >= 0)
        update(); // Remove the previous selection handles before local stroke updates.
    m_selectedShape = -1;
    m_editingShape = false;
    m_editingMagnifierPart = MagnifierPart::None;
    m_start = imagePoint;
    m_current = m_start;
    const bool effectBrush =
        (m_tool == Tool::Mosaic || m_tool == Tool::Blur) && m_effectShape == EffectShape::Brush;
    m_currentPoints = effectBrush || m_tool == Tool::Pen || m_tool == Tool::Marker || m_tool == Tool::Eraser
                          ? QVector<QPointF>{m_start}
                          : QVector<QPointF>{};
    m_dragging = true;
    if (m_tool == Tool::Text)
    {
        m_dragging = false;
        beginTextInput(event->position());
        event->accept();
        return;
    }
    if (m_tool == Tool::Number)
    {
        m_dragging = false;
        Shape shape = currentShape();
        shape.text = QString::number(m_document.takeNumber());
        m_document.append(shape);
        invalidateRenderCache();
        if (m_autoSelectOnCreation)
        {
            m_selectedShape = m_document.shapes().size() - 1;
            syncSelectedStyle();
        }
        recordHistory();
        updateToolCursor();
        update();
    }
    if (m_tool == Tool::Eraser)
    {
        applyEraserSegment(imagePoint, imagePoint);
        return;
    }
}

void AnnotationCanvas::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_tool != Tool::Eraser && !isNonMovableBrush(m_tool))
    {
        finishTextInput();
        const int hit = shapeAt(toImagePoint(event->position()));
        if (hit >= 0 && m_document.shapes().at(hit).tool == Tool::Text)
        {
            editTextShape(hit);
            event->accept();
            return;
        }
    }
    QWidget::mouseDoubleClickEvent(event);
}

void AnnotationCanvas::mouseMoveEvent(QMouseEvent *event)
{
    // The first click can open the child editor after the canvas has already
    // acquired the mouse grab. Continue that same gesture as character
    // selection, instead of losing the drag until the user clicks again.
    if (m_textEditing && event->buttons().testFlag(Qt::LeftButton))
    {
        const QPoint local = m_textEditor->viewport()->mapFrom(this, event->position().toPoint());
        QTextCursor cursor = m_textEditor->textCursor();
        cursor.setPosition(m_textEditor->cursorForPosition(local).position(), QTextCursor::KeepAnchor);
        m_textEditor->setTextCursor(cursor);
        event->accept();
        return;
    }
    if (m_editingRotation && m_selectedShape >= 0 && m_selectedShape < m_document.shapes().size())
    {
        const QPointF point = toImagePoint(event->position());
        const QRectF bounds = annotation::shapeBounds(m_document.shapes().at(m_selectedShape));
        const QPointF center = bounds.center();
        const qreal angle1 =
            std::atan2(m_start.y() - center.y(), m_start.x() - center.x()) * 180.0 / 3.14159265358979323846;
        const qreal angle2 =
            std::atan2(point.y() - center.y(), point.x() - center.x()) * 180.0 / 3.14159265358979323846;
        rotateSelected(angle2 - angle1);
        m_start = point;
        emit historyChanged();
        update();
        event->accept();
        return;
    }
    if (m_resizingHandle >= 0 && m_selectedShape >= 0 && m_selectedShape < m_document.shapes().size())
    {
        const Shape &shape = m_document.shapes().at(m_selectedShape);
        qreal angle = shape.rotation;
        if (shape.tool == Tool::Line || shape.tool == Tool::Arrow)
            angle = QLineF(shape.start, shape.end).angle();
        else if (m_resizingHandle == 1 || m_resizingHandle == 5)
            angle += 90;
        else if (m_resizingHandle % 2 == 0)
            angle += (m_resizingHandle == 0 || m_resizingHandle == 4) ? 45 : 135;
        const int sector = (qRound(angle / 45) % 4 + 4) % 4;
        const Qt::CursorShape cursors[] = {Qt::SizeHorCursor, Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor};
        setCursor(cursors[sector]);

        const QPointF point = toImagePoint(event->position());
        const QPointF delta = point - m_start;
        m_document.editShape(m_selectedShape, [&](Shape &shape) { annotation::resizeShape(shape, m_resizingHandle, delta); });
        invalidateRenderCache();
        m_start = point;
        emit historyChanged();
        update();
        event->accept();
        return;
    }
    if (m_editingShape && m_selectedShape >= 0 && m_selectedShape < m_document.shapes().size())
    {
        const QPointF point = toImagePoint(event->position());
        const QPointF delta = point - m_start;
        if (!delta.isNull())
        {
            m_document.editShape(m_selectedShape, [&](Shape &shape) {
                if (shape.tool == Tool::Magnifier)
                    translateMagnifierPart(shape,
                                           m_editingMagnifierPart == MagnifierPart::None
                                               ? MagnifierPart::Target
                                               : m_editingMagnifierPart,
                                           delta);
                else
                    translateShape(shape, delta);
            });
            invalidateRenderCache();
            m_start = point;
            emit historyChanged();
            update();
        }
        event->accept();
        return;
    }
    if (!m_dragging)
    {
        updatePointerCursor(event->position());
        return;
    }
    const QPointF raw = toImagePoint(event->position());
    if (m_tool == Tool::Eraser)
    {
        const QPointF previous = m_currentPoints.isEmpty() ? m_current : m_currentPoints.last();
        m_current = raw;
        m_currentPoints.append(raw);
        applyEraserSegment(previous, raw);
        event->accept();
        return;
    }
    const bool brush = m_tool == Tool::Pen || m_tool == Tool::Marker;
    const bool constrained = event->modifiers().testFlag(Qt::ShiftModifier);
    QRectF dirty;
    if (brush && !constrained)
    {
        // Extending a stroke changes its endpoint cap and the new join. Keep
        // the preceding segment as well as the new one in the repaint area.
        const QPointF previous = m_currentPoints.isEmpty() ? m_start : m_currentPoints.last();
        const QPointF before =
            m_currentPoints.size() > 1 ? m_currentPoints.at(m_currentPoints.size() - 2) : previous;
        dirty = QRectF(before, previous).normalized().united(QRectF(previous, raw).normalized());
    }
    else
    {
        dirty = annotation::shapeBounds(currentShape());
    }
    m_current = constrainedDrawPoint(m_tool, m_effectShape, m_start, raw,
                                     event->modifiers().testFlag(Qt::ShiftModifier));
    if (m_tool == Tool::Pen || m_tool == Tool::Marker ||
        ((m_tool == Tool::Mosaic || m_tool == Tool::Blur) && m_effectShape == EffectShape::Brush))
    {
        if (event->modifiers().testFlag(Qt::ShiftModifier))
        {
            m_strokePreview.reset();
            m_currentPoints = {m_start, m_current};
        }
        else
            m_currentPoints.append(m_current);
    }
    if (m_penStyle != Qt::SolidLine)
    {
        // Qt's dashed rasterization at fractional scales is clip-sensitive.
        // Retain complete repainting for those styles so preview pixels match.
        update();
    }
    else if (brush)
    {
        if (constrained)
            dirty |= annotation::shapeBounds(currentShape());
        const qreal margin = qMax<qreal>(3.0, m_penWidth * 0.5 + 2.0);
        updateImageRegion(dirty.adjusted(-margin, -margin, margin, margin));
    }
    else if (m_tool == Tool::Rectangle || m_tool == Tool::RoundedRectangle || m_tool == Tool::Ellipse ||
             m_tool == Tool::Line || m_tool == Tool::Arrow)
    {
        // Arrow heads can extend beyond the line's endpoints.
        dirty |= annotation::shapeBounds(currentShape());
        updateImageRegion(dirty.adjusted(-20, -20, 20, 20));
    }
    else
    {
        update();
    }
}

void AnnotationCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if ((m_editingShape || m_editingRotation || m_resizingHandle >= 0) && event->button() == Qt::LeftButton)
    {
        finishHistoryGesture();
        syncSelectedStyle();
        updatePointerCursor(event->position());
        update();
        event->accept();
        return;
    }
    if (!m_dragging || event->button() != Qt::LeftButton)
        return;
    const QPointF raw = toImagePoint(event->position());
    m_current = constrainedDrawPoint(m_tool, m_effectShape, m_start, raw,
                                     event->modifiers().testFlag(Qt::ShiftModifier));
    const int shapeCountBefore = m_document.shapes().size();
    if (m_tool == Tool::Eraser)
    {
        const QPointF previous = m_currentPoints.isEmpty() ? m_current : m_currentPoints.last();
        applyEraserSegment(previous, m_current);
        finishHistoryGesture();
        update();
        event->accept();
        return;
    }
    const bool effectBrush =
        (m_tool == Tool::Mosaic || m_tool == Tool::Blur) && m_effectShape == EffectShape::Brush;
    if (effectBrush)
        m_currentPoints.append(m_current);
    if (m_tool == Tool::Pen || m_tool == Tool::Marker || effectBrush)
    {
        // A click is a valid brush stamp in the same way it is in Photoshop;
        // it must leave a dot even when the pointer did not move.
        if (!m_currentPoints.isEmpty())
            m_document.append(currentShape());
    }
    else if (QLineF(m_start, m_current).length() > 2.0)
    {
        m_document.append(currentShape());
    }
    if (m_document.shapes().size() > shapeCountBefore)
        invalidateRenderCache();
    if (m_autoSelectOnCreation && m_document.shapes().size() > shapeCountBefore &&
        annotation::selectable(m_document.shapes().constLast()))
    {
        m_selectedShape = m_document.shapes().size() - 1;
        syncSelectedStyle();
    }
    finishHistoryGesture();
    updatePointerCursor(event->position());
    update();
}

void AnnotationCanvas::wheelEvent(QWheelEvent *event)
{
    int delta = event->angleDelta().y() != 0 ? event->angleDelta().y() : event->angleDelta().x();
    if (delta == 0)
        delta = event->pixelDelta().y() != 0 ? event->pixelDelta().y() : event->pixelDelta().x();
    if (delta == 0)
    {
        event->ignore();
        return;
    }
    const int steps = qMax(1, qAbs(delta) / 120);
    const int sign = delta > 0 ? 1 : -1;
    adjustToolSize(sign * steps);
    event->accept();
}

void AnnotationCanvas::adjustToolSize(int steps)
{
    if (m_tool == Tool::Number)
    {
        setNumberSize(m_numberSize + steps * 2);
    }
    else if (m_tool == Tool::Text)
    {
        setTextFontSize(m_textFontSize + steps * 2);
    }
    else
    {
        setPenWidth(m_penWidth + steps);
    }
}

void AnnotationCanvas::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_F2 && event->modifiers() == Qt::NoModifier && m_selectedShape >= 0 &&
        m_selectedShape < m_document.shapes().size() && m_document.shapes().at(m_selectedShape).tool == Tool::Text)
    {
        editTextShape(m_selectedShape);
        event->accept();
        return;
    }
    if (m_selectedShape >= 0 && event->modifiers().testFlag(Qt::AltModifier))
    {
        if (event->key() == Qt::Key_Left)
        {
            rotateSelected(-15.0);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Right)
        {
            rotateSelected(15.0);
            event->accept();
            return;
        }
    }
    if (event->modifiers() == Qt::NoModifier &&
        (event->key() == Qt::Key_BracketLeft || event->key() == Qt::Key_BracketRight))
    {
        setPenWidth(m_penWidth + (event->key() == Qt::Key_BracketRight ? 1 : -1));
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && m_selectedShape >= 0)
    {
        m_selectedShape = -1;
        m_editingShape = false;
        update();
        return;
    }
    if (event->modifiers().testFlag(snipaste::platform::primaryModifier()) &&
        event->modifiers().testFlag(Qt::ShiftModifier) && event->key() == Qt::Key_C)
    {
        copySelectedShape();
        event->accept();
        return;
    }
    if (event->modifiers().testFlag(snipaste::platform::primaryModifier()) &&
        event->modifiers().testFlag(Qt::ShiftModifier) && event->key() == Qt::Key_V)
    {
        pasteShape();
        event->accept();
        return;
    }
    if (event->modifiers().testFlag(snipaste::platform::primaryModifier()) &&
        event->modifiers().testFlag(Qt::ShiftModifier) && event->key() == Qt::Key_Z)
    {
        clearAnnotations();
        event->accept();
        return;
    }
    if (event->modifiers().testFlag(snipaste::platform::primaryModifier()) && event->key() == Qt::Key_Z)
    {
        undo();
        return;
    }
    if (event->modifiers().testFlag(snipaste::platform::primaryModifier()) &&
        (event->key() == Qt::Key_Y || event->key() == Qt::Key_R))
    {
        redo();
        return;
    }
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
    {
        if (m_selectedShape >= 0 && m_selectedShape < m_document.shapes().size())
        {
            finishHistoryGesture();
            m_document.remove(m_selectedShape);
            invalidateRenderCache();
            m_selectedShape = -1;
            recordHistory();
            update();
        }
        else
        {
            clearAnnotations();
        }
        return;
    }
    QWidget::keyPressEvent(event);
}

void AnnotationCanvas::copySelectedShape()
{
    if (m_selectedShape < 0 || m_selectedShape >= m_document.shapes().size())
        return;
    m_shapeClipboard = {m_document.shapes().at(m_selectedShape)};
}

void AnnotationCanvas::pasteShape()
{
    if (m_shapeClipboard.isEmpty())
        return;
    finishHistoryGesture();
    Shape pasted = m_shapeClipboard.constLast();
    translateShape(pasted, QPointF(12.0, 12.0));
    m_document.append(std::move(pasted));
    invalidateRenderCache();
    m_selectedShape = m_document.shapes().size() - 1;
    syncSelectedStyle();
    recordHistory();
    update();
}

} // namespace snipaste::ui
