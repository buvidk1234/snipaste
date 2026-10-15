#include "ui/annotation/annotation_effect_cache.h"
#include "ui/annotation/annotation_renderer.h"
#include "ui/annotation/annotation_selection_geometry.h"
#include "ui/annotation/annotation_stroke_preview.h"
#include "ui/annotation/annotation_toolbar_icons.h"

#include <QPainter>
#include <QDir>
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
    void lineStyles_data();
    void lineStyles();
    void lineStyleIcons();
    void arrowPreviews();
    void arrowTransparency_data();
    void arrowTransparency();
    void arrowTapers();
    void arrowExplicitNone();
    void arrowBoundsAndSelection();
    void arrowCombinations();
    void arrowPreviewTransforms();
    void arrowIcons();
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

void AnnotationRenderTest::lineStyles_data()
{
    QTest::addColumn<int>("style");
    QTest::addColumn<int>("tool");
    QTest::addColumn<qreal>("width");
    QTest::addColumn<qreal>("scale");
    for (const auto style : {Qt::SolidLine, Qt::DashLine, Qt::DotLine, Qt::DashDotLine, Qt::DashDotDotLine})
        for (const auto tool : {Tool::Pen, Tool::Line, Tool::Rectangle, Tool::Ellipse})
            for (const qreal width : {1.0, 3.0, 8.0})
                for (const qreal scale : {1.0, 1.25})
                {
                    const auto name = QString("%1-%2-%3-%4").arg(int(style)).arg(int(tool))
                        .arg(width).arg(scale).toLatin1();
                    QTest::newRow(name) << int(style) << int(tool) << width << scale;
                }
}

void AnnotationRenderTest::lineStyles()
{
    QFETCH(int, style);
    QFETCH(int, tool);
    QFETCH(qreal, width);
    QFETCH(qreal, scale);
    Shape shape;
    shape.tool = Tool(tool);
    shape.penStyle = Qt::PenStyle(style);
    shape.color = QColor(30, 160, 230);
    shape.width = width;
    shape.start = {20, 30};
    shape.end = {220, 130};
    shape.points = {shape.start, {90, 50}, {140, 120}, shape.end};
    QImage expected(260, 170, QImage::Format_ARGB32_Premultiplied);
    expected.fill(Qt::transparent);
    QImage actual = expected.copy();
    QImage preview = expected.copy();
    const AnnotationRenderer renderer(expected.size());
    QPainter reference(&expected), output(&actual), live(&preview);
    reference.setRenderHint(QPainter::Antialiasing);
    output.setRenderHint(QPainter::Antialiasing);
    live.setRenderHint(QPainter::Antialiasing);
    reference.setPen(QPen(shape.color, width * scale, Qt::PenStyle(style), Qt::RoundCap, Qt::RoundJoin));
    if (shape.tool == Tool::Pen)
        reference.drawPolyline(shape.points);
    else if (shape.tool == Tool::Line)
        reference.drawLine(shape.start, shape.end);
    else if (shape.tool == Tool::Rectangle)
        reference.drawRect(QRectF(shape.start, shape.end));
    else
        reference.drawEllipse(QRectF(shape.start, shape.end));
    renderer.drawShape(output, shape, scale);
    reference.end();
    output.end();
    QCOMPARE(actual, expected);
    const QString artifacts = qEnvironmentVariable("SNIPASTE_TEST_ARTIFACTS");
    if (!artifacts.isEmpty() && shape.tool == Tool::Pen && width == 3 && scale == 1)
    {
        QVERIFY(QDir().mkpath(artifacts));
        QVERIFY(actual.save(QDir(artifacts).filePath(QStringLiteral("brush-line-style-%1.png").arg(style))));
    }
    if (shape.tool == Tool::Pen && scale == 1)
    {
        StrokePreview stroke;
        stroke.draw(renderer, live, shape, QRectF(expected.rect()));
        live.end();
        QCOMPARE(preview, expected);
    }
}

