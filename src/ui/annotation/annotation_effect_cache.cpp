#include "ui/annotation/annotation_effect_cache.h"
#include <utility>

namespace snipaste::ui::annotation
{

qint64 EffectPixels::bytes() const
{
    return image.sizeInBytes() + qint64(mask.elementCount()) * sizeof(QPainterPath::Element);
}

EffectCache::EffectCache(qint64 maximumBytes) : m_maximumBytes(qMax<qint64>(0, maximumBytes)) {}

void EffectCache::beginFrame(const QImage &source, const QVector<core::AnnotationShape> &shapes,
                            QSize imageSize)
{
    qsizetype prefix = 0;
    if (m_sourceKey == source.cacheKey() && m_imageSize == imageSize)
    {
        const auto count = qMin(m_shapes.size(), shapes.size());
        while (prefix < count && m_shapes.at(prefix) == shapes.at(prefix))
            ++prefix;
    }
    auto entry = m_effects.lowerBound(prefix);
    while (entry != m_effects.end())
    {
        m_bytes -= entry.value().bytes();
        entry = m_effects.erase(entry);
    }
    m_sourceKey = source.cacheKey();
    m_imageSize = imageSize;
    m_shapes = shapes;
}

const EffectPixels *EffectCache::find(qsizetype index)
{
    const auto entry = m_effects.constFind(index);
    if (entry == m_effects.cend())
        return nullptr;
    ++m_hits;
    return &entry.value();
}

void EffectCache::store(qsizetype index, EffectPixels pixels)
{
    const auto cost = pixels.bytes();
    if (pixels.image.isNull() || cost > m_maximumBytes - m_bytes || m_effects.contains(index))
        return;
    m_bytes += cost;
    m_effects.insert(index, std::move(pixels));
}

} // namespace snipaste::ui::annotation
