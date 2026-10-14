#include "ui/annotation/image_region.h"

#include <QPainter>
#include <QTransform>

namespace snipaste::ui::annotation {

void paintMagnifiedRegion(QPainter &painter, const QImage &frame,
                          const QRectF &source, const QRectF &target)
{
    const QRectF clippedSource = source.intersected(QRectF(frame.rect()));
    if (clippedSource.isEmpty())
        return;

    // Bilinear sampling can read adjacent pixels. Retain a small halo while
    // avoiding a full-frame deep copy from an actively painted QImage.
    const QRect sampleRect = clippedSource.adjusted(-2, -2, 2, 2)
                                 .toAlignedRect().intersected(frame.rect());
    const QImage pixels = frame.copy(sampleRect);
    QTransform zoom;
    zoom.translate(target.center().x(), target.center().y());
    zoom.scale(target.width() / qMax<qreal>(1.0, source.width()),
               target.height() / qMax<qreal>(1.0, source.height()));
    zoom.translate(-source.center().x(), -source.center().y());

    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setWorldTransform(zoom, true);
    painter.drawImage(QPointF(sampleRect.topLeft()), pixels);
    painter.restore();
}

} // namespace snipaste::ui::annotation
