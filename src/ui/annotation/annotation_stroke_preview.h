#pragma once

#include "core/annotation/annotation_types.h"

class QPainter;

namespace snipaste::ui::annotation
{

class AnnotationRenderer;

// A geometry index for the current append-only stroke. No image-sized cache.
class StrokePreview
{
  public:
    void draw(const AnnotationRenderer &renderer, QPainter &painter, const core::AnnotationShape &shape,
              const QRectF &visible);

  private:
    void index(const QVector<QPointF> &points);
    QVector<QRectF> m_chunks;
    qsizetype m_indexedPoints = 0;
    QPointF m_lastIndexed;
};

} // namespace snipaste::ui::annotation
