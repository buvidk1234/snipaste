#include "ui/annotation/annotation_toolbar_icons.h"
#include "ui/annotation/annotation_renderer.h"
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
namespace snipaste::ui::annotation
{
namespace
{
QIcon arrowIcon(core::ArrowHead start, core::ArrowHead end, int w, int h, bool dropdown)
{
    QPixmap pix(w, h);
    pix.fill(Qt::transparent);
    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing);
    Shape shape;
    shape.tool = core::AnnotationTool::Arrow;
    shape.arrowStyle = core::ArrowStyle::None;
    shape.startArrowHead = start;
    shape.endArrowHead = end;
    shape.width = 2;
    shape.start = {dropdown ? 5.0 : 8.0, h / 2.0};
    shape.end = {qreal(w - (dropdown ? 18 : 8)), h / 2.0};
    shape.color = QColor(dropdown ? QStringLiteral("#52616b") : QStringLiteral("#333333"));
    // Native menu previews use the same path with a slightly wider head aspect.
    painter.fillPath(arrowPath(shape, 1.0, 0.6), shape.color);
    if (dropdown)
    {
        const qreal midY = h / 2.0;
        painter.setPen(QPen(QColor(QStringLiteral("#666666")), 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPolyline(QPolygonF{{qreal(w - 13), midY - 2}, {qreal(w - 9), midY + 2},
                                       {qreal(w - 5), midY - 2}});
    }
    return QIcon(pix);
}
}

QIcon makeColorSwatchIcon(const QColor &color)
{
    QPixmap pix(22, 22);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QColor(160, 160, 160));
    p.setBrush(color);
    p.drawRect(QRectF(1, 1, 20, 20));
    QPolygonF tri{QPointF(21, 13), QPointF(21, 21), QPointF(13, 21)};
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(237, 28, 36));
    p.drawPolygon(tri);
    QPolygonF tri2{QPointF(21, 16), QPointF(21, 21), QPointF(16, 21)};
    p.setBrush(QColor(255, 242, 0));
    p.drawPolygon(tri2);
    QPolygonF tri3{QPointF(21, 19), QPointF(21, 21), QPointF(19, 21)};
    p.setBrush(QColor(0, 162, 232));
    p.drawPolygon(tri3);
    p.end();
    return QIcon(pix);
}

QIcon makeDynamicWidthIcon(qreal width, bool checked)
{
    QIcon icon;
    for (int size : {20, 24, 32})
    {
        for (const auto state : {QIcon::Off, QIcon::On})
        {
            QPixmap pix(size, size);
            pix.fill(Qt::transparent);
            QPainter p(&pix);
            p.setRenderHint(QPainter::Antialiasing, true);
            const QPointF center(size / 2.0, size / 2.0);
            qreal r;
            if (width <= 2.0)
            {
                r = 0.8 + width * 0.45;
            }
            else if (width <= 8.0)
            {
                r = 1.7 + (width - 2.0) * 0.4;
            }
            else if (width <= 24.0)
            {
                r = 4.1 + (width - 8.0) * 0.22;
            }
            else
            {
                r = qMin(10.0, 7.6 + (width - 24.0) * 0.06);
            }
            const bool isWhite = (state == QIcon::On) || checked;
            const QColor fill = isWhite ? Qt::white : QColor(QStringLiteral("#52616b"));
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
            p.drawEllipse(center, r, r);
            p.end();
            icon.addPixmap(pix, QIcon::Normal, state);
        }
    }
    return icon;
}

