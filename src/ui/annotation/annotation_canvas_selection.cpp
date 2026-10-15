#include "ui/annotation/annotation_canvas.h"
#include "ui/annotation/annotation_selection_geometry.h"
#include <QLineF>
#include <cmath>

namespace snipaste::ui
{
bool AnnotationCanvas::ownsPointerAt(const QPointF &position) const
{
    if (m_resizingHandle >= 0 || m_editingRotation || m_editingShape)
        return true;
    if (m_textEditing || (m_tool == Tool::Eraser && m_disableEraserEditing))
        return false;
    if (m_tool == Tool::Pen || m_tool == Tool::Marker || m_tool == Tool::Eraser ||
        ((m_tool == Tool::Mosaic || m_tool == Tool::Blur) && m_effectShape == EffectShape::Brush))
        return false;
    const QPointF point = toImagePoint(position);
    const qreal tolerance = 7 * m_image.width() / qMax<qreal>(1, imageRect().width());
    if (m_selectedShape >= 0 && m_selectedShape < m_document.shapes().size())
    {
        const Shape &shape = m_document.shapes().at(m_selectedShape);
        if (annotation::selectionHandleAt(shape, point, tolerance) >= 0 ||
            (annotation::hasRotationHandle(shape) &&
             QLineF(point, annotation::rotationHandle(shape)).length() <= tolerance))
            return true;
    }
    return shapeAt(point) >= 0;
}

void AnnotationCanvas::updatePointerCursor(const QPointF &position)
{
    if (m_textEditing || m_dragging)
        return;
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
        return;
    }
    if (m_editingRotation)
    {
        setCursor(Qt::OpenHandCursor);
        return;
    }
    if (m_editingShape)
    {
        setCursor(Qt::SizeAllCursor);
        return;
    }
    const QPointF point = toImagePoint(position);
    const qreal tolerance = 7 * m_image.width() / qMax<qreal>(1, imageRect().width());
    const bool brush = m_tool == Tool::Pen || m_tool == Tool::Marker || m_tool == Tool::Eraser ||
                       ((m_tool == Tool::Mosaic || m_tool == Tool::Blur) && m_effectShape == EffectShape::Brush);
    const int hit = brush ? -1 : shapeAt(point);
    const int hovered = (hit >= 0 && hit != m_selectedShape) ? hit : -1;
    if (hovered != m_hoveredShape)
    {
        m_hoveredShape = hovered;
        m_hoveredText = -1;
        update();
    }
    if (m_selectedShape >= 0 && m_selectedShape < m_document.shapes().size() && !brush)
    {
        const Shape &shape = m_document.shapes().at(m_selectedShape);
        if (annotation::hasRotationHandle(shape) &&
            QLineF(point, annotation::rotationHandle(shape)).length() <= tolerance)
        {
            setCursor(Qt::OpenHandCursor);
            return;
        }
        const int handle = annotation::selectionHandleAt(shape, point, tolerance);
        if (handle >= 0)
        {
            // Side handles resize along their normal; corners use the diagonal.
            qreal angle = shape.rotation;
            if (shape.tool == Tool::Line || shape.tool == Tool::Arrow)
                angle = QLineF(shape.start, shape.end).angle();
            else if (handle == 1 || handle == 5)
                angle += 90;
            else if (handle % 2 == 0)
                angle += (handle == 0 || handle == 4) ? 45 : 135;
            const int sector = (qRound(angle / 45) % 4 + 4) % 4;
            const Qt::CursorShape cursors[] = {Qt::SizeHorCursor, Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor};
            setCursor(cursors[sector]);
            return;
        }
    }
    if (hit >= 0)
    {
        const Shape &shape = m_document.shapes().at(hit);
        setCursor(shape.tool == Tool::Text && annotation::textInteriorContains(shape, point, 5 / qMax<qreal>(0.01, imageRect().width() / m_image.width()))
                      ? Qt::IBeamCursor : Qt::SizeAllCursor);
    }
    else if (cursor().shape() != Qt::BitmapCursor || !brush)
        updateToolCursor();
}

void AnnotationCanvas::leaveEvent(QEvent *event)
{
    if (m_hoveredShape != -1 || m_hoveredText != -1)
    {
        m_hoveredShape = -1;
        m_hoveredText = -1;
        update();
    }
    QWidget::leaveEvent(event);
}
} // namespace snipaste::ui
