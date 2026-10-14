#include "ui/annotation/annotation_renderer.h"
#include "ui/annotation/image_region.h"

#include <QFontMetricsF>
#include <QLineF>
#include <QPainter>
#include <QPainterPathStroker>
#include <QTransform>
#include <cmath>
#include <limits>

namespace snipaste::ui::annotation {
using Tool = core::AnnotationTool;
using ArrowStyle = core::ArrowStyle;
using EffectShape = core::EffectShape;

QRectF magnifierTargetRect(const Shape &shape)
{
    if (shape.tool != Tool::Magnifier)
        return QRectF(shape.start, shape.end).normalized();
    // The generated display frame is stored by its two corners.  The source
    // rectangle is kept separately in sourceRect, so hit testing, painting,
    // and sampling all use the same outer/inner geometry after either frame
    // is moved independently.
    return QRectF(shape.start, shape.end).normalized();
}

QRectF shapeBounds(const Shape &shape)
{
    QRectF bounds = QRectF(shape.start, shape.end).normalized();
    if (!shape.sourceRect.isEmpty())
        bounds = bounds.united(shape.sourceRect);
    for (const QPointF &point : shape.points) {
        bounds.setLeft(qMin(bounds.left(), point.x()));
        bounds.setTop(qMin(bounds.top(), point.y()));
        bounds.setRight(qMax(bounds.right(), point.x()));
        bounds.setBottom(qMax(bounds.bottom(), point.y()));
    }
    bounds = bounds.normalized();
    if (shape.tool == Tool::Text) {
        QFont textFont;
        textFont.setFamily(shape.fontFamily.isEmpty()
                               ? QStringLiteral("Microsoft YaHei UI")
                               : shape.fontFamily);
        textFont.setPixelSize(qMax(6, qRound(shape.fontSize)));
        textFont.setBold(shape.textBold);
        textFont.setItalic(shape.textItalic);
        const QFontMetricsF metrics(textFont);
        const QStringList lines = shape.text.split(QChar::LineFeed);
        qreal width = 0.0;
        for (const QString &line : lines)
            width = qMax(width, metrics.horizontalAdvance(line));
        const qreal height = qMax(metrics.lineSpacing(),
                                  metrics.lineSpacing() * qMax(1, lines.size()));
        bounds |= QRectF(shape.start, QSizeF(width + 6.0, height + 4.0));
    } else if (shape.tool == Tool::Number) {
        const qreal diameter = qMax<qreal>(1.0, shape.numberSize);
        bounds |= QRectF(shape.start - QPointF(diameter * 0.5, diameter * 0.5),
                         QSizeF(diameter, diameter));
    }
    bounds = bounds.normalized();
    const qreal margin = qMax<qreal>(6.0, shape.width * 1.5);
    bounds = bounds.adjusted(-margin, -margin, margin, margin);
    if (qFuzzyIsNull(shape.rotation))
        return bounds;
    const QPointF center = bounds.center();
    const QTransform transform = QTransform().translate(center.x(), center.y())
        .rotate(shape.rotation).translate(-center.x(), -center.y());
    const QPolygonF corners({bounds.topLeft(), bounds.topRight(), bounds.bottomRight(), bounds.bottomLeft()});
    return transform.map(corners).boundingRect();
}

void AnnotationRenderer::applyErasedClip(QPainter &painter, const Shape &shape) const
{
    if (shape.erasedPath.isEmpty())
        return;

    // QPainter clips by intersection.  Build the part of the image outside
    // the accumulated eraser path, then intersect it with the caller's
    // existing clip.  The path is in image coordinates and is transformed
    // together with the painter, including rotated/zoomed previews.
    const QSizeF imageSize = m_imageSize.isEmpty()
        ? QSizeF(1.0, 1.0) : QSizeF(m_imageSize);
    QRectF outer(QPointF(-2.0, -2.0), imageSize + QSizeF(4.0, 4.0));
    QPainterPath keep;
    keep.setFillRule(Qt::WindingFill);
    keep.addRect(outer);
    keep = keep.subtracted(shape.erasedPath);
    painter.setClipPath(keep, Qt::IntersectClip);
}

void AnnotationRenderer::drawShape(QPainter &painter, const Shape &shape, qreal scale) const
{
    if (!shape.erasedPath.isEmpty()) {
        painter.save();
        applyErasedClip(painter, shape);
        Shape visible = shape;
        visible.erasedPath.clear();
        drawShape(painter, visible, scale);
        painter.restore();
        return;
    }
    if (!qFuzzyIsNull(shape.rotation)) {
        Shape unrotated = shape;
        unrotated.rotation = 0.0;
        const QPointF center = shapeBounds(unrotated).center();
        painter.save();
        painter.translate(center);
        painter.rotate(shape.rotation);
        painter.translate(-center);
        drawShape(painter, unrotated, scale);
        painter.restore();
        return;
    }
    QColor color = shape.color;
    if (shape.tool == Tool::Marker)
        color.setAlpha(190);
    QPen pen(color);
    pen.setWidthF(qMax<qreal>(1.0, shape.width * scale));
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    pen.setStyle(shape.penStyle);
    painter.setPen(pen);

    if (shape.tool == Tool::Mosaic || shape.tool == Tool::Blur) {
        const bool mosaic = shape.tool == Tool::Mosaic;
        pen.setStyle(mosaic ? Qt::DashLine : Qt::DotLine);
        pen.setColor(mosaic ? QColor(90, 90, 90, 220) : QColor(120, 120, 120, 190));
        // This is only the selection hint.  Its thickness must not follow
        // the mosaic brush width; doing so paints a wide grey halo around a
        // correctly sized effect and makes the result look oversized.
        pen.setWidthF(qMax<qreal>(1.0, scale));
        painter.setPen(pen);
        // The effect itself has already been rendered underneath.  Keep this
        // layer as a border/selection hint only; filling the shape here makes
        // a rectangle or ellipse look like a large grey overlay and hides the
        // live mosaic preview.
        painter.setBrush(Qt::NoBrush);
        if (shape.effectShape == EffectShape::Rectangle) {
            painter.drawRect(QRectF(shape.start, shape.end).normalized());
        } else if (shape.effectShape == EffectShape::Ellipse) {
            painter.drawEllipse(QRectF(shape.start, shape.end).normalized());
        } else {
            // Brush width controls the footprint. Effect size controls only
            // the pixel block/blur kernel, so the controls stay independent.
            const qreal radius = qMax<qreal>(2.0, shape.width * 0.5 * scale);
            const QVector<QPointF> points = shape.points.isEmpty()
                ? QVector<QPointF>{shape.start, shape.end} : shape.points;
            QPainterPath centerline;
            if (!points.isEmpty()) {
                centerline.moveTo(points.first());
                for (qsizetype index = 1; index < points.size(); ++index)
                    centerline.lineTo(points.at(index));
            }
            QPainterPathStroker stroker;
            stroker.setWidth(radius * 2.0);
            stroker.setCapStyle(Qt::RoundCap);
            stroker.setJoinStyle(Qt::RoundJoin);
            const QPainterPath footprint = centerline.isEmpty()
                ? QPainterPath() : stroker.createStroke(centerline);
            if (!footprint.isEmpty())
                painter.drawPath(footprint);
            else if (!points.isEmpty())
                painter.drawEllipse(points.first(), radius, radius);
        }
        return;
    }

    if (shape.tool == Tool::Eraser)
        return;

    if (shape.tool == Tool::Rectangle) {
        if (shape.filled)
            painter.setBrush(color);
        painter.drawRect(QRectF(shape.start, shape.end).normalized());
        return;
    }
    if (shape.tool == Tool::RoundedRectangle) {
        if (shape.filled)
            painter.setBrush(color);
        const qreal r = shape.cornerRadius > 0 ? shape.cornerRadius * scale : 12.0 * scale;
        painter.drawRoundedRect(QRectF(shape.start, shape.end).normalized(), r, r);
        return;
    }
    if (shape.tool == Tool::Magnifier) {
        const QRectF area = magnifierTargetRect(shape);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(pen);
        if (shape.effectShape == EffectShape::Ellipse) {
            painter.drawEllipse(area);
        } else if (shape.cornerRadius > 0.0) {
            const qreal r = shape.cornerRadius * scale;
            painter.drawRoundedRect(area, r, r);
        } else {
            painter.drawRect(area);
        }
        if (shape.magnifierCutout && !shape.sourceRect.isEmpty()) {
            const QRectF source = shape.sourceRect.normalized();
            QPen sourcePen = pen;
            // The source frame becomes a white dashed frame while it overlaps
            // the target.  When detached, keep it solid and stop the
            // connector exactly at the target border.
            const bool overlaps = source.intersects(area);
            sourcePen.setStyle(overlaps ? Qt::DashLine : Qt::SolidLine);
            sourcePen.setColor(Qt::white);
            sourcePen.setWidthF(qMax<qreal>(1.0, scale));
            painter.setPen(sourcePen);
            if (shape.effectShape == EffectShape::Ellipse) {
                painter.drawEllipse(source);
            } else if (shape.cornerRadius > 0.0) {
                const qreal r = shape.cornerRadius * scale;
                painter.drawRoundedRect(source, r, r);
            } else {
                painter.drawRect(source);
            }
            if (!overlaps && shape.magnifierConnectorStyle != 2) {
                QPen connectorPen = sourcePen;
                connectorPen.setStyle(Qt::SolidLine);
                connectorPen.setWidthF(qMax<qreal>(1.0, shape.width * scale * 0.85));
                painter.setPen(connectorPen);
                const QPointF sourceCenter = source.center();
                const QPointF targetCenter = area.center();
                const QPointF delta = targetCenter - sourceCenter;
                const qreal distance = std::hypot(delta.x(), delta.y());
                if (distance > 0.001) {
                    const QPointF unit = delta / distance;
                    const auto boundaryAlong = [](const QRectF &rect,
                                                  const QPointF &center,
                                                  const QPointF &direction) {
                        qreal t = std::numeric_limits<qreal>::max();
                        if (!qFuzzyIsNull(direction.x())) {
                            const qreal x = direction.x() > 0.0 ? rect.right() : rect.left();
                            t = qMin(t, (x - center.x()) / direction.x());
                        }
                        if (!qFuzzyIsNull(direction.y())) {
                            const qreal y = direction.y() > 0.0 ? rect.bottom() : rect.top();
                            t = qMin(t, (y - center.y()) / direction.y());
                        }
                        return t > 0.0 && std::isfinite(t) ? center + direction * t : center;
                    };
                    QPointF from = boundaryAlong(source, sourceCenter, unit);
                    QPointF to = boundaryAlong(area, targetCenter, -unit);
                    // Inset both endpoints by half the stroke width so
                    // antialiasing cannot paint into either rectangle.
                    const qreal inset = connectorPen.widthF() * 0.5 + 0.5;
                    from += unit * inset;
                    to -= unit * inset;
                    if (QLineF(from, to).length() > 0.001) {
                        if (shape.magnifierConnectorStyle == 1) {
                            const QPointF mid(from.x(), to.y());
                            const QVector<QPointF> pts{from, mid, to};
                            painter.drawPolyline(pts.constData(), pts.size());
                        } else {
                            painter.drawLine(from, to);
                        }
                    }
                }
            }
        }
        return;
    }
    if (shape.tool == Tool::Ellipse) {
        if (shape.filled)
            painter.setBrush(color);
        painter.drawEllipse(QRectF(shape.start, shape.end).normalized());
        return;
    }
    if (shape.tool == Tool::Line) {
        painter.drawLine(shape.start, shape.end);
        return;
    }
    if (shape.tool == Tool::Pen || shape.tool == Tool::Marker) {
        if (shape.points.size() > 1)
            painter.drawPolyline(shape.points.constData(), shape.points.size());
        else if (shape.points.size() == 1) {
            const qreal radius = qMax<qreal>(0.5, shape.width * scale / 2.0);
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            painter.drawEllipse(shape.points.constFirst(), radius, radius);
        }
        return;
    }
    if (shape.tool == Tool::Arrow) {
        const QPointF delta = shape.end - shape.start;
        const qreal length = std::hypot(delta.x(), delta.y());
        painter.drawLine(shape.start, shape.end);
        if (length > 1.0) {
            const QPointF unit = delta / length;
            const QPointF normal(-unit.y(), unit.x());
            const qreal head = 14.0 * scale;
            const auto drawHead = [&](const QPointF &tip, const QPointF &direction, bool hollow) {
                const QPointF side(-direction.y(), direction.x());
                const QPointF left = tip - direction * head + side * head * 0.45;
                const QPointF right = tip - direction * head - side * head * 0.45;
                if (hollow) {
                    painter.setBrush(Qt::NoBrush);
                    painter.drawPolygon(QPolygonF{tip, left, right});
                } else {
                    painter.setBrush(color);
                    painter.drawPolygon(QPolygonF{tip, left, right});
                    painter.setBrush(Qt::NoBrush);
                }
            };
            const bool startHead = shape.arrowStyle == ArrowStyle::Start
                || shape.arrowStyle == ArrowStyle::Both
                || shape.arrowStyle == ArrowStyle::HollowBoth
                || shape.arrowStyle == ArrowStyle::HollowStart;
            const bool endHead = shape.arrowStyle == ArrowStyle::End
                || shape.arrowStyle == ArrowStyle::Both
                || shape.arrowStyle == ArrowStyle::Flat
                || shape.arrowStyle == ArrowStyle::HollowEnd
                || shape.arrowStyle == ArrowStyle::HollowBoth;
            const bool hollowStart = shape.arrowStyle == ArrowStyle::HollowStart
                || shape.arrowStyle == ArrowStyle::HollowBoth;
            const bool hollowEnd = shape.arrowStyle == ArrowStyle::HollowEnd
                || shape.arrowStyle == ArrowStyle::HollowBoth;
            if (startHead)
                drawHead(shape.start, -unit, hollowStart);
            if (endHead)
                drawHead(shape.end, unit, hollowEnd);
        }
        return;
    }
    QFont font = painter.font();
    font.setFamily(shape.fontFamily.isEmpty() ? QStringLiteral("Microsoft YaHei UI")
                                             : shape.fontFamily);
    font.setPixelSize(qMax(6, qRound(shape.fontSize * scale)));
    font.setBold(shape.textBold);
    font.setItalic(shape.textItalic);
    painter.setFont(font);
    if (shape.tool == Tool::Number) {
        font.setPixelSize(qMax(8, qRound(shape.numberSize * 0.52 * scale)));
        font.setBold(true);
        painter.setFont(font);
        const qreal diameter = shape.numberSize * scale;
        const QRectF badge(shape.start.x() - diameter / 2.0,
                           shape.start.y() - diameter / 2.0,
                           diameter, diameter);
        if (shape.numberStyle == 0) {
            painter.setBrush(shape.color);
            painter.setPen(Qt::NoPen);
            painter.drawEllipse(badge);
            painter.setPen(Qt::white);
        } else if (shape.numberStyle == 1) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(shape.color, qMax<qreal>(1.5, 1.5 * scale)));
            painter.drawEllipse(badge);
            painter.drawEllipse(badge.adjusted(2.5 * scale, 2.5 * scale, -2.5 * scale, -2.5 * scale));
            painter.setPen(shape.color);
        } else {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(shape.color, qMax<qreal>(1.5, 1.5 * scale)));
            painter.drawEllipse(badge);
            painter.setPen(shape.color);
        }
        painter.drawText(badge, Qt::AlignCenter, shape.text);
        return;
    }
    const QFontMetricsF metrics(font);
    const QStringList lines = shape.text.split(QChar::LineFeed);
    for (qsizetype index = 0; index < lines.size(); ++index) {
        const QPointF top = shape.start + QPointF(0, index * metrics.lineSpacing());
        if (shape.textBackground) {
            painter.fillRect(QRectF(top, QSizeF(metrics.horizontalAdvance(lines.at(index)) + 4,
                                               metrics.lineSpacing())),
                             QColor(color.red(), color.green(), color.blue(), 80));
        }
        const QPointF baseline = top + QPointF(0, metrics.ascent());
        if (shape.textOutlineWidth > 0.0 && shape.textOutlineColor.alpha() > 0) {
            QPainterPath textPath;
            textPath.addText(baseline, font, lines.at(index));
            painter.fillPath(textPath, color);
            QPen outline(shape.textOutlineColor);
            outline.setWidthF(shape.textOutlineWidth * scale);
            outline.setJoinStyle(Qt::RoundJoin);
            painter.strokePath(textPath, outline);
        } else {
            painter.drawText(baseline, lines.at(index));
        }
    }
}

