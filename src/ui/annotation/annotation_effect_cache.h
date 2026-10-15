#pragma once

#include "core/annotation/annotation_types.h"
#include <QImage>
#include <QMap>

namespace snipaste::ui::annotation
{

struct EffectPixels
{
    QRect rect;
    QPainterPath mask;
    QImage image;
    qint64 bytes() const;
};

// Effects sample the layers below them. Reuse only the unchanged prefix of
// a render, and bound retained crops rather than caching complete desktops.
class EffectCache
{
  public:
    explicit EffectCache(qint64 maximumBytes = 8 * 1024 * 1024);
    void beginFrame(const QImage &source, const QVector<core::AnnotationShape> &shapes, QSize imageSize);
    const EffectPixels *find(qsizetype index);
    void store(qsizetype index, EffectPixels pixels);
    qint64 bytes() const { return m_bytes; }
    quint64 hits() const { return m_hits; }

  private:
    qint64 m_maximumBytes;
    qint64 m_bytes = 0;
    qint64 m_sourceKey = 0;
    QSize m_imageSize;
    QVector<core::AnnotationShape> m_shapes;
    QMap<qsizetype, EffectPixels> m_effects;
    quint64 m_hits = 0;
};

} // namespace snipaste::ui::annotation
