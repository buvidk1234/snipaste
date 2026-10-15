#pragma once

#include "core/annotation/annotation_types.h"
#include <QImage>
#include <span>

class QPainter;

namespace snipaste::ui::annotation {

using Shape = core::AnnotationShape;
class EffectCache;
QRectF shapeBounds(const Shape &shape);
QRectF magnifierTargetRect(const Shape &shape);
QPainterPath effectMask(const Shape &shape);

// Native-pixel compositing shared by export and live canvas previews.
class AnnotationRenderer
{
public:
    explicit AnnotationRenderer(QSize imageSize) : m_imageSize(imageSize) {}
    QImage render(const QImage &image, const QVector<Shape> &shapes, EffectCache *cache = nullptr) const;
    void drawShape(QPainter &painter, const Shape &shape, qreal scale = 1.0) const;
    // Unrotated, unerased pen/marker points, kept in one compositing operation.
    void drawStroke(QPainter &painter, const Shape &shape,
                    std::span<const QPointF> points, qreal scale = 1.0) const;
    void drawEffect(QPainter &painter, const QImage &source, const Shape &shape) const;

private:
    void drawEffect(QPainter &painter, const QImage &source, const Shape &shape,
                    EffectCache *cache, qsizetype index) const;
    void applyErasedClip(QPainter &painter, const Shape &shape) const;
    QSize m_imageSize;
};

} // namespace snipaste::ui::annotation
