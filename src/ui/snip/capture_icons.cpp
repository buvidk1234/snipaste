#include "ui/snip/capture_icons.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QHash>

namespace snipaste::ui {

QIcon captureIcon(CaptureIcon kind, bool light)
{
    static QHash<int, QIcon> cache;
    const int key = int(kind) * 2 + int(light);
    const auto cached = cache.constFind(key);
    if (cached != cache.cend())
        return cached.value();
    QIcon icon;
    for (int size : {16, 20, 24, 32, 40, 48}) {
      for (const auto mode : {QIcon::Normal, QIcon::Disabled}) {
       for (const auto state : {QIcon::Off, QIcon::On}) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(size / 20.0, size / 20.0);
        const QColor ink = mode == QIcon::Disabled ? QColor("#999999")
            : state == QIcon::On || light ? QColor(Qt::white) : QColor("#303236");
        p.setPen(QPen(ink, 1.25, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
        p.setBrush(Qt::NoBrush);
        switch (kind) {
        case CaptureIcon::Shapes:
            p.drawRect(QRectF(1.5, 3, 12, 10));
            p.setBrush(state == QIcon::On ? QColor("#2889ed") : QColor("#fafafa"));
            p.drawEllipse(QRectF(8, 9, 11, 8));
            break;
        case CaptureIcon::Number: {
            p.setPen(Qt::NoPen);
            p.setBrush(ink);
            p.drawEllipse(QRectF(2, 2, 16, 16));
            p.setPen(state == QIcon::On ? QColor("#2889ed") : Qt::white);
            QFont font = p.font(); font.setPixelSize(12); p.setFont(font);
            p.drawText(QRectF(2, 1, 16, 17), Qt::AlignCenter, QStringLiteral("1"));
            break;
        }
        case CaptureIcon::Magnifier:
            p.drawEllipse(QRectF(2, 2, 10, 10));
            p.drawLine(QPointF(10.5, 10.5), QPointF(18, 18));
            p.drawArc(QRectF(4, 4, 6, 6), 30 * 16, 110 * 16);
            break;
        case CaptureIcon::Ocr: {
            p.drawPolyline(QPolygonF{QPointF(5, 1), QPointF(1, 1), QPointF(1, 19), QPointF(5, 19)});
            p.drawPolyline(QPolygonF{QPointF(15, 1), QPointF(19, 1), QPointF(19, 19), QPointF(15, 19)});
            QFont font = p.font(); font.setPixelSize(17); p.setFont(font);
            p.drawText(QRectF(3, 0, 14, 20), Qt::AlignCenter, QStringLiteral("A"));
            break;
        }
        case CaptureIcon::Ai: {
            QFont font = p.font(); font.setPixelSize(12); font.setBold(true); p.setFont(font);
            p.drawText(QRectF(0, 1, 20, 18), Qt::AlignCenter, QStringLiteral("AI"));
            break;
        }
        case CaptureIcon::Share:
            p.drawLine(QPointF(4, 10), QPointF(15, 4));
            p.drawLine(QPointF(4, 10), QPointF(15, 16));
            p.setBrush(ink);
            for (const QPointF point : {QPointF(4, 10), QPointF(15, 4), QPointF(15, 16)})
                p.drawEllipse(point, 2, 2);
            break;
        case CaptureIcon::More:
            p.setBrush(ink); p.setPen(Qt::NoPen);
            for (int y : {4, 10, 16}) p.drawEllipse(QPointF(10, y), 1.4, 1.4);
            break;
        case CaptureIcon::RoundedCorners: {
            QPainterPath corner;
            corner.moveTo(2, 18); corner.lineTo(2, 6);
            corner.quadTo(2, 2, 6, 2); corner.lineTo(18, 2);
            p.drawPath(corner);
            break;
        }
        case CaptureIcon::StraightCorners: {
            QPainterPath corner;
            corner.moveTo(2, 18); corner.lineTo(2, 2); corner.lineTo(18, 2);
            p.drawPath(corner);
            break;
        }
        case CaptureIcon::SelectScreen:
            p.drawRect(QRectF(1, 2, 18, 15));
            p.drawRect(QRectF(4, 5, 12, 9));
            break;
        case CaptureIcon::Refresh:
            p.drawArc(QRectF(2, 2, 16, 16), 30 * 16, 145 * 16);
            p.drawArc(QRectF(2, 2, 16, 16), 210 * 16, 145 * 16);
            p.setBrush(ink);
            p.drawPolygon(QPolygonF{QPointF(17, 1), QPointF(18, 7), QPointF(12, 6)});
            p.drawPolygon(QPolygonF{QPointF(3, 19), QPointF(2, 13), QPointF(8, 14)});
            break;
        case CaptureIcon::Scroll:
            p.drawLine(QPointF(10, 2), QPointF(10, 18));
            p.drawPolyline(QPolygonF{QPointF(6, 6), QPointF(10, 2), QPointF(14, 6)});
            p.drawPolyline(QPolygonF{QPointF(6, 14), QPointF(10, 18), QPointF(14, 14)});
            p.drawRect(QRectF(4, 7, 12, 6));
            break;
        case CaptureIcon::Rectangle: {
            p.drawRect(QRectF(2.5, 4.0, 14, 10.5));
            p.setBrush(ink);
            p.setPen(Qt::NoPen);
            p.drawPolygon(QPolygonF{QPointF(13.5, 15.0), QPointF(17.5, 15.0), QPointF(15.5, 17.5)});
            break;
        }
        case CaptureIcon::Ellipse: p.drawEllipse(QRectF(2, 4.5, 16, 11)); break;
        case CaptureIcon::Line:
            p.drawPolyline(QPolygonF{QPointF(2, 14), QPointF(7, 9), QPointF(11, 12), QPointF(18, 5)});
            break;
        case CaptureIcon::Arrow:
            p.drawLine(QPointF(3, 17), QPointF(16, 3));
            p.setBrush(ink);
            p.drawPolygon(QPolygonF{QPointF(10, 5), QPointF(16, 3), QPointF(14, 9), QPointF(13, 6)});
            break;
        case CaptureIcon::Pen:
            p.translate(10, 10);
            p.rotate(40);
            p.drawRect(QRectF(-2, -8, 4, 12));
            p.drawPolyline(QPolygonF{QPointF(-2, 4), QPointF(0, 8), QPointF(2, 4)});
            p.drawLine(QPointF(-2, -5), QPointF(2, -5));
            break;
        case CaptureIcon::Marker:
            p.drawPolygon(QPolygonF{QPointF(5, 12), QPointF(10, 5), QPointF(16, 11), QPointF(9, 16)});
            p.drawLine(QPointF(9, 4), QPointF(17, 12));
            p.drawLine(QPointF(11, 2), QPointF(19, 10));
            p.setBrush(ink);
            p.drawPolygon(QPolygonF{QPointF(3, 13), QPointF(7, 16), QPointF(3, 18), QPointF(1, 16)});
            break;
        case CaptureIcon::Mosaic:
            p.setPen(Qt::NoPen);
            for (int y = 0; y < 3; ++y)
                for (int x = 0; x < 3; ++x) {
                    p.setBrush((x + y) % 2 ? QColor("#eeeeee") : ink);
                    p.drawRect(QRectF(2 + x * 5, 2 + y * 5, 5, 5));
                }
            break;
        case CaptureIcon::Blur:
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(ink.red(), ink.green(), ink.blue(), 90));
            p.drawEllipse(QRectF(2, 2, 16, 16));
            p.setBrush(QColor(ink.red(), ink.green(), ink.blue(), 150));
            p.drawEllipse(QRectF(6, 6, 8, 8));
            break;
        case CaptureIcon::Text:
            p.drawLine(QPointF(10, 3), QPointF(10, 17));
            p.drawLine(QPointF(3, 3), QPointF(17, 3));
            break;
        case CaptureIcon::Eraser:
            p.drawPolygon(QPolygonF{QPointF(2, 12), QPointF(11, 3), QPointF(17, 9),
                                    QPointF(9, 17), QPointF(7, 17)});
            p.drawLine(QPointF(6, 8), QPointF(12, 14));
            p.setBrush(ink);
            p.drawPolygon(QPolygonF{QPointF(6, 8), QPointF(11, 3), QPointF(17, 9), QPointF(12, 14)});
            break;
        case CaptureIcon::Undo:
        case CaptureIcon::Redo: {
            if (kind == CaptureIcon::Redo) { p.translate(20, 0); p.scale(-1, 1); }
            QPainterPath path;
            path.moveTo(4, 7);
            path.cubicTo(13, 3, 18, 10, 14, 17);
            p.setPen(QPen(ink, 2));
            p.drawPath(path);
            p.setBrush(ink);
            p.drawPolygon(QPolygonF{QPointF(2, 7), QPointF(7, 2), QPointF(7, 12)});
            break;
        }
        case CaptureIcon::Save:
            p.setBrush(ink);
            p.drawRect(QRectF(3, 2, 14, 16));
            p.setBrush(state == QIcon::On ? QColor("#2889ed") : Qt::white);
            p.drawRect(QRectF(6, 2, 8, 6));
            p.drawRect(QRectF(6, 12, 8, 6));
            p.drawLine(QPointF(12, 3), QPointF(12, 6));
            break;
        case CaptureIcon::Paste:
            p.translate(10, 10);
            p.rotate(42);
            p.setBrush(ink);
            p.drawPolygon(QPolygonF{QPointF(-4, -7), QPointF(4, -7), QPointF(3, -5),
                QPointF(3, 1), QPointF(6, 4), QPointF(-6, 4), QPointF(-3, 1), QPointF(-3, -5)});
            p.drawLine(QPointF(0, 4), QPointF(0, 10));
            break;
        case CaptureIcon::FinishEditing:
            p.setPen(QPen(ink, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPolyline(QPolygonF{QPointF(3, 10), QPointF(8, 15), QPointF(17, 4)});
            break;
        case CaptureIcon::Finish:
            p.drawPolyline(QPolygonF{QPointF(7, 3), QPointF(16, 3), QPointF(16, 15)});
            p.drawRect(QRectF(4, 5, 10, 13));
            break;
        case CaptureIcon::Cancel:
            p.setPen(QPen(ink, 2, Qt::SolidLine, Qt::SquareCap));
            p.drawLine(QPointF(5, 5), QPointF(15, 15));
            p.drawLine(QPointF(15, 5), QPointF(5, 15));
            break;
        case CaptureIcon::FillPattern:
            p.setPen(Qt::NoPen);
            p.setBrush(ink);
            for (int y = 3; y < 18; y += 3)
                for (int x = 3; x < 18; x += 3)
                    p.drawRect(QRectF(x, y, 1.5, 1.5));
            break;
        case CaptureIcon::PatternRect:
            p.setPen(QPen(ink, 1.0));
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRectF(2.5, 4.5, 15, 11));
            p.setBrush(ink);
            for (int y = 6; y < 15; y += 3)
                for (int x = 4; x < 17; x += 3)
                    p.drawRect(QRectF(x, y, 1.5, 1.5));
            break;
        case CaptureIcon::PatternEllipse:
            p.setPen(QPen(ink, 1.0));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(QRectF(2, 4.5, 16, 11));
            p.setBrush(ink);
            for (int y = 6; y < 15; y += 3)
                for (int x = 4; x < 17; x += 3)
                    if (std::hypot((x - 10) / 7.0, (y - 10) / 4.5) < 0.9)
                        p.drawRect(QRectF(x, y, 1.5, 1.5));
            break;
        case CaptureIcon::ArrowHeadPlain:
            p.drawLine(QPointF(2, 10), QPointF(18, 10));
            break;
        case CaptureIcon::ArrowHeadFlat:
            p.drawLine(QPointF(2, 10), QPointF(16, 10));
            p.drawLine(QPointF(16, 5), QPointF(16, 15));
            break;
        case CaptureIcon::ArrowHeadEnd:
            p.drawLine(QPointF(2, 10), QPointF(15, 10));
            p.setBrush(ink);
            p.drawPolygon(QPolygonF{QPointF(18, 10), QPointF(12, 6), QPointF(12, 14)});
            break;
        case CaptureIcon::ArrowHeadStart:
            p.drawLine(QPointF(5, 10), QPointF(18, 10));
            p.setBrush(ink);
            p.drawPolygon(QPolygonF{QPointF(2, 10), QPointF(8, 6), QPointF(8, 14)});
            break;
        case CaptureIcon::ArrowHeadBoth:
            p.drawLine(QPointF(5, 10), QPointF(15, 10));
            p.setBrush(ink);
            p.drawPolygon(QPolygonF{QPointF(2, 10), QPointF(7, 6), QPointF(7, 14)});
            p.drawPolygon(QPolygonF{QPointF(18, 10), QPointF(13, 6), QPointF(13, 14)});
            break;
        case CaptureIcon::ArrowHeadHollowEnd:
            p.drawLine(QPointF(2, 10), QPointF(12, 10));
            p.setBrush(Qt::NoBrush);
            p.drawPolygon(QPolygonF{QPointF(18, 10), QPointF(12, 6), QPointF(12, 14)});
            break;
        case CaptureIcon::ArrowHeadHollowStart:
            p.drawLine(QPointF(8, 10), QPointF(18, 10));
            p.setBrush(Qt::NoBrush);
            p.drawPolygon(QPolygonF{QPointF(2, 10), QPointF(8, 6), QPointF(8, 14)});
            break;
        case CaptureIcon::ArrowHeadHollowBoth:
            p.drawLine(QPointF(7, 10), QPointF(13, 10));
            p.setBrush(Qt::NoBrush);
            p.drawPolygon(QPolygonF{QPointF(2, 10), QPointF(7, 6), QPointF(7, 14)});
            p.drawPolygon(QPolygonF{QPointF(18, 10), QPointF(13, 6), QPointF(13, 14)});
            break;
        case CaptureIcon::SwapDirection:
            p.drawLine(QPointF(4, 7), QPointF(16, 7));
            p.drawLine(QPointF(13, 4), QPointF(16, 7));
            p.drawLine(QPointF(13, 10), QPointF(16, 7));
            p.drawLine(QPointF(4, 13), QPointF(16, 13));
            p.drawLine(QPointF(7, 10), QPointF(4, 13));
            p.drawLine(QPointF(7, 16), QPointF(4, 13));
            break;
        case CaptureIcon::TextBold: {
            QFont f = p.font(); f.setPixelSize(13); f.setBold(true); p.setFont(f);
            p.setPen(ink);
            p.drawText(QRectF(0, 0, 20, 20), Qt::AlignCenter, QStringLiteral("B"));
            break;
        }
        case CaptureIcon::TextItalic: {
            QFont f = p.font(); f.setPixelSize(13); f.setItalic(true); p.setFont(f);
            p.setPen(ink);
            p.drawText(QRectF(0, 0, 20, 20), Qt::AlignCenter, QStringLiteral("I"));
            break;
        }
        case CaptureIcon::TextOutline: {
            QFont f = p.font(); f.setPixelSize(13); f.setBold(true); p.setFont(f);
            QPainterPath path;
            path.addText(QPointF(5, 15), f, QStringLiteral("A"));
            p.setPen(QPen(ink, 1.25));
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
            break;
        }
        case CaptureIcon::TextBackground: {
            p.setPen(Qt::NoPen);
            p.setBrush(ink);
            p.drawRoundedRect(QRectF(2, 2, 16, 16), 3, 3);
            QFont f = p.font(); f.setPixelSize(12); f.setBold(true); p.setFont(f);
            p.setPen(state == QIcon::On ? QColor("#2889ed") : Qt::white);
            p.drawText(QRectF(2, 2, 16, 16), Qt::AlignCenter, QStringLiteral("A"));
            break;
        }
        case CaptureIcon::NumberFilled: {
            p.setPen(Qt::NoPen);
            p.setBrush(ink);
            p.drawEllipse(QRectF(2, 2, 16, 16));
            QFont f = p.font(); f.setPixelSize(11); f.setBold(true); p.setFont(f);
            p.setPen(state == QIcon::On ? QColor("#2889ed") : Qt::white);
            p.drawText(QRectF(2, 2, 16, 16), Qt::AlignCenter, QStringLiteral("1"));
            break;
        }
        case CaptureIcon::NumberDoubleRing: {
            p.setPen(QPen(ink, 1.25));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(QRectF(2, 2, 16, 16));
            p.drawEllipse(QRectF(4.5, 4.5, 11, 11));
            QFont f = p.font(); f.setPixelSize(10); f.setBold(true); p.setFont(f);
            p.setPen(ink);
            p.drawText(QRectF(2, 2, 16, 16), Qt::AlignCenter, QStringLiteral("1"));
            break;
        }
        case CaptureIcon::NumberOutline: {
            p.setPen(QPen(ink, 1.5));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(QRectF(2, 2, 16, 16));
            QFont f = p.font(); f.setPixelSize(11); f.setBold(true); p.setFont(f);
            p.setPen(ink);
            p.drawText(QRectF(2, 2, 16, 16), Qt::AlignCenter, QStringLiteral("1"));
            break;
        }
        case CaptureIcon::MagnifierCutout: {
            p.setPen(QPen(ink, 1.25));
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRectF(2.5, 2.5, 11.5, 11.5));
            p.save();
            p.translate(14, 14);
            p.rotate(-45);
            p.setPen(QPen(ink, 1.2));
            p.setBrush(state == QIcon::On ? QColor(QStringLiteral("#2889ed")) : Qt::white);
            p.drawRoundedRect(QRectF(-2.5, -4.5, 5, 9), 1, 1);
            p.drawLine(QPointF(-2.5, 0), QPointF(2.5, 0));
            p.restore();
            break;
        }
        case CaptureIcon::MagnifierBorder: {
            p.setPen(QPen(ink, 1.25));
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRectF(2, 4, 10, 12));
            p.drawLine(QPointF(12, 10), QPointF(18, 10));
            break;
        }
        }
        icon.addPixmap(pixmap, mode, state);
       }
      }
    }
    cache.insert(key, icon);
    return icon;
}

} // namespace snipaste::ui