void AnnotationRenderTest::lineStyleIcons()
{
    QList<QImage> icons;
    for (const auto style : {Qt::SolidLine, Qt::DashLine, Qt::DotLine, Qt::DashDotLine, Qt::DashDotDotLine})
    {
        const QImage actual = ui::annotation::makeLineMenuItemIcon(style, 128, 22)
            .pixmap(QSize(128, 22), 1.0).toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        QImage expected(actual.size(), QImage::Format_ARGB32_Premultiplied);
        expected.fill(Qt::transparent);
        QPainter painter(&expected);
        painter.setRenderHint(QPainter::Antialiasing);
        // Original PixelPerfect::penStyle uses Qt's square caps and built-in dash patterns.
        painter.setPen(QPen(QColor(QStringLiteral("#333333")), 2.4, style));
        painter.drawLine(QPointF(4, 11), QPointF(124, 11));
        painter.end();
        QCOMPARE(actual, expected);
        for (const auto &previous : icons)
            QVERIFY(actual != previous);
        icons.append(actual);
    }
}

void AnnotationRenderTest::arrowPreviews()
{
    const QString artifacts = qEnvironmentVariable("SNIPASTE_TEST_ARTIFACTS");
    if (artifacts.isEmpty())
        return;
    QImage image(960, 660, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9));
    painter.setPen(Qt::black);
    painter.drawText(20, 25, QStringLiteral("Style / width 8"));
    painter.drawText(340, 25, QStringLiteral("Short / width 3"));
    painter.drawText(490, 25, QStringLiteral("Diagonal / width 4"));
    painter.drawText(690, 25, QStringLiteral("Both / width 5"));
    painter.drawText(880, 25, QStringLiteral("Menu"));
    const char *names[] = {"Line", "Flat", "Circle", "Diamond", "Dart", "Open V", "Taper", "Hollow"};
    const AnnotationRenderer renderer(image.size());
    for (int index = 0; index < 8; ++index)
    {
        const qreal y = 75 + index * 76;
        Shape shape;
        shape.tool = Tool::Arrow;
        shape.arrowStyle = core::ArrowStyle::None;
        shape.endArrowHead = core::ArrowHead(index);
        shape.width = 8;
        shape.color = QColor(235, 35, 45);
        shape.start = {90, y};
        shape.end = {305, y};
        renderer.drawShape(painter, shape);
        shape.width = 3;
        shape.start = {360, y};
        shape.end = {395, y};
        renderer.drawShape(painter, shape);
        shape.width = 4;
        shape.start = {505, y + 20};
        shape.end = {620, y - 20};
        renderer.drawShape(painter, shape);
        shape.width = 5;
        shape.start = {700, y};
        shape.end = {825, y};
        shape.startArrowHead = core::ArrowHead(index);
        renderer.drawShape(painter, shape);
        painter.drawPixmap(885, int(y) - 11,
            ui::annotation::makeEndArrowMenuItemIcon(core::ArrowHead(index)).pixmap(QSize(34, 22), 1.0));
        painter.setPen(Qt::black);
        painter.drawText(12, int(y) + 5, QString::fromLatin1(names[index]));
    }
    painter.end();
    QVERIFY(QDir().mkpath(artifacts));
    QVERIFY(image.save(QDir(artifacts).filePath(QStringLiteral("arrow-styles.png"))));
}

void AnnotationRenderTest::arrowTransparency_data()
{
    QTest::addColumn<int>("head");
    QTest::addColumn<qreal>("length");
    QTest::addColumn<qreal>("width");
    QTest::addColumn<int>("direction");
    for (int head = 0; head < 8; ++head)
        for (qreal length : {3.0, 20.0, 180.0})
            for (qreal width : {1.0, 8.0})
                for (int direction = 0; direction < 3; ++direction)
                    QTest::newRow(qPrintable(QString("%1-%2-%3-%4").arg(head).arg(length).arg(width).arg(direction)))
                        << head << length << width << direction;
}

void AnnotationRenderTest::arrowTransparency()
{
    QFETCH(int, head);
    QFETCH(qreal, length);
    QFETCH(qreal, width);
    QFETCH(int, direction);
    Shape shape;
    shape.tool = Tool::Arrow;
    shape.arrowStyle = core::ArrowStyle::None;
    shape.endArrowHead = core::ArrowHead(head);
    shape.start = {210, 210};
    const QPointF directions[] = {{1, 0}, {-0.8, 0.6}, {0, -1}};
    shape.end = shape.start + directions[direction] * length;
    shape.width = width;
    shape.color = QColor(220, 30, 45, 96);
    QImage base(440, 440, QImage::Format_ARGB32_Premultiplied);
    base.fill(Qt::transparent);
    const QImage image = AnnotationRenderer(base.size()).render(base, {shape});
    int maximum = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            maximum = qMax(maximum, image.pixelColor(x, y).alpha());
    QVERIFY(maximum > 0);
    // One annotation must retain its chosen opacity at every head/shaft joint.
    QVERIFY2(maximum <= 96, qPrintable(QStringLiteral("Joint opacity rose to %1").arg(maximum)));
}

