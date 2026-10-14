#include "ui/annotation/annotation_canvas.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <cmath>
#include <cstdio>

// Controlled pointer input on our own canvas. No system cursor or hotkey use.
class PaintRecorder final : public QObject
{
  public:
    QRegion damage;
    qint64 pixels = 0;
    int paints = 0;
    bool eventFilter(QObject*, QEvent* event) override
    {
        if (event->type() == QEvent::Paint)
        {
            const auto region = static_cast<QPaintEvent*>(event)->region();
            damage |= region;
            for (const QRect& rect : region)
                pixels += qint64(rect.width()) * rect.height();
            ++paints;
        }
        return false;
    }
};

static void mouse(QWidget& widget, QEvent::Type type, QPointF point,
                  Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QMouseEvent event(type, point, point, type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                      type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, modifiers);
    QApplication::sendEvent(&widget, &event);
    QApplication::processEvents();
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    using Canvas = snipaste::ui::AnnotationCanvas;
    QImage source(3840, 2160, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < source.height(); ++y)
    {
        auto* row = reinterpret_cast<QRgb*>(source.scanLine(y));
        for (int x = 0; x < source.width(); ++x)
            row[x] = qRgb((x + y) % 256, (x * 3 + y) % 256, (y * 7) % 256);
    }
    QJsonArray results;
    bool allEqual = true;
    // Also exercise fractional image scales, crop origins and dashed joins.
    for (int mode = 0; mode < 5; ++mode)
    {
        if (argc > 1 && QString::fromLocal8Bit(argv[1]).toInt() != mode)
            continue;
        const qreal scale = mode == 0 ? 1.0 : mode == 1 ? 1.25 : 1.75;
        for (const auto tool :
             {Canvas::Tool::Pen, Canvas::Tool::Marker, Canvas::Tool::Rectangle, Canvas::Tool::Arrow})
        {
            Canvas canvas(source);
            canvas.setAutoSelectOnCreation(false);
            canvas.resize((QSizeF(source.size()) / scale).toSize());
            if (mode >= 2)
                canvas.setCanvasFrame(QPoint(137, 83),
                                      mode == 4 ? QSizeF(source.size()) / scale : QSizeF(canvas.size()),
                                      QRect(QPoint(137, 83), canvas.size()));
            canvas.setTool(tool);
            canvas.setPenWidth(tool == Canvas::Tool::Marker ? 30 : 5);
            if (mode == 3)
                canvas.setPenStyle(Qt::DashLine);
            PaintRecorder recorder;
            canvas.installEventFilter(&recorder);
            canvas.show();
            QApplication::processEvents();
            const qreal dpr = canvas.devicePixelRatioF();
            const QSize physical(qCeil(canvas.width() * dpr), qCeil(canvas.height() * dpr));
            QImage incremental(physical, QImage::Format_ARGB32_Premultiplied);
            incremental.setDevicePixelRatio(dpr);
            incremental.fill(Qt::transparent);
            canvas.render(&incremental);
            QImage originalBackground(physical, QImage::Format_ARGB32_Premultiplied);
            originalBackground.setDevicePixelRatio(dpr);
            originalBackground.fill(Qt::transparent);
            {
                QPainter background(&originalBackground);
                background.fillRect(canvas.rect(), QColor(30, 30, 30));
                QRectF frame;
                if (mode >= 2)
                {
                    const QSizeF logical = mode == 4 ? QSizeF(source.size()) / scale : QSizeF(canvas.size());
                    frame = QRectF(QPointF(-137, -83), logical);
                }
                else
                {
                    const qreal ratio = qMin(qreal(canvas.width()) / source.width(),
                                             qreal(canvas.height()) / source.height());
                    const QSizeF size = QSizeF(source.size()) * ratio;
                    frame = QRectF(QPointF((canvas.width() - size.width()) * 0.5,
                                           (canvas.height() - size.height()) * 0.5),
                                   size);
                }
                background.drawImage(frame, source);
            }
            const bool sameBackground = originalBackground == incremental;
            mouse(canvas, QEvent::MouseButtonPress, QPointF(100, 600));
            canvas.render(&incremental);
            recorder.pixels = recorder.paints = 0;
            double totalMs = 0;
            double maxMs = 0;
            int comparisons = 0;
            bool equal = true;
            int firstMismatch = 0;
            QRect mismatchBounds;
            for (int i = 1; i <= 240; ++i)
            {
                recorder.damage = {};
                QElapsedTimer timer;
                timer.start();
                mouse(canvas, QEvent::MouseMove, QPointF(100 + i * 6, 600 + std::sin(i * 0.09) * 200),
                      i >= 120 && i <= 135 ? Qt::ShiftModifier : Qt::NoModifier);
                const double ms = timer.nsecsElapsed() / 1e6;
                totalMs += ms;
                maxMs = qMax(maxMs, ms);
                const QRegion dirty = recorder.damage;
                const auto pixels = recorder.pixels;
                const auto paints = recorder.paints;
                if (!dirty.isEmpty())
                    canvas.render(&incremental, dirty.boundingRect().topLeft(), dirty);
                if (i % 12 == 0)
                {
                    QImage full(physical, QImage::Format_ARGB32_Premultiplied);
                    full.setDevicePixelRatio(dpr);
                    full.fill(Qt::transparent);
                    canvas.render(&full);
                    if (full != incremental && !firstMismatch)
                    {
                        firstMismatch = i;
                        for (int y = 0; y < full.height(); ++y)
                        {
                            const auto* a = reinterpret_cast<const QRgb*>(full.constScanLine(y));
                            const auto* b = reinterpret_cast<const QRgb*>(incremental.constScanLine(y));
                            for (int x = 0; x < full.width(); ++x)
                                if (a[x] != b[x])
                                    mismatchBounds |= QRect(x, y, 1, 1);
                        }
                    }
                    equal &= full == incremental;
                    ++comparisons;
                }
                recorder.pixels = pixels;
                recorder.paints = paints;
            }
            mouse(canvas, QEvent::MouseButtonRelease, QPointF(1540, 600));
            const QImage output = canvas.fullRenderedImage();
            const auto hash =
                QCryptographicHash::hash(
                    QByteArrayView(reinterpret_cast<const char*>(output.constBits()), output.sizeInBytes()),
                    QCryptographicHash::Sha256)
                    .toHex();
            allEqual &= equal && sameBackground;
            results.append(
                QJsonObject{{"tool", int(tool)},
                            {"movesMs", totalMs},
                            {"scale", scale},
                            {"devicePixelRatio", dpr},
                            {"sameOriginalBackground", sameBackground},
                            {"maxMoveMs", maxMs},
                            {"paints", recorder.paints},
                            {"paintPixels", double(recorder.pixels)},
                            {"comparisons", comparisons},
                            {"partialEqualsFull", equal},
                            {"firstMismatch", firstMismatch},
                            {"mismatchBounds", QJsonArray{mismatchBounds.x(), mismatchBounds.y(),
                                                          mismatchBounds.width(), mismatchBounds.height()}},
                            {"outputSha256", QString::fromLatin1(hash)}});
        }
    }
    const auto json = QJsonDocument(results).toJson();
    std::fwrite(json.constData(), 1, size_t(json.size()), stdout);
    return allEqual ? 0 : 1;
}
