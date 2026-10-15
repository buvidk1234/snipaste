#include "ui/annotation/annotation_toolbar_icons.h"
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
namespace snipaste::ui::annotation
{
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

    QPen pen(QColor(QStringLiteral("#52616b")), 2.5);
    if (style == Qt::DashLine)
    {
        pen.setStyle(Qt::CustomDashLine);
        pen.setDashPattern({4, 3});
    }
    else if (style == Qt::DotLine)
    {
        pen.setStyle(Qt::CustomDashLine);
        pen.setDashPattern({1.5, 2.5});
    }
    else if (style == Qt::DashDotLine)
    {
        pen.setStyle(Qt::CustomDashLine);
        pen.setDashPattern({4, 2.5, 1.5, 2.5});
    }
    else
    {
        pen.setStyle(Qt::SolidLine);
    }
    p.setPen(pen);
    p.drawLine(QPointF(6, h / 2.0), QPointF(32, h / 2.0));

    p.setPen(QPen(QColor(QStringLiteral("#666666")), 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    QPolygonF chevron{QPointF(w - 14, h / 2.0 - 2), QPointF(w - 10, h / 2.0 + 2),
                      QPointF(w - 6, h / 2.0 - 2)};
    p.drawPolyline(chevron);
    p.end();
    return QIcon(pix);
}

QIcon makeArrowStyleDropdownIcon(core::ArrowStyle style, int w, int h)
{
    QPixmap pix(w, h);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal midY = h / 2.0;
    const qreal startX = 6;
    const qreal endX = 32;

    p.setPen(QPen(QColor(QStringLiteral("#52616b")), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawLine(QPointF(startX, midY), QPointF(endX, midY));

    const auto drawHead = [&](const QPointF &tip, qreal dir, bool hollow) {
        QPainterPath path;
        path.moveTo(tip);
        path.lineTo(tip.x() - dir * 6, tip.y() - 4);
        path.lineTo(tip.x() - dir * 6, tip.y() + 4);
        path.closeSubpath();
        if (hollow)
        {
            p.setPen(QPen(QColor(QStringLiteral("#52616b")), 1.5));
            p.setBrush(Qt::white);
        }
        else
        {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(QStringLiteral("#52616b")));
        }
        p.drawPath(path);
    };

    switch (style)
    {
    case core::ArrowStyle::None:
        break;
    case core::ArrowStyle::Flat:
        p.setPen(QPen(QColor(QStringLiteral("#52616b")), 2.0));
        p.drawLine(QPointF(endX, midY - 4), QPointF(endX, midY + 4));
        break;
    case core::ArrowStyle::End:
        drawHead(QPointF(endX, midY), 1.0, false);
        break;
    case core::ArrowStyle::Start:
        drawHead(QPointF(startX, midY), -1.0, false);
        break;
    case core::ArrowStyle::Both:
        drawHead(QPointF(startX, midY), -1.0, false);
        drawHead(QPointF(endX, midY), 1.0, false);
        break;
    case core::ArrowStyle::HollowEnd:
        drawHead(QPointF(endX, midY), 1.0, true);
        break;
    case core::ArrowStyle::HollowStart:
        drawHead(QPointF(startX, midY), -1.0, true);
        break;
    case core::ArrowStyle::HollowBoth:
        drawHead(QPointF(startX, midY), -1.0, true);
        drawHead(QPointF(endX, midY), 1.0, true);
        break;
    }

    p.setPen(QPen(QColor(QStringLiteral("#666666")), 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    QPolygonF chevron{QPointF(w - 14, midY - 2), QPointF(w - 10, midY + 2), QPointF(w - 6, midY - 2)};
    p.drawPolyline(chevron);
    p.end();
    return QIcon(pix);
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
    QPen pen(QColor(QStringLiteral("#333333")), 2.5);
    if (style == Qt::DashLine)
    {
        pen.setStyle(Qt::CustomDashLine);
        pen.setDashPattern({4, 3});
    }
    else if (style == Qt::DotLine)
    {
        pen.setStyle(Qt::CustomDashLine);
        pen.setDashPattern({1.5, 2.5});
    }
    else if (style == Qt::DashDotLine)
    {
        pen.setStyle(Qt::CustomDashLine);
        pen.setDashPattern({4, 2.5, 1.5, 2.5});
    }
    else
    {
        pen.setStyle(Qt::SolidLine);
    }
    p.setPen(pen);
    p.drawLine(QPointF(6, h / 2.0), QPointF(w - 6, h / 2.0));
    p.end();
    return QIcon(pix);
}

QIcon makeArrowMenuItemIcon(core::ArrowStyle style, int w, int h)
{
    QPixmap pix(w, h);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal midY = h / 2.0;
    const qreal startX = 6;
    const qreal endX = w - 6;

    p.setPen(QPen(QColor(QStringLiteral("#333333")), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawLine(QPointF(startX, midY), QPointF(endX, midY));

    const auto drawHead = [&](const QPointF &tip, qreal dir, bool hollow) {
        QPainterPath path;
        path.moveTo(tip);
        path.lineTo(tip.x() - dir * 6, tip.y() - 4);
        path.lineTo(tip.x() - dir * 6, tip.y() + 4);
        path.closeSubpath();
        if (hollow)
        {
            p.setPen(QPen(QColor(QStringLiteral("#333333")), 1.5));
            p.setBrush(Qt::white);
        }
        else
        {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(QStringLiteral("#333333")));
        }
        p.drawPath(path);
    };

    switch (style)
    {
    case core::ArrowStyle::None:
        break;
    case core::ArrowStyle::Flat:
        p.setPen(QPen(QColor(QStringLiteral("#333333")), 2.0));
        p.drawLine(QPointF(endX, midY - 4), QPointF(endX, midY + 4));
        break;
    case core::ArrowStyle::End:
        drawHead(QPointF(endX, midY), 1.0, false);
        break;
    case core::ArrowStyle::Start:
        drawHead(QPointF(startX, midY), -1.0, false);
        break;
    case core::ArrowStyle::Both:
        drawHead(QPointF(startX, midY), -1.0, false);
        drawHead(QPointF(endX, midY), 1.0, false);
        break;
    case core::ArrowStyle::HollowEnd:
        drawHead(QPointF(endX, midY), 1.0, true);
        break;
    case core::ArrowStyle::HollowStart:
        drawHead(QPointF(startX, midY), -1.0, true);
        break;
    case core::ArrowStyle::HollowBoth:
        drawHead(QPointF(startX, midY), -1.0, true);
        drawHead(QPointF(endX, midY), 1.0, true);
        break;
    }
    p.end();
    return QIcon(pix);
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