QPainterPath effectMask(const Shape &shape)
{
    QPainterPath mask;
    mask.setFillRule(Qt::WindingFill);
    if (shape.effectShape == EffectShape::Rectangle) {
        mask.addRect(QRectF(shape.start, shape.end).normalized());
        return mask;
    }
    if (shape.effectShape == EffectShape::Ellipse) {
        mask.addEllipse(QRectF(shape.start, shape.end).normalized());
        return mask;
    }

    const QVector<QPointF> points = shape.points.isEmpty()
        ? QVector<QPointF>{shape.start, shape.end} : shape.points;
    const qreal radius = qMax<qreal>(0.5, shape.width * 0.5);
    if (points.isEmpty())
        return mask;

    // Use one stroked path for the freehand mode.  A union of independent
    // circles leaves visible gaps whenever Windows delivers sparse mouse
    // move events, and those gaps make the mosaic look like a chain of large
    // grey dots.  A round-capped stroke keeps the footprint equal to the
    // configured brush width and remains continuous between events.
    QPainterPath stroke;
    stroke.moveTo(points.first());
    bool hasSegment = false;
    for (qsizetype index = 1; index < points.size(); ++index) {
        const QPointF point = points.at(index);
        if (QLineF(stroke.currentPosition(), point).length() <= 0.001)
            continue;
        stroke.lineTo(point);
        hasSegment = true;
    }
    if (!hasSegment) {
        mask.addEllipse(QRectF(points.first() - QPointF(radius, radius),
                               QSizeF(radius * 2.0, radius * 2.0)));
        return mask;
    }

    QPainterPathStroker stroker;
    stroker.setWidth(radius * 2.0);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    return stroker.createStroke(stroke);
}