void AnnotationRenderTest::arrowTapers()
{
    const auto columnSpan = [](const QImage &image, int x) {
        int top = image.height(), bottom = -1;
        for (int y = 0; y < image.height(); ++y)
            if (image.pixelColor(x, y).alpha() > 128) { top = qMin(top, y); bottom = y; }
        return bottom - top + 1;
    };
    for (const auto head : {core::ArrowHead::Stealth, core::ArrowHead::Hollow})
    {
        Shape shape;
        shape.tool = Tool::Arrow;
        shape.start = {30, 60};
        shape.end = {270, 60};
        shape.width = 8;
        shape.color = Qt::red;
        shape.endArrowHead = head;
        QImage base(300, 120, QImage::Format_ARGB32_Premultiplied);
        base.fill(Qt::transparent);
        const QImage image = AnnotationRenderer(base.size()).render(base, {shape});
        QVERIFY(columnSpan(image, 190) > columnSpan(image, 70) + 2);
        if (head == core::ArrowHead::Hollow)
        {
            QCOMPARE(image.pixelColor(160, 60).alpha(), 0);
            QCOMPARE(image.pixelColor(250, 60).alpha(), 0);
        }
        else
            QCOMPARE(image.pixelColor(160, 60).alpha(), 255);
    }
}

void AnnotationRenderTest::arrowExplicitNone()
{
    Shape shape;
    shape.tool = Tool::Arrow;
    shape.start = {40, 60};
    shape.end = {250, 60};
    shape.width = 8;
    shape.color = Qt::red;
    shape.startArrowHead = core::ArrowHead::Flat;
    shape.endArrowHead = core::ArrowHead::None;
    shape.arrowStyle = core::combineArrowHeads(shape.startArrowHead, shape.endArrowHead);
    QImage base(300, 120, QImage::Format_ARGB32_Premultiplied);
    base.fill(Qt::transparent);
    const QImage image = AnnotationRenderer(base.size()).render(base, {shape});
    QVERIFY(image.pixelColor(40, 70).alpha() > 128);
    QCOMPARE(image.pixelColor(250, 70).alpha(), 0);
}

void AnnotationRenderTest::arrowBoundsAndSelection()
{
    for (int head = 1; head < 8; ++head)
    {
        Shape shape;
        shape.tool = Tool::Arrow;
        shape.start = {40, 100};
        shape.end = {260, 100};
        shape.width = 10;
        shape.color = Qt::red;
        shape.endArrowHead = core::ArrowHead(head);
        QImage base(320, 200, QImage::Format_ARGB32_Premultiplied);
        base.fill(Qt::transparent);
        const QImage image = AnnotationRenderer(base.size()).render(base, {shape});
        const QRectF bounds = ui::annotation::shapeBounds(shape).adjusted(-1, -1, 1, 1);
        // The complete visible head must repaint and be selectable, including its wings.
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                if (image.pixelColor(x, y).alpha() > 192)
                {
                    QVERIFY(bounds.contains(QPointF(x + 0.5, y + 0.5)));
                    QVERIFY(ui::annotation::containsShape(shape, QPointF(x + 0.5, y + 0.5), 0.5));
                }
    }
}

