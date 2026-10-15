#include "ui/annotation/annotation_canvas.h"
#include "ui/annotation/annotation_renderer.h"
#include "ui/annotation/annotation_effect_cache.h"
#include "ui/annotation/annotation_stroke_preview.h"
#include "ui/annotation/annotation_selection_geometry.h"
#include <QPaintEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <algorithm>

namespace snipaste::ui
{

void AnnotationCanvas::paintEvent(QPaintEvent *event)
{
    const QImage display = displayImage();

    QPainter painter(this);
    if (!testAttribute(Qt::WA_TranslucentBackground))
    {
        painter.fillRect(rect(), QColor(30, 30, 30));
    }
    const QRectF frame = imageRect();
    const QRectF localFrame = frame.translated(-QPointF(m_coordinateOrigin));
    paintBackground(painter, display, localFrame);
    const qreal scale = frame.width() / qMax(1, m_image.width());
    painter.save();
    painter.translate(localFrame.topLeft());
    painter.scale(scale, scale);
    if (m_dragging)
    {
        Shape preview = currentShape();
        // Only the filtered crop is transient.  Drawing it on the widget
        // avoids detaching/copying the complete cached desktop every frame.
        if (m_tool == Tool::Mosaic || m_tool == Tool::Blur)
            annotation::AnnotationRenderer(m_image.size()).drawEffect(painter, display, preview);
        // During the first magnifier gesture the UI is a plain rectangular
        // selection of the source pixels.  The larger target frame and
        // enlarged pixels appear after the drag is committed.
        if (m_tool == Tool::Magnifier)
        {
            const QRectF source = preview.sourceRect.normalized();
            preview.start = source.topLeft();
            preview.end = source.bottomRight();
            preview.sourceRect = {};
        }
        const annotation::AnnotationRenderer renderer(m_image.size());
        if ((m_tool == Tool::Pen || m_tool == Tool::Marker) && preview.points.size() > 128 && scale > 0)
        {
            if (!m_strokePreview)
                m_strokePreview = std::make_unique<annotation::StrokePreview>();
            const QRectF localVisible = QRectF(event->rect()).translated(-localFrame.topLeft());
            const QRectF visible(localVisible.topLeft() / scale, localVisible.size() / scale);
            m_strokePreview->draw(renderer, painter, preview, visible);
        }
        else
            renderer.drawShape(painter, preview, 1.0);
    }
    const int hoverIndex = (m_hoveredShape >= 0) ? m_hoveredShape : m_hoveredText;
    if (!m_textEditing && hoverIndex >= 0 && hoverIndex != m_selectedShape &&
        hoverIndex < m_document.shapes().size())
    {
        const Shape &hovered = m_document.shapes().at(hoverIndex);
        const QRectF shapeRect = annotation::selectionRect(hovered);
        painter.save();
        painter.setTransform(annotation::selectionTransform(hovered), true);
        // Dashed outline directly on the shape itself, no outer bounding box
        QPen hoverPen(QColor(255, 255, 255, 230), 1.0, Qt::DashLine);
        hoverPen.setCosmetic(true);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(hoverPen);
        if (hovered.tool == Tool::Ellipse)
            painter.drawEllipse(shapeRect);
        else if (hovered.tool == Tool::RoundedRectangle)
            painter.drawRoundedRect(shapeRect, hovered.cornerRadius, hovered.cornerRadius);
        else if (hovered.tool == Tool::Line || hovered.tool == Tool::Arrow)
            painter.drawLine(hovered.start, hovered.end);
        else if (hovered.tool == Tool::Text)
        {
            hoverPen.setColor(QColor(35, 35, 35));
            painter.setPen(hoverPen);
            painter.drawRect(shapeRect);
            hoverPen.setColor(Qt::white);
            hoverPen.setDashOffset(3);
            painter.setPen(hoverPen);
            painter.drawRect(shapeRect);
        }
        else
            painter.drawRect(shapeRect);
        painter.restore();
    }
    if (!m_textEditing && !(m_tool == Tool::Eraser && m_disableEraserEditing) && m_selectedShape >= 0 &&
        m_selectedShape < m_document.shapes().size())
    {
        const Shape &sel = m_document.shapes().at(m_selectedShape);
        const QRectF shapeRect = annotation::selectionRect(sel);
        painter.save();
        painter.setTransform(annotation::selectionTransform(sel), true);

        // 8 square resize handles directly on the corners and edges (no outer frame line)
        const qreal hs = 6.0 / qMax<qreal>(0.01, scale);
        const auto handles = annotation::selectionHandles(shapeRect);
        if (sel.tool == Tool::Text)
        {
            QPen framePen(QColor(40, 137, 237), 1, Qt::DashLine);
            framePen.setCosmetic(true);
            painter.setPen(framePen);
            painter.drawRect(shapeRect);
        }
        painter.setPen(QPen(QColor(40, 137, 237), 1.0));
        painter.setBrush(Qt::white);
        const bool line = sel.tool == Tool::Line || sel.tool == Tool::Arrow;
        const bool edgeHandles = annotation::hasEdgeHandles(sel, 7.0 / qMax<qreal>(0.01, scale));
        for (int index = 0; index < int(handles.size()); ++index)
        {
            if (line && index != 0 && index != 4)
                continue;
            if (sel.tool == Tool::Text && index % 2 != 0)
                continue;
            if (index % 2 != 0 && !edgeHandles)
                continue;
            const QPointF pt = line ? (index == 0 ? sel.start : sel.end) : handles[index];
            painter.drawRect(QRectF(pt.x() - hs / 2.0, pt.y() - hs / 2.0, hs, hs));
        }

        if (annotation::hasRotationHandle(sel))
        {
            // Top rotation handle floating above top center
            const QPointF rotHandleCenter(shapeRect.center().x(), shapeRect.top() - 16.0);
            painter.setPen(QPen(QColor(40, 137, 237), 1.2));
            painter.setBrush(Qt::white);
            painter.drawEllipse(rotHandleCenter, 6.5, 6.5);
            painter.drawArc(QRectF(rotHandleCenter.x() - 4.0, rotHandleCenter.y() - 4.0, 8.0, 8.0), 45 * 16,
                            270 * 16);
            painter.drawLine(rotHandleCenter + QPointF(1.5, -3.5), rotHandleCenter + QPointF(3.5, -1.5));
        }
        painter.restore();
    }
    painter.restore();

    // The parent overlay dims everything outside the active selection.  The
    // canvas itself is kept at the selection geometry for compatibility with
    // resize hit testing, so only paint the part of the full frame that falls
    // inside this child widget.
}

QImage AnnotationCanvas::fullRenderedImage(bool includePendingText) const
{
    if (!includePendingText || !m_textEditing)
        return renderDocument();
    QVector<Shape> shapes = m_document.shapes();
    Shape pending = m_textShape;
    pending.text = m_textEditor->toPlainText();
    if (m_textShapeIndex >= 0)
    {
        if (pending.text.isEmpty())
            shapes.removeAt(m_textShapeIndex);
        else
            shapes[m_textShapeIndex] = pending;
    }
    else if (!pending.text.isEmpty())
        shapes.append(pending);
    // Replace in place so objects and effects above the text retain their order.
    return renderShapes(shapes);
}

QImage AnnotationCanvas::displayImage() const
{
    return renderDocument(m_textEditing ? m_textShapeIndex : -1);
}

QImage AnnotationCanvas::renderDocument(int hiddenTextIndex) const
{
    if (m_renderCacheRevision != m_document.revision() || m_renderCacheHiddenText != hiddenTextIndex)
    {
        m_renderCacheValid = false;
        m_renderCache = {};
        m_previewBackground = {};
        m_previewSourceKey = 0;
        m_renderCacheRevision = m_document.revision();
        m_renderCacheHiddenText = hiddenTextIndex;
    }
    if (m_renderCacheValid)
        return m_renderCache;
    if (hiddenTextIndex >= 0)
    {
        QVector<Shape> shapes = m_document.shapes();
        shapes.removeAt(hiddenTextIndex);
        m_renderCache = renderShapes(shapes);
    }
    else
        m_renderCache = renderShapes(m_document.shapes());
    m_renderCacheValid = true;
    return m_renderCache;
}

QImage AnnotationCanvas::renderShapes(const QVector<Shape> &shapes) const
{
    if (shapes.isEmpty())
    {
        m_effectCache.reset();
        return m_image;
    }
    const annotation::AnnotationRenderer renderer(m_image.size());
    const bool hasEffects = std::any_of(shapes.cbegin(), shapes.cend(), [](const Shape &shape) {
        return shape.tool == Tool::Mosaic || shape.tool == Tool::Blur;
    });
    if (hasEffects && !m_effectCache)
        m_effectCache = std::make_unique<annotation::EffectCache>();
    else if (!hasEffects)
        m_effectCache.reset();
    return renderer.render(m_image, shapes, m_effectCache.get());
}

void AnnotationCanvas::invalidateRenderCache()
{
    if (m_document.shapes().isEmpty())
        m_effectCache.reset();
    m_renderCacheValid = false;
    // Undo/redo retains vector shapes, not this raster. Release the stale
    // frame before rebuilding so it does not overlap with the next full-size
    // result or survive a clear/undo back to an unannotated canvas.
    m_renderCache = {};
    m_previewBackground = {};
    m_previewSourceKey = 0;
}

QImage AnnotationCanvas::renderedImage(bool includePendingText) const
{
    const QImage full = fullRenderedImage(includePendingText);
    const QRect crop = outputPixelRect();
    return crop.isEmpty() ? full : full.copy(crop);
}

void AnnotationCanvas::updateImageRegion(const QRectF &imageRegion)
{
    const QRectF frame = imageRect().translated(-QPointF(m_coordinateOrigin));
    if (m_image.isNull() || frame.isEmpty())
        return;
    if (usesScaledBackground() && !canCacheBackground())
    {
        // Keep memory bounded on very large scaled views. Their background
        // uses Qt's complete scaling path to retain consistent source sampling.
        update();
        return;
    }
    const qreal scale = frame.width() / m_image.width();
    const QRectF local(frame.topLeft() + imageRegion.topLeft() * scale, imageRegion.size() * scale);
    // Include device rounding and the antialiasing fringe at any canvas DPI.
    update(local.adjusted(-2, -2, 2, 2).toAlignedRect().intersected(rect()));
}

bool AnnotationCanvas::usesScaledBackground() const
{
    const qreal pixelsPerSourcePixel = imageRect().width() * devicePixelRatioF() / qMax(1, m_image.width());
    return !qFuzzyCompare(pixelsPerSourcePixel, 1.0);
}

bool AnnotationCanvas::canCacheBackground() const
{
    constexpr qint64 maximumBytes = 16 * 1024 * 1024;
    const qreal dpr = devicePixelRatioF();
    return qint64(qCeil(width() * dpr)) * qCeil(height() * dpr) * 4 <= maximumBytes;
}

void AnnotationCanvas::paintBackground(QPainter &painter, const QImage &display, const QRectF &frame) const
{
    if (!usesScaledBackground() || !canCacheBackground())
    {
        m_previewBackground = {};
        painter.drawImage(frame, display);
        return;
    }
    const qreal dpr = devicePixelRatioF();
    const QSize pixels(qCeil(width() * dpr), qCeil(height() * dpr));
    if (m_previewSourceKey != display.cacheKey() || m_previewFrame != frame ||
        m_previewBackground.size() != pixels || m_previewDpr != dpr)
    {
        m_previewBackground = {}; // Release the previous view before allocating its replacement.
        m_previewBackground = QImage(pixels, QImage::Format_ARGB32_Premultiplied);
        m_previewBackground.setDevicePixelRatio(dpr);
        m_previewBackground.fill(Qt::transparent);
        QPainter background(&m_previewBackground);
        background.drawImage(frame, display);
        background.end();
        m_previewSourceKey = display.cacheKey();
        m_previewFrame = frame;
        m_previewDpr = dpr;
    }
    // Paint at native widget pixels. The source scaling is performed once
    // with the full visible clip, rather than rounded anew for each dirty area.
    painter.drawImage(QPointF(0, 0), m_previewBackground);
}

} // namespace snipaste::ui