void AnnotationRenderer::drawEffect(QPainter &painter,
                                  const QImage &sourceFrame,
                                  const Shape &shape) const
{
    if ((shape.tool != Tool::Mosaic && shape.tool != Tool::Blur)
        || sourceFrame.isNull())
        return;

    const QPainterPath mask = effectMask(shape);
    const QRect effectRect = mask.boundingRect().toAlignedRect()
        .intersected(sourceFrame.rect());
    if (effectRect.isEmpty())
        return;

    // Both effects use the same selected pixels and the same shape mask.  The
    // only difference is the filter applied to the cropped source image.
    const QImage source = sourceFrame.copy(effectRect);
    const int blockSize = qBound(2, shape.effectSize, 40);
    const QSize reducedSize(qMax(1, qCeil(qreal(effectRect.width()) / blockSize)),
                            qMax(1, qCeil(qreal(effectRect.height()) / blockSize)));

    QImage filtered;
    if (shape.tool == Tool::Mosaic) {
        // Pixelate on a stable grid.  Each cell receives the mean source
        // colour, which gives a predictable block instead of a post-rendered
        // blur and works identically for brush, rectangle, and ellipse masks.
        filtered = QImage(effectRect.size(), QImage::Format_ARGB32_Premultiplied);
        filtered.fill(Qt::transparent);
        QPainter mosaicPainter(&filtered);
        mosaicPainter.setRenderHint(QPainter::Antialiasing, false);

        // Blocks do not overlap: each source pixel contributes exactly once.
        // Direct sums therefore remain O(width * height) while avoiding four
        // 64-bit integral tables (over 250 MiB for a full 4K selection).
        const int sourceWidth = source.width();
        const int sourceHeight = source.height();
        for (int y = 0; y < source.height(); y += blockSize) {
            for (int x = 0; x < source.width(); x += blockSize) {
                const int right = qMin(x + blockSize, sourceWidth);
                const int bottom = qMin(y + blockSize, sourceHeight);
                quint32 sumRed = 0, sumGreen = 0, sumBlue = 0, sumAlpha = 0;
                for (int row = y; row < bottom; ++row) {
                    const auto *pixels = reinterpret_cast<const QRgb *>(source.constScanLine(row));
                    for (int column = x; column < right; ++column) {
                        const QRgb pixel = pixels[column];
                        sumRed += qRed(pixel);
                        sumGreen += qGreen(pixel);
                        sumBlue += qBlue(pixel);
                        sumAlpha += qAlpha(pixel);
                    }
                }
                const quint32 count = quint32(right - x) * quint32(bottom - y);
                const int red = int(sumRed / count);
                const int green = int(sumGreen / count);
                const int blue = int(sumBlue / count);
                const int alpha = int(sumAlpha / count);
                const QRect cell(x, y, right - x, bottom - y);
                mosaicPainter.fillRect(cell,
                                       QColor(red, green, blue, alpha));
            }
        }
        mosaicPainter.end();
    } else {
        const QImage reduced = source.scaled(reducedSize,
                                             Qt::IgnoreAspectRatio,
                                             Qt::SmoothTransformation);
        filtered = reduced.scaled(effectRect.size(),
                                  Qt::IgnoreAspectRatio,
                                  Qt::SmoothTransformation);
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    applyErasedClip(painter, shape);
    painter.setClipPath(mask, Qt::IntersectClip);
    painter.drawImage(effectRect.topLeft(), filtered);
    painter.restore();
}

QImage AnnotationRenderer::render(const QImage &image, const QVector<Shape> &shapes) const
{
    QImage result = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (const Shape &shape : shapes) {
        if (shape.tool == Tool::Magnifier) {
            const QRectF target = magnifierTargetRect(shape);
            if (target.width() < 2.0 || target.height() < 2.0)
                continue;
            QRectF source = shape.sourceRect;
            if (source.isEmpty()) {
                source = QRectF(target.center().x() - target.width() / 4.0,
                                target.center().y() - target.height() / 4.0,
                                target.width() / 2.0,
                                target.height() / 2.0);
            }
            const QRectF unclippedSource = source;
            source = source.intersected(QRectF(result.rect()));
            if (source.isEmpty())
                continue;
            painter.save();
            applyErasedClip(painter, shape);
            if (shape.effectShape == EffectShape::Ellipse) {
                QPainterPath clip;
                clip.addEllipse(target);
                painter.setClipPath(clip, Qt::IntersectClip);
            } else if (shape.cornerRadius > 0.0) {
                QPainterPath clip;
                clip.addRoundedRect(target, shape.cornerRadius, shape.cornerRadius);
                painter.setClipPath(clip, Qt::IntersectClip);
            } else {
                painter.setClipRect(target, Qt::IntersectClip);
            }
            // Map the source centre onto the target centre.  Drawing the
            // clipped source directly through this transform avoids the
            // top-left anchoring and stretching that occurred when a source
            // rectangle touched the image boundary.
            annotation::paintMagnifiedRegion(painter, result, unclippedSource, target);
            painter.restore();
            Shape outline = shape;
            outline.sourceRect = source;
            drawShape(painter, outline, 1.0);
            continue;
        }
        if (shape.tool == Tool::Mosaic || shape.tool == Tool::Blur) {
            // drawEffect snapshots its affected rectangle before drawing.
            // Passing the image by reference avoids copying the whole desktop
            // just to produce that small crop from an active paint device.
            drawEffect(painter, result, shape);
            continue;
        }
        drawShape(painter, shape, 1.0);
    }
    painter.end();
    return result;
}

} // namespace snipaste::ui::annotation
