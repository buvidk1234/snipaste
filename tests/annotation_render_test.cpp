#include "ui/annotation/annotation_effect_cache.h"
#include "ui/annotation/annotation_renderer.h"
#include "ui/annotation/annotation_stroke_preview.h"

#include <QPainter>
#include <QTest>
#include <cmath>

using namespace snipaste;
using Shape = core::AnnotationShape;
using Tool = core::AnnotationTool;
using EffectShape = core::EffectShape;
using ui::annotation::AnnotationRenderer;
using ui::annotation::EffectCache;
using ui::annotation::StrokePreview;

namespace
{
QImage sourceImage()
{
    QImage image(320, 240, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            image.setPixelColor(x, y, QColor(x % 256, y % 256, (x * 3 + y * 5) % 256, 120 + x % 136));
    return image;
}

Shape area(Tool tool, QPointF start, QPointF end)
{
    Shape shape;
    shape.tool = tool;
    shape.start = start;
    shape.end = end;
    shape.width = 8;
    shape.effectSize = 11;
    shape.effectShape = EffectShape::Rectangle;
    shape.color = QColor(220, 40, 70, 130);
    return shape;
}

QVector<Shape> layers()
{
    auto base = area(Tool::Rectangle, {10, 10}, {160, 190});
    base.filled = true;
    auto mosaic = area(Tool::Mosaic, {-10, 20}, {130, 200});
    auto magnifier = area(Tool::Magnifier, {100, 80}, {240, 220});
    magnifier.sourceRect = {20, 30, 70, 65};
    auto blur = area(Tool::Blur, {150, 10}, {310, 180});
    blur.effectShape = EffectShape::Ellipse;
    blur.erasedPath.addEllipse(QRectF(160, 70, 80, 80));
    auto marker = area(Tool::Marker, {60, 100}, {260, 100});
    marker.points = {{60, 100}, {140, 60}, {260, 100}};
    auto brush = area(Tool::Mosaic, {90, 180}, {300, 220});
    brush.effectShape = EffectShape::Brush;
    brush.width = 24;
    brush.points = {{90, 180}, {140, 100}, {170, 160}, {300, 220}};
    brush.erasedPath.addRect(QRectF(160, 140, 30, 50));
    return {base, mosaic, magnifier, blur, marker, brush};
}
}

class AnnotationRenderTest : public QObject
{
    Q_OBJECT
  private slots:
    void effectDependencies();
    void effectBudgetAndSourceChanges();
    void strokePreview_data();
    void strokePreview();
};

void AnnotationRenderTest::effectDependencies()
{
    const QImage source = sourceImage();
    const AnnotationRenderer renderer(source.size());
    EffectCache cache;
    auto shapes = layers();
    const auto check = [&](quint64 expectedHits) {
        const auto hits = cache.hits();
        const QImage reference = renderer.render(source, shapes);
        const QImage cached = renderer.render(source, shapes, &cache);
        QCOMPARE(cached, reference);
        QCOMPARE(cache.hits() - hits, expectedHits);
        QVERIFY(cache.bytes() > 0 && cache.bytes() <= 8 * 1024 * 1024);
    };
    check(0);
    check(3);
    shapes.append(area(Tool::Ellipse, {30, 60}, {220, 210}));
    check(3);
    shapes.last().color = Qt::blue;
    check(3);
    const auto undo = shapes;
    shapes[4].points[1] += QPointF(10, 20);
    check(2);
    shapes = undo; // Undo changes the later mosaic's sampled pixels again.
    check(2);
    shapes[0].color = Qt::green;
    check(0);
    shapes.removeAt(2); // Removing a magnifier invalidates effects above it.
    check(1);
    shapes.swapItemsAt(0, 1);
    check(0);
    shapes[0].erasedPath.addEllipse(QRectF(40, 80, 50, 50));
    check(0);
    shapes = undo; // Restore the complete history snapshot and its ordering.
    check(0);
    shapes[1].effectSize = 6;
    check(0);
    shapes[3].effectShape = EffectShape::Rectangle;
    check(1);
}

void AnnotationRenderTest::effectBudgetAndSourceChanges()
{
    QImage source = sourceImage();
    auto shapes = layers();
    const AnnotationRenderer renderer(source.size());
    for (const qint64 budget : {qint64(0), qint64(1024), qint64(100000)})
    {
        EffectCache cache(budget);
        QCOMPARE(renderer.render(source, shapes, &cache), renderer.render(source, shapes));
        QCOMPARE(renderer.render(source, shapes, &cache), renderer.render(source, shapes));
        QVERIFY(cache.bytes() <= budget);
    }
    EffectCache cache;
    renderer.render(source, shapes, &cache);
    source.setPixelColor(60, 80, Qt::yellow);
    QCOMPARE(renderer.render(source, shapes, &cache), renderer.render(source, shapes));
    QCOMPARE(cache.hits(), quint64(0));
    source = source.scaled(360, 280);
    const AnnotationRenderer larger(source.size());
    QCOMPARE(larger.render(source, shapes, &cache), larger.render(source, shapes));
    QCOMPARE(cache.hits(), quint64(0));
    shapes.clear();
    QCOMPARE(larger.render(source, shapes, &cache), larger.render(source, shapes));
    QCOMPARE(cache.bytes(), qint64(0));
}

void AnnotationRenderTest::strokePreview_data()
{
    QTest::addColumn<int>("tool");
    QTest::addColumn<qreal>("width");
    QTest::addColumn<qreal>("scale");
    QTest::addColumn<int>("path");
    QTest::addColumn<bool>("antialias");
    for (const auto tool : {Tool::Pen, Tool::Marker})
        for (const qreal width : {1.0, 5.5, 25.0})
            for (const qreal scale : {0.65, 1.0, 1.25})
                for (int path = 0; path < 4; ++path)
                    for (bool antialias : {false, true})
                    {
                        const QByteArray name = QString("%1-%2-%3-%4-%5")
                                                    .arg(int(tool)).arg(width).arg(scale).arg(path).arg(antialias)
                                                    .toLatin1();
                        QTest::newRow(name) << int(tool) << width << scale << path << antialias;
                    }
}

void AnnotationRenderTest::strokePreview()
{
    QFETCH(int, tool);
    QFETCH(qreal, width);
    QFETCH(qreal, scale);
    QFETCH(int, path);
    QFETCH(bool, antialias);
    Shape shape;
    shape.tool = Tool(tool);
    shape.width = width;
    shape.color = QColor(30, 160, 230, 130);
    for (int i = 0; i < 2049; ++i)
    {
        const qreal t = i * 0.013;
        QPointF point;
        if (path == 0)
            point = {i * 0.4 - 20, 200 + 150 * std::sin(t)};
        else if (path == 1)
            point = {320 + 250 * std::sin(t), 240 + 180 * std::sin(t * 2)};
        else if (path == 2)
            point = {(i / 400) % 2 ? 320.0 : 100 + (i / 3) % 500, 100 + (i / 400) * 70.0};
        else
            point = {i < 1024 ? 20 + i * 0.5 : 1044 - i * 0.5, 180 + (i % 64 == 0 ? 80.0 : 0.0)};
        shape.points.append(point);
    }
    shape.start = shape.points.first();
    shape.end = shape.points.last();
    const AnnotationRenderer renderer({900, 650});
    StrokePreview preview;
    const QPointF offset(13.25, 7.75);
    // Start with a shorter stroke, then append into the existing geometry index.
    const auto allPoints = shape.points;
    for (const qsizetype count : {qsizetype(160), qsizetype(1100), qsizetype(2049), qsizetype(300)})
    {
        shape.points = allPoints.first(count);
        for (const QRectF visible : {QRectF(0, 0, 900, 650), QRectF(85, 85, 35, 30),
                                     QRectF(290, 190, 55, 70), QRectF(495, 155, 30, 130),
                                     QRectF(700, 0, 80, 450)})
        {
            const auto draw = [&](bool indexed) {
                QImage image(900, 650, QImage::Format_ARGB32_Premultiplied);
                image.fill(QColor(70, 40, 50, 210));
                QPainter painter(&image);
                painter.setRenderHint(QPainter::Antialiasing, antialias);
                painter.translate(offset);
                painter.scale(scale, scale);
                painter.setClipRect(visible);
                if (indexed)
                    preview.draw(renderer, painter, shape, visible);
                else
                    renderer.drawShape(painter, shape);
                painter.end();
                return image;
            };
            const QImage reference = draw(false);
            const QImage indexed = draw(true);
            QCOMPARE(indexed, reference);
        }
    }
    // Unsupported styles and edited/rotated strokes retain the complete renderer.
    for (int fallback = 0; fallback < 3; ++fallback)
    {
        if (fallback == 0)
            shape.penStyle = Qt::DashLine;
        else if (fallback == 1)
            shape.rotation = 35;
        else
            shape.erasedPath.addRect(QRectF(100, 100, 70, 200));
        QImage expected(900, 650, QImage::Format_ARGB32_Premultiplied);
        expected.fill(Qt::transparent);
        QImage actual = expected.copy();
        QPainter original(&expected), optimized(&actual);
        renderer.drawShape(original, shape);
        preview.draw(renderer, optimized, shape, {100, 100, 80, 80});
        original.end();
        optimized.end();
        QCOMPARE(actual, expected);
    }
}

QTEST_MAIN(AnnotationRenderTest)
#include "annotation_render_test.moc"
