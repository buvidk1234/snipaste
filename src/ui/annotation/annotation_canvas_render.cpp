#include "ui/annotation/annotation_canvas.h"
#include "ui/annotation/annotation_renderer.h"
#include <QPainter>
#include <QPlainTextEdit>

namespace snipaste::ui {

void AnnotationCanvas::paintEvent(QPaintEvent *)
{
    const QImage display = fullRenderedImage(false);

    QPainter painter(this);
    if (!testAttribute(Qt::WA_TranslucentBackground)) {
        painter.fillRect(rect(), QColor(30, 30, 30));
    }
    const QRectF frame = imageRect();
    const QRectF localFrame = frame.translated(-QPointF(m_coordinateOrigin));
    paintBackground(painter, display, localFrame);
    const qreal scale = frame.width() / qMax(1, m_image.width());
    painter.save();
    painter.translate(localFrame.topLeft());
    painter.scale(scale, scale);
    if (m_dragging) {
        Shape preview = currentShape();
        // Only the filtered crop is transient.  Drawing it on the widget
        // avoids detaching/copying the complete cached desktop every frame.
        if (m_tool == Tool::Mosaic || m_tool == Tool::Blur)
            annotation::AnnotationRenderer(m_image.size()).drawEffect(painter, display, preview);
        // During the first magnifier gesture the UI is a plain rectangular
        // selection of the source pixels.  The larger target frame and
        // enlarged pixels appear after the drag is committed.
        if (m_tool == Tool::Magnifier) {
            const QRectF source = preview.sourceRect.normalized();
            preview.start = source.topLeft();
            preview.end = source.bottomRight();
            preview.sourceRect = {};
        }
        annotation::AnnotationRenderer(m_image.size()).drawShape(painter, preview, 1.0);
    }
    if (!(m_tool == Tool::Eraser && m_disableEraserEditing)
        && m_selectedShape >= 0 && m_selectedShape < m_document.shapes().size()) {
        const Shape &sel = m_document.shapes().at(m_selectedShape);
        QRectF bounds = annotation::shapeBounds(sel);
        painter.save();
        if (!qFuzzyIsNull(sel.rotation)) {
            painter.translate(bounds.center());
            painter.rotate(sel.rotation);
            painter.translate(-bounds.center());
        }
        QPen dashedPen(QColor(255, 255, 255, 240), 1.0, Qt::DashLine);
        painter.setPen(dashedPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(bounds);

        // 8 square resize handles
        const qreal hs = 6.0;
        const QList<QPointF> handles = {
            bounds.topLeft(),
            QPointF(bounds.center().x(), bounds.top()),
            bounds.topRight(),
            QPointF(bounds.right(), bounds.center().y()),
            bounds.bottomRight(),
            QPointF(bounds.center().x(), bounds.bottom()),
            bounds.bottomLeft(),
            QPointF(bounds.left(), bounds.center().y())
        };
        painter.setPen(QPen(QColor(40, 137, 237, 230), 1.0));
        painter.setBrush(Qt::white);
        for (const QPointF &pt : handles) {
            painter.drawRect(QRectF(pt.x() - hs / 2.0, pt.y() - hs / 2.0, hs, hs));
        }

        // Top rotation handle
        const QPointF topCenter(bounds.center().x(), bounds.top());
        const QPointF rotHandleCenter(bounds.center().x(), bounds.top() - 20.0);
        painter.setPen(QPen(QColor(40, 137, 237, 220), 1.5));
        painter.drawLine(topCenter, rotHandleCenter);
        painter.setPen(QPen(QColor(40, 137, 237), 1.5));
        painter.setBrush(Qt::white);
        painter.drawEllipse(rotHandleCenter, 7.0, 7.0);
        painter.setPen(QPen(QColor(40, 137, 237), 1.5));
        painter.drawArc(QRectF(rotHandleCenter.x() - 4.5, rotHandleCenter.y() - 4.5, 9.0, 9.0), 45 * 16, 270 * 16);
        painter.drawLine(rotHandleCenter + QPointF(2, -4), rotHandleCenter + QPointF(4, -2));
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
    if (m_document.shapes().isEmpty() && (!includePendingText || !m_textEditing))
        return m_image;
    const bool cacheable = !includePendingText || !m_textEditing;
    if (cacheable && m_renderCacheValid)
        return m_renderCache;

    const annotation::AnnotationRenderer renderer(m_image.size());
    QImage result = renderer.render(m_image, m_document.shapes());
    if (includePendingText && m_textEditing) {
        Shape pending = m_textShape;
        pending.text = m_textEditor->toPlainText();
        QPainter painter(&result);
        painter.setRenderHint(QPainter::Antialiasing, true);
        renderer.drawShape(painter, pending);
    }
    if (cacheable) {
        m_renderCache = result;
        m_renderCacheValid = true;
    }
    return result;
}

void AnnotationCanvas::invalidateRenderCache()
{
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
    if (usesScaledBackground() && !canCacheBackground()) {
        // Keep memory bounded on very large scaled views. Their background
        // uses Qt's complete scaling path to retain consistent source sampling.
        update();
        return;
    }
    const qreal scale = frame.width() / m_image.width();
    const QRectF local(frame.topLeft() + imageRegion.topLeft() * scale,
                       imageRegion.size() * scale);
    // Include device rounding and the antialiasing fringe at any canvas DPI.
    update(local.adjusted(-2, -2, 2, 2).toAlignedRect().intersected(rect()));
}

bool AnnotationCanvas::usesScaledBackground() const
{
    const qreal pixelsPerSourcePixel = imageRect().width() * devicePixelRatioF()
        / qMax(1, m_image.width());
    return !qFuzzyCompare(pixelsPerSourcePixel, 1.0);
}

bool AnnotationCanvas::canCacheBackground() const
{
    constexpr qint64 maximumBytes = 16 * 1024 * 1024;
    const qreal dpr = devicePixelRatioF();
    return qint64(qCeil(width() * dpr)) * qCeil(height() * dpr) * 4 <= maximumBytes;
}

void AnnotationCanvas::paintBackground(QPainter &painter, const QImage &display,
                                      const QRectF &frame) const
{
    if (!usesScaledBackground() || !canCacheBackground()) {
        m_previewBackground = {};
        painter.drawImage(frame, display);
        return;
    }
    const qreal dpr = devicePixelRatioF();
    const QSize pixels(qCeil(width() * dpr), qCeil(height() * dpr));
    if (m_previewSourceKey != display.cacheKey() || m_previewFrame != frame
        || m_previewBackground.size() != pixels || m_previewDpr != dpr) {
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
