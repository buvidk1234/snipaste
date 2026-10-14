#pragma once

#include <QImage>
#include <QRectF>

class QPainter;

namespace snipaste::ui::annotation {

// Sample the composed layers below a magnifier before painting into the same
// frame. The caller controls the destination clip and outline.
void paintMagnifiedRegion(QPainter &painter, const QImage &frame,
                          const QRectF &source, const QRectF &target);

} // namespace snipaste::ui::annotation
