#include "ui/annotation/annotation_stroke_preview.h"
#include "ui/annotation/annotation_renderer.h"

#include <QPainter>
#include <cmath>

namespace snipaste::ui::annotation
{
namespace
{
constexpr qsizetype segmentsPerChunk = 64;
}

void StrokePreview::index(const QVector<QPointF> &points)
{
    if (points.size() < m_indexedPoints ||
        (m_indexedPoints > 0 && points.at(m_indexedPoints - 1) != m_lastIndexed))
    {
        m_chunks.clear();
        m_indexedPoints = 0;
    }
    for (qsizetype point = qMax<qsizetype>(1, m_indexedPoints); point < points.size(); ++point)
    {
        const qsizetype chunk = (point - 1) / segmentsPerChunk;
        if (chunk == m_chunks.size())
            m_chunks.append(QRectF(points.at(point - 1), QSizeF()));
        auto &bounds = m_chunks[chunk];
        const QPointF next = points.at(point);
        bounds = QRectF(QPointF(qMin(bounds.left(), next.x()), qMin(bounds.top(), next.y())),
                        QPointF(qMax(bounds.right(), next.x()), qMax(bounds.bottom(), next.y())));
    }
    m_indexedPoints = points.size();
    if (!points.isEmpty())
        m_lastIndexed = points.last();
}

void StrokePreview::draw(const AnnotationRenderer &renderer, QPainter &painter,
                         const core::AnnotationShape &shape, const QRectF &visible)
{
    using Tool = core::AnnotationTool;
    if ((shape.tool != Tool::Pen && shape.tool != Tool::Marker) || shape.points.size() < 2 ||
        shape.penStyle != Qt::SolidLine || !qFuzzyIsNull(shape.rotation) || !shape.erasedPath.isEmpty() ||
        visible.isEmpty())
    {
        renderer.drawShape(painter, shape);
        return;
    }
    index(shape.points);
    const auto inverse = painter.deviceTransform().inverted();
    const qreal pixel = qMax(std::hypot(inverse.m11(), inverse.m12()),
                             std::hypot(inverse.m21(), inverse.m22()));
    const qreal margin = qMax<qreal>(1, shape.width) * 0.5 + 4 * pixel + 2;
    const QRectF query = visible.adjusted(-margin, -margin, margin, margin);
    qsizetype first = -1;
    qsizetype last = -1;
    for (qsizetype chunk = 0; chunk < m_chunks.size(); ++chunk)
    {
        // Inflating also makes horizontal, vertical and repeated-point chunks
        // nonempty, so their caps remain visible at the edge of a dirty area.
        const bool intersects = m_chunks.at(chunk).adjusted(-margin, -margin, margin, margin).intersects(query);
        if (intersects && first < 0)
            first = chunk;
        if (intersects)
            last = chunk;
    }
    if (first < 0)
        return;
    const auto firstPoint = qMax<qsizetype>(0, first * segmentsPerChunk - 1);
    const auto lastPoint = qMin(shape.points.size() - 1, (last + 1) * segmentsPerChunk + 1);
    if (firstPoint == 0 && lastPoint == shape.points.size() - 1)
    {
        renderer.drawShape(painter, shape);
        return;
    }
    // Keep a single draw call, including gaps between visible spans. Splitting
    // a translucent, self-intersecting stroke into calls changes compositing.
    renderer.drawStroke(painter, shape,
                        {shape.points.constData() + firstPoint, size_t(lastPoint - firstPoint + 1)});
}

} // namespace snipaste::ui::annotation