void AnnotationRenderTest::arrowCombinations()
{
    QImage base(340, 120, QImage::Format_ARGB32_Premultiplied);
    base.fill(Qt::transparent);
    const AnnotationRenderer renderer(base.size());
    for (int start = 0; start < 8; ++start)
        for (int end = 0; end < 8; ++end)
            for (const auto style : {Qt::SolidLine, Qt::DashLine, Qt::DotLine, Qt::DashDotLine, Qt::DashDotDotLine})
            {
                Shape shape;
                shape.tool = Tool::Arrow;
                shape.arrowStyle = core::ArrowStyle::None;
                shape.startArrowHead = core::ArrowHead(start);
                shape.endArrowHead = core::ArrowHead(end);
                shape.start = {50, 60};
                shape.end = {290, 60};
                shape.width = 8;
                shape.color = QColor(220, 30, 45, 96);
                shape.penStyle = style;
                const QImage image = renderer.render(base, {shape});
                QVector<QPoint> covered;
                QImage visited(base.size(), QImage::Format_Grayscale8);
                visited.fill(0);
                for (int y = 0; y < image.height(); ++y)
                {
                    const auto *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
                    for (int x = 0; x < image.width(); ++x)
                    {
                        QVERIFY(qAlpha(row[x]) <= 96);
                        if (qAlpha(row[x]) > 8)
                            covered.append({x, y});
                    }
                }
                QVERIFY(!covered.isEmpty());
                if (style != Qt::SolidLine)
                    continue;
                // Solid arrows, including hollow outlines, must form one connected mark.
                QVector<QPoint> queue{covered.first()};
                visited.scanLine(queue.first().y())[queue.first().x()] = 1;
                for (qsizetype i = 0; i < queue.size(); ++i)
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx)
                        {
                            const QPoint point = queue[i] + QPoint(dx, dy);
                            if (!image.rect().contains(point) || visited.constScanLine(point.y())[point.x()] ||
                                qAlpha(image.pixel(point)) <= 8)
                                continue;
                            visited.scanLine(point.y())[point.x()] = 1;
                            queue.append(point);
                        }
                QVERIFY2(queue.size() == covered.size(),
                    qPrintable(QStringLiteral("Disconnected endpoints %1/%2").arg(start).arg(end)));
            }
}

void AnnotationRenderTest::arrowPreviewTransforms()
{
    for (const auto head : {core::ArrowHead::Triangle, core::ArrowHead::Classic,
                            core::ArrowHead::Stealth, core::ArrowHead::Hollow})
    {
        Shape shape;
        shape.tool = Tool::Arrow;
        shape.start = {30, 60};
        shape.end = {240, 60};
        shape.width = 8;
        shape.color = Qt::red;
        shape.endArrowHead = head;
        shape.erasedPath.addRect(QRectF(100, 0, 25, 150));
        QImage source(300, 150, QImage::Format_ARGB32_Premultiplied);
        source.fill(Qt::transparent);
        const AnnotationRenderer renderer(source.size());
        const QImage erased = renderer.render(source, {shape});
        QCOMPARE(erased.pixelColor(110, 60).alpha(), 0);
        QVERIFY(erased.pixelColor(200, 60).alpha() > 0 || head == core::ArrowHead::Hollow);
        // Scaling a preview must preserve the same footprint and erased region as export.
        QImage preview(600, 300, QImage::Format_ARGB32_Premultiplied);
        preview.fill(Qt::transparent);
        QPainter painter(&preview);
        painter.scale(2, 2);
        painter.setRenderHint(QPainter::Antialiasing);
        renderer.drawShape(painter, shape);
        painter.end();
        QCOMPARE(preview.pixelColor(220, 120).alpha(), 0);
        QVERIFY(preview.pixelColor(400, 120).alpha() > 0 || head == core::ArrowHead::Hollow);
        // A legacy style still renders when no explicit endpoint pair is present.
        shape.erasedPath.clear();
        shape.endArrowHead = core::ArrowHead::None;
        shape.arrowStyle = core::ArrowStyle::HollowStart;
        shape.rotation = 32;
        const QImage legacy = renderer.render(source, {shape});
        shape.startArrowHead = core::ArrowHead::Hollow;
        shape.arrowStyle = core::ArrowStyle::None;
        QCOMPARE(renderer.render(source, {shape}), legacy);
    }
}

void AnnotationRenderTest::arrowIcons()
{
    QList<QImage> icons;
    for (int head = 0; head < 8; ++head)
    {
        const auto style = core::ArrowHead(head);
        const QImage end = ui::annotation::makeEndArrowMenuItemIcon(style)
            .pixmap(QSize(34, 22), 1.0).toImage();
        const QImage start = ui::annotation::makeStartArrowMenuItemIcon(style)
            .pixmap(QSize(34, 22), 1.0).toImage();
        for (const auto &previous : icons)
            QVERIFY(end != previous);
        icons.append(end);
        int mismatch = 0;
        // Initial and terminal previews must show the same shape, mirrored.
        for (int y = 0; y < end.height(); ++y)
            for (int x = 0; x < end.width(); ++x)
                mismatch += std::abs(qAlpha(end.pixel(x, y)) - qAlpha(start.pixel(end.width() - 1 - x, y)));
        QVERIFY2(mismatch < 1200, qPrintable(QStringLiteral("Icon %1 mirror difference %2").arg(head).arg(mismatch)));
    }
}

QTEST_MAIN(AnnotationRenderTest)
#include "annotation_render_test.moc"