QIcon makeLineStyleDropdownIcon(Qt::PenStyle style, int w, int h)
{
    QPixmap pix(w, h);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal midY = h / 2.0;
    const qreal startX = 6;
    const qreal endX = w - 16;

    QPen pen(QColor(QStringLiteral("#52616b")), 2.4);
    applyLineStyle(pen, style);
    p.setPen(pen);
    p.drawLine(QPointF(startX, midY), QPointF(endX, midY));

    p.setPen(QPen(QColor(QStringLiteral("#666666")), 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    QPolygonF chevron{QPointF(w - 12, midY - 2), QPointF(w - 8, midY + 2), QPointF(w - 4, midY - 2)};
    p.drawPolyline(chevron);
    p.end();
    return QIcon(pix);
}

QIcon makeArrowStyleDropdownIcon(core::ArrowStyle style, int w, int h)
{
    return arrowIcon(core::startArrowHead(style), core::endArrowHead(style), w, h, true);
}

QIcon makeCurveStyleDropdownIcon(int curveStyle, int w, int h)
{
    QPixmap pix(w, h);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal midY = h / 2.0;
    p.setPen(QPen(QColor(QStringLiteral("#52616b")), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (curveStyle == 1)
    {
        QPainterPath path;
        path.moveTo(6, midY + 3);
        path.quadTo(18, midY - 6, 30, midY + 3);
        p.drawPath(path);
    }
    else
    {
        p.drawLine(QPointF(6, midY), QPointF(30, midY));
    }

    p.setPen(QPen(QColor(QStringLiteral("#666666")), 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    QPolygonF chevron{QPointF(w - 13, midY - 2), QPointF(w - 9, midY + 2), QPointF(w - 5, midY - 2)};
    p.drawPolyline(chevron);
    p.end();
    return QIcon(pix);
}

QIcon makeLineMenuItemIcon(Qt::PenStyle style, int w, int h)
{
    QPixmap pix(w, h);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor(QStringLiteral("#333333")), 2.4);
    applyLineStyle(pen, style);
    p.setPen(pen);
    p.drawLine(QPointF(4, h / 2.0), QPointF(w - 4, h / 2.0));
    p.end();
    return QIcon(pix);
}

QIcon makeStartArrowDropdownIcon(core::ArrowHead style, int w, int h)
{
    return arrowIcon(style, core::ArrowHead::None, w, h, true);
}

QIcon makeStartArrowMenuItemIcon(core::ArrowHead style, int w, int h)
{
    return arrowIcon(style, core::ArrowHead::None, w, h, false);
}

QIcon makeEndArrowDropdownIcon(core::ArrowHead style, int w, int h)
{
    return arrowIcon(core::ArrowHead::None, style, w, h, true);
}

QIcon makeEndArrowMenuItemIcon(core::ArrowHead style, int w, int h)
{
    return arrowIcon(core::ArrowHead::None, style, w, h, false);
}

QIcon makeToggleArrowIcon(bool active, int w, int h)
{
    QPixmap pix(w, h);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal midY = h / 2.0;
    const QColor color = active ? QColor(QStringLiteral("#2889ed")) : QColor(QStringLiteral("#555555"));
    p.setPen(QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawLine(QPointF(3, midY), QPointF(w - 5, midY));

    QPainterPath path;
    path.moveTo(w - 4, midY);
    path.lineTo(w - 9, midY - 4);
    path.lineTo(w - 9, midY + 4);
    path.closeSubpath();
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawPath(path);
    p.end();
    return QIcon(pix);
}

QIcon makeArrowMenuItemIcon(core::ArrowStyle style, int w, int h)
{
    return arrowIcon(core::startArrowHead(style), core::endArrowHead(style), w, h, false);
}

QIcon makeMagnifierConnectorDropdownIcon(int mode, int w, int h)
{
    QPixmap pix(w, h);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal midY = h / 2.0;
    p.setPen(QPen(QColor(QStringLiteral("#52616b")), 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(5.5, midY - 4.5, 9, 9));

    if (mode == 0)
    {
        p.drawLine(QPointF(14.5, midY), QPointF(28, midY));
    }
    else if (mode == 1)
    {
        p.drawLine(QPointF(14.5, midY), QPointF(21, midY));
        p.drawLine(QPointF(21, midY), QPointF(21, midY + 4));
        p.drawLine(QPointF(21, midY + 4), QPointF(28, midY + 4));
    }

    p.setPen(QPen(QColor(QStringLiteral("#666666")), 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    QPolygonF chevron{QPointF(w - 13, midY - 2), QPointF(w - 9, midY + 2), QPointF(w - 5, midY - 2)};
    p.drawPolyline(chevron);
    p.end();
    return QIcon(pix);
}

QIcon makeMagnifierConnectorMenuItemIcon(int mode, int w, int h)
{
    QPixmap pix(w, h);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal midY = h / 2.0;
    p.setPen(QPen(QColor(QStringLiteral("#52616b")), 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(3.5, midY - 4.5, 9, 9));

    if (mode == 0)
    {
        p.drawLine(QPointF(12.5, midY), QPointF(38, midY));
    }
    else if (mode == 1)
    {
        p.drawLine(QPointF(12.5, midY), QPointF(25, midY));
        p.drawLine(QPointF(25, midY), QPointF(25, midY + 4));
        p.drawLine(QPointF(25, midY + 4), QPointF(38, midY + 4));
    }
    p.end();
    return QIcon(pix);
}

} // namespace snipaste::ui::annotation
