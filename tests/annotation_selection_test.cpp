#include "ui/annotation/annotation_canvas.h"
#include "ui/annotation/annotation_options.h"
#include "ui/annotation/annotation_dialog.h"
#include "ui/annotation/annotation_renderer.h"
#include "ui/annotation/annotation_selection_geometry.h"
#include "ui/snip/snip_overlay.h"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QStackedLayout>
#include <QTest>
#include <QToolButton>
#include <QWheelEvent>

using namespace snipaste;
using Canvas = ui::AnnotationCanvas;

namespace
{
QImage background()
{
    QImage image(640, 420, QImage::Format_RGB32);
    image.fill(Qt::white);
    return image;
}
void move(QWidget *widget, QPoint point, Qt::MouseButtons buttons = Qt::NoButton)
{
    QMouseEvent event(QEvent::MouseMove, QPointF(point), QPointF(widget->mapToGlobal(point)),
                      Qt::NoButton, buttons, Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
}
void drag(QWidget *widget, QPoint start, QPoint end)
{
    QTest::mousePress(widget, Qt::LeftButton, Qt::NoModifier, start);
    move(widget, end, Qt::LeftButton);
    QTest::mouseRelease(widget, Qt::LeftButton, Qt::NoModifier, end);
}
void wheel(QWidget *widget)
{
    const QPoint center = widget->rect().center();
    QWheelEvent event(QPointF(center), QPointF(widget->mapToGlobal(center)), {}, QPoint(0, 120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(widget, &event);
    QVERIFY(event.isAccepted());
}
}

class AnnotationSelectionTest : public QObject
{
    Q_OBJECT
  private slots:
    void optionsCreateOnlySelectedPages();
    void selectedStyleAndUndo();
    void reselectSynchronizesControls();
    void pointerFeedback_data();
    void pointerFeedback();
    void overlayKeepsAnnotationGesture();
    void lineEndpoints();
    void smallRectangleEdgeYieldsToMove();
    void arrowDropdownGeometryAndItems();
    void lineStyleMenusAndHistory();
    void numberCursorMatchesStamp_data();
    void numberCursorMatchesStamp();
    void numberSelectionFitsBadge();
    void numberModeSelectsExistingShape_data();
    void numberModeSelectsExistingShape();
};

void AnnotationSelectionTest::numberModeSelectsExistingShape_data()
{
    QTest::addColumn<int>("tool");
    QTest::addColumn<QPoint>("hit");
    QTest::addColumn<int>("page");
    QTest::newRow("rectangle") << int(Canvas::Tool::Rectangle) << QPoint(80, 105) << 0;
    QTest::newRow("arrow") << int(Canvas::Tool::Arrow) << QPoint(160, 130) << 1;
}

void AnnotationSelectionTest::numberModeSelectsExistingShape()
{
    QFETCH(int, tool);
    QFETCH(QPoint, hit);
    QFETCH(int, page);
    const auto targetTool = Canvas::Tool(tool);
    Canvas canvas(background());
    canvas.resize(640, 420);
    canvas.setAutoSelectOnCreation(true);
    ui::AnnotationOptions options(nullptr, [] {});
    options.setCanvas(&canvas);
    canvas.show();
    canvas.setTool(targetTool);
    drag(&canvas, {80, 80}, {240, 180});
    canvas.setTool(Canvas::Tool::Number);
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, {400, 300});
    const auto shapes = canvas.document().shapes();
    const QImage before = canvas.renderedImage();
    QCOMPARE(options.stack()->currentIndex(), 6);
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, hit);
    QCOMPARE(canvas.tool(), targetTool);
    QCOMPARE(canvas.selectedShapeIndex(), 0);
    QCOMPARE(options.stack()->currentIndex(), page);
    QVERIFY(canvas.document().shapes() == shapes);
    QCOMPARE(canvas.renderedImage(), before);
    QCOMPARE(canvas.document().nextNumber(), 2);
    drag(&canvas, hit, hit + QPoint(12, 10));
    QCOMPARE(canvas.document().shapes().size(), 2);
    QCOMPARE(canvas.document().shapes().first().start, shapes.first().start + QPointF(12, 10));
    canvas.undo();
    QVERIFY(canvas.document().shapes() == shapes);
    canvas.setTool(Canvas::Tool::Number);
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, {500, 300});
    QCOMPARE(canvas.document().shapes().size(), 3);
    QCOMPARE(canvas.document().shapes().last().text, QStringLiteral("2"));
}

void AnnotationSelectionTest::numberCursorMatchesStamp_data()
{
    QTest::addColumn<qreal>("scale");
    QTest::addColumn<qreal>("size");
    QTest::addColumn<int>("style");
    for (const qreal scale : {0.5, 1.0, 1.5})
        for (const qreal size : {12.0, 24.0, 96.0})
            for (int style = 0; style < 3; ++style)
                QTest::newRow(qPrintable(QStringLiteral("%1-%2-%3").arg(scale).arg(size).arg(style)))
                    << scale << size << style;
}

void AnnotationSelectionTest::numberCursorMatchesStamp()
{
    QFETCH(qreal, scale);
    QFETCH(qreal, size);
    QFETCH(int, style);
    Canvas canvas(background());
    canvas.setMinimumSize(0, 0);
    canvas.resize(qRound(640 * scale), qRound(420 * scale));
    canvas.setAutoSelectOnCreation(false);
    canvas.setTool(Canvas::Tool::Number);
    canvas.setNumberSize(size);
    canvas.setNumberStyle(style);
    canvas.setTextFontFamily(QStringLiteral("Arial"));
    canvas.show();
    const QPoint position(qRound(200 * scale), qRound(200 * scale));
    QImage tenthPreview;
    for (int number = 1; number <= 10; ++number)
    {
        const QCursor cursor = canvas.cursor();
        const QImage preview = cursor.pixmap().toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        QCOMPARE(preview.devicePixelRatio(), canvas.devicePixelRatioF());
        QVERIFY(preview.deviceIndependentSize().width() >= size * scale + 2);
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, position);
        QCOMPARE(canvas.document().shapes().size(), number);
        const auto &stamp = canvas.document().shapes().last();
        QCOMPARE(stamp.text, QString::number(number));
        QImage expected(preview.size(), QImage::Format_ARGB32_Premultiplied);
        expected.setDevicePixelRatio(preview.devicePixelRatio());
        expected.fill(Qt::transparent);
        QPainter painter(&expected);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.translate(cursor.hotSpot());
        painter.scale(scale, scale);
        painter.translate(-stamp.start);
        ui::annotation::AnnotationRenderer(background().size()).drawShape(painter, stamp);
        painter.end();
        QCOMPARE(preview, expected);
        if (number == 10)
            tenthPreview = preview;
    }
    const QImage nextPreview = canvas.cursor().pixmap().toImage();
    canvas.undo();
    QCOMPARE(canvas.cursor().pixmap().toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied), tenthPreview);
    canvas.redo();
    QCOMPARE(canvas.cursor().pixmap().toImage(), nextPreview);
}

void AnnotationSelectionTest::numberSelectionFitsBadge()
{
    core::AnnotationShape shape;
    shape.tool = Canvas::Tool::Number;
    shape.start = shape.end = QPointF(100, 100);
    for (const qreal size : {12.0, 24.0, 96.0})
        for (const qreal width : {2.0, 16.0, 64.0})
            for (int style = 0; style < 3; ++style)
            {
                shape.numberSize = size;
                shape.width = width;
                shape.numberStyle = style;
                const QRectF frame = ui::annotation::selectionRect(shape);
                QCOMPARE(frame.center(), shape.start);
                QCOMPARE(frame.size(), QSizeF(size, size));
                const auto anchor = ui::annotation::selectionHandles(frame)[0];
                ui::annotation::resizeShape(shape, 4, QPointF(4, 4));
                QCOMPARE(ui::annotation::selectionHandles(ui::annotation::selectionRect(shape))[0], anchor);
                shape.start = shape.end = QPointF(100, 100);
            }
}

void AnnotationSelectionTest::optionsCreateOnlySelectedPages()
{
    Canvas canvas(background());
    ui::AnnotationOptions options(nullptr, [] {});
    options.setCanvas(&canvas);
    QCOMPARE(options.stack()->count(), 9);
    QVERIFY(options.findChildren<QToolButton *>().isEmpty());
    // Index zero is already current; the first click must still create it.
    canvas.setPenColor(Qt::blue);
    canvas.setPenWidth(8);
    canvas.setTool(Canvas::Tool::Rectangle);
    auto *shapePage = options.stack()->currentWidget();
    QVERIFY(shapePage->findChild<QToolButton *>(QStringLiteral("annotationRectangle")));
    QVERIFY(!options.findChild<QToolButton *>(QStringLiteral("annotationStartArrowStyle")));
    QVERIFY(!options.findChild<QToolButton *>(QStringLiteral("annotationTextBold")));
    QCOMPARE(canvas.penColor(), QColor(Qt::blue));
    QCOMPARE(canvas.penWidth(), qreal(8));
    canvas.setTool(Canvas::Tool::Text);
    QVERIFY(options.findChild<QToolButton *>(QStringLiteral("annotationTextBold")));
    canvas.setTextBold(true);
    canvas.setTool(Canvas::Tool::Rectangle);
    QCOMPARE(options.stack()->currentWidget(), shapePage);
    canvas.setTool(Canvas::Tool::Text);
    QVERIFY(options.findChild<QToolButton *>(QStringLiteral("annotationTextBold"))->isChecked());
    QVERIFY(!options.findChild<QToolButton *>(QStringLiteral("annotationStartArrowStyle")));
}

void AnnotationSelectionTest::selectedStyleAndUndo()
{
    Canvas canvas(background());
    canvas.resize(640, 420);
    ui::AnnotationOptions options(nullptr, [] {});
    options.setCanvas(&canvas);
    canvas.show(); options.show();
    drag(&canvas, {80, 80}, {240, 180});
    const QImage original = canvas.renderedImage();
    const qreal width = canvas.penWidth();
    wheel(&canvas);
    QCOMPARE(canvas.penWidth(), width + 1);
    QVERIFY(canvas.renderedImage() != original);
    canvas.undo(); QCOMPARE(canvas.renderedImage(), original);
    QVERIFY(canvas.canRedo());
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, {80, 105});
    canvas.setPenWidth(canvas.penWidth());
    QVERIFY(canvas.canRedo());
    canvas.redo();
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, {80, 105});
    auto *page = options.stack()->currentWidget();
    auto *wide = page->findChild<QToolButton *>(QStringLiteral("penWidth8"));
    QVERIFY(wide);
    wide->click(); QCOMPARE(canvas.penWidth(), qreal(8));
    const QImage widened = canvas.renderedImage();
    QVERIFY(widened != original);
    auto *color = page->findChild<QToolButton *>(QStringLiteral("annotationColor_00a2e8"));
    QVERIFY(color); color->click();
    QCOMPARE(canvas.penColor(), QColor(QStringLiteral("#00a2e8")));
    QVERIFY(canvas.renderedImage() != widened);
    auto *styles = page->findChild<QToolButton *>(QStringLiteral("annotationLineStyle"));
    QVERIFY(styles && styles->menu());
    for (auto *action : styles->menu()->actions())
        if (action->data().toInt() == int(Qt::DashLine)) action->trigger();
    QCOMPARE(canvas.annotationStyle().penStyle, Qt::DashLine);
    auto *fill = page->findChild<QToolButton *>(QStringLiteral("annotationShapeFilled"));
    QVERIFY(fill); fill->click();
    QVERIFY(canvas.shapeFilled());
    auto *ellipse = page->findChild<QToolButton *>(QStringLiteral("annotationEllipse"));
    QVERIFY(ellipse); ellipse->click();
    QCOMPARE(canvas.tool(), Canvas::Tool::Ellipse);
    const QImage changed = canvas.renderedImage();
    canvas.undo(); QVERIFY(canvas.renderedImage() != changed);
    canvas.redo(); QCOMPARE(canvas.renderedImage(), changed);
}

void AnnotationSelectionTest::reselectSynchronizesControls()
{
    Canvas canvas(background());
    canvas.resize(640, 420);
    ui::AnnotationOptions options(nullptr, [] {});
    options.setCanvas(&canvas);
    canvas.show();
    canvas.setPenColor(Qt::red); canvas.setPenWidth(3);
    drag(&canvas, {80, 80}, {220, 180});
    QTest::keyClick(&canvas, Qt::Key_Escape);
    canvas.setTool(Canvas::Tool::Ellipse);
    canvas.setPenColor(Qt::blue); canvas.setPenWidth(8);
    canvas.setPenStyle(Qt::DashLine); canvas.setShapeFilled(true);
    drag(&canvas, {350, 80}, {500, 220});
    const QImage both = canvas.renderedImage();
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, {80, 105});
    QCOMPARE(canvas.tool(), Canvas::Tool::Rectangle);
    QCOMPARE(canvas.penColor(), QColor(Qt::red));
    QCOMPARE(canvas.penWidth(), qreal(3));
    QCOMPARE(options.stack()->currentIndex(), 0);
    QVERIFY(options.findChild<QToolButton *>(QStringLiteral("annotationRectangle"))->isChecked());
    QVERIFY(!options.findChild<QToolButton *>(QStringLiteral("annotationShapeFilled"))->isChecked());
    for (auto *button : options.findChildren<QToolButton *>(QStringLiteral("annotationLineStyle")))
        for (auto *action : button->menu()->actions())
            QCOMPARE(action->isChecked(), action->data().toInt() == int(Qt::SolidLine));
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, {420, 130});
    QCOMPARE(canvas.tool(), Canvas::Tool::Ellipse);
    QCOMPARE(canvas.penWidth(), qreal(8));
    QCOMPARE(canvas.penColor(), QColor(Qt::blue));
    QVERIFY(options.findChild<QToolButton *>(QStringLiteral("annotationEllipse"))->isChecked());
    QVERIFY(options.findChild<QToolButton *>(QStringLiteral("annotationShapeFilled"))->isChecked());
    QCOMPARE(canvas.renderedImage(), both); // Reading a selection must not edit it.
    ui::AnnotationOptions secondOptions(nullptr, [] {});
    secondOptions.setCanvas(&canvas);
    QCOMPARE(canvas.renderedImage(), both); // Binding controls must not overwrite colour.
    canvas.undo(); QVERIFY(canvas.renderedImage() != both); // Only the second drawing is undone.
}

void AnnotationSelectionTest::pointerFeedback_data()
{
    QTest::addColumn<qreal>("scale");
    QTest::newRow("half") << qreal(0.5);
    QTest::newRow("native") << qreal(1);
    QTest::newRow("enlarged") << qreal(1.5);
}

void AnnotationSelectionTest::pointerFeedback()
{
    QFETCH(qreal, scale);
    Canvas canvas(background());
    canvas.setMinimumSize(0, 0);
    canvas.resize(qRound(640 * scale), qRound(420 * scale));
    canvas.show();
    const auto point = [scale](int x, int y) { return QPoint(qRound(x * scale), qRound(y * scale)); };
    drag(&canvas, point(80, 80), point(240, 180));
    const QList<QPair<QPoint, Qt::CursorShape>> checks{
        {{80,80},Qt::SizeFDiagCursor}, {{160,80},Qt::SizeVerCursor},
        {{240,80},Qt::SizeBDiagCursor}, {{240,130},Qt::SizeHorCursor},
        {{240,180},Qt::SizeFDiagCursor}, {{160,180},Qt::SizeVerCursor},
        {{80,180},Qt::SizeBDiagCursor}, {{80,130},Qt::SizeHorCursor},
        {{80,105},Qt::SizeAllCursor}, {{160,130},Qt::CrossCursor},
        {{160,64},Qt::OpenHandCursor}, {{300,300},Qt::CrossCursor}};
    for (const auto &[position, cursor] : checks)
    {
        move(&canvas, point(position.x(), position.y()));
        QCOMPARE(canvas.cursor().shape(), cursor);
    }
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, point(240, 180));
    QCOMPARE(canvas.cursor().shape(), Qt::SizeFDiagCursor);
    move(&canvas, point(260, 200), Qt::LeftButton);
    QCOMPARE(canvas.cursor().shape(), Qt::SizeFDiagCursor);
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, point(260, 200));
    const QImage rendered = canvas.renderedImage();
    const qint64 key = canvas.fullRenderedImage().cacheKey();
    QTest::keyClick(&canvas, Qt::Key_Escape);
    move(&canvas, point(80, 105));
    QCOMPARE(canvas.cursor().shape(), Qt::SizeAllCursor);
    canvas.repaint();
    QCOMPARE(canvas.renderedImage(), rendered);
    QCOMPARE(canvas.fullRenderedImage().cacheKey(), key);
}

void AnnotationSelectionTest::overlayKeepsAnnotationGesture()
{
    auto *screen = QGuiApplication::primaryScreen();
    std::unique_ptr<ui::SnipOverlay> overlay(ui::SnipOverlay::createPrepared(screen));
    overlay->setAutoSelectAnnotations(true);
    overlay->setBackgroundProvider([screen] {
        QImage image(screen->size(), QImage::Format_RGB32); image.fill(Qt::white); return image;
    });
    overlay->beginCapture(screen, false, false, QRect(40, 40, 320, 220));
    overlay->show();
    auto *canvas = overlay->findChild<Canvas *>(QStringLiteral("selectionCanvas"));
    QVERIFY(canvas);
    overlay->findChild<QToolButton *>(QStringLiteral("形状"))->click();
    drag(canvas, {15,40}, {180,160});
    const QRect originalSelection = canvas->geometry();
    drag(canvas, {15,100}, {4,100});
    move(canvas, {4,40});
    QCOMPARE(canvas->cursor().shape(), Qt::SizeFDiagCursor);
    QCOMPARE(overlay->cursor().shape(), Qt::SizeFDiagCursor);
    const QImage before = canvas->renderedImage();
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, {4, 40});
    QCOMPARE(canvas->cursor().shape(), Qt::SizeFDiagCursor);
    QCOMPARE(overlay->cursor().shape(), Qt::SizeFDiagCursor);
    move(canvas, {14, 50}, Qt::LeftButton);
    QCOMPARE(canvas->cursor().shape(), Qt::SizeFDiagCursor);
    QCOMPARE(overlay->cursor().shape(), Qt::SizeFDiagCursor);
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, {14, 50});
    QCOMPARE(canvas->geometry(), originalSelection);
    QVERIFY(canvas->renderedImage() != before);
    auto *options = static_cast<ui::AnnotationOptions *>(
        overlay->findChild<QFrame *>(QStringLiteral("captureOptions")));
    QVERIFY(options);
    auto *width = options->stack()->currentWidget()->findChild<QToolButton *>(QStringLiteral("penWidth4"));
    QVERIFY(width); width->click();
    const qreal oldWidth = canvas->penWidth();
    const QImage previous = canvas->renderedImage();
    wheel(width);
    QCOMPARE(canvas->penWidth(), oldWidth + 1);
    QVERIFY(canvas->renderedImage() != previous);
    const QString dir = qEnvironmentVariable("SNIPASTE_TEST_ARTIFACTS");
    if (!dir.isEmpty())
    {
        QDir().mkpath(dir);
        QVERIFY(overlay->grab().save(dir + QStringLiteral("/selection-cursor-and-style.png")));
    }
}

void AnnotationSelectionTest::lineEndpoints()
{
    Canvas canvas(background());
    canvas.resize(640, 420); canvas.show();
    canvas.setTool(Canvas::Tool::Line);
    drag(&canvas, {80,100}, {240,100});
    const QImage horizontal = canvas.renderedImage();
    drag(&canvas, {240,100}, {260,140});
    QVERIFY(canvas.renderedImage() != horizontal);
    canvas.undo(); QCOMPARE(canvas.renderedImage(), horizontal);
}

void AnnotationSelectionTest::smallRectangleEdgeYieldsToMove()
{
    Canvas canvas(background());
    canvas.resize(640, 420); canvas.show();
    // Draw a small 30x30 rectangle from (80, 80) to (110, 110)
    drag(&canvas, {80, 80}, {110, 110});

    // Four corners must be resize cursors (scaling)
    move(&canvas, {80, 80});
    QCOMPARE(canvas.cursor().shape(), Qt::SizeFDiagCursor);
    move(&canvas, {110, 80});
    QCOMPARE(canvas.cursor().shape(), Qt::SizeBDiagCursor);
    move(&canvas, {110, 110});
    QCOMPARE(canvas.cursor().shape(), Qt::SizeFDiagCursor);
    move(&canvas, {80, 110});
    QCOMPARE(canvas.cursor().shape(), Qt::SizeBDiagCursor);

    // Edges must yield to move cursor (SizeAllCursor)
    move(&canvas, {95, 80}); // top edge midpoint
    QCOMPARE(canvas.cursor().shape(), Qt::SizeAllCursor);
    move(&canvas, {110, 95}); // right edge midpoint
    QCOMPARE(canvas.cursor().shape(), Qt::SizeAllCursor);
    move(&canvas, {95, 110}); // bottom edge midpoint
    QCOMPARE(canvas.cursor().shape(), Qt::SizeAllCursor);
    move(&canvas, {80, 95}); // left edge midpoint
    QCOMPARE(canvas.cursor().shape(), Qt::SizeAllCursor);

    // Dragging an edge moves the shape without changing its size
    drag(&canvas, {95, 80}, {105, 90});
    QCOMPARE(canvas.document().shapes().size(), 1);
    const auto moved = canvas.document().shapes().first();
    QCOMPARE(qRound(moved.start.x()), 90);
    QCOMPARE(qRound(moved.start.y()), 90);
    QCOMPARE(qRound(moved.end.x()), 120);
    QCOMPARE(qRound(moved.end.y()), 120);

    // Dragging a corner resizes the shape
    drag(&canvas, {120, 120}, {130, 130});
    QCOMPARE(canvas.document().shapes().size(), 1);
    const auto resized = canvas.document().shapes().first();
    QCOMPARE(qRound(resized.start.x()), 90);
    QCOMPARE(qRound(resized.start.y()), 90);
    QCOMPARE(qRound(resized.end.x()), 130);
    QCOMPARE(qRound(resized.end.y()), 130);
}

void AnnotationSelectionTest::arrowDropdownGeometryAndItems()
{
    Canvas canvas(background());
    canvas.resize(640, 420);

    ui::AnnotationOptions options(nullptr, [] {});
    options.setCanvas(&canvas);

    canvas.setTool(Canvas::Tool::Arrow);

    auto *btnStartArrow = options.findChild<QToolButton *>(QStringLiteral("annotationStartArrowStyle"));
    QVERIFY(btnStartArrow);
    QCOMPARE(btnStartArrow->width(), 54);

    auto *menu = btnStartArrow->menu();
    QVERIFY(menu);
    QCOMPARE(menu->sizeHint(), QSize(54, 194));
    QCOMPARE(menu->minimumSize(), QSize(54, 194));
    QCOMPARE(menu->maximumSize(), QSize(54, 194));

    auto *listWidget = menu->findChild<QListWidget *>();
    QVERIFY(listWidget);
    QCOMPARE(listWidget->width(), 52);
    QCOMPARE(listWidget->height(), 192);
    QCOMPARE(listWidget->count(), 8);
    menu->popup(QPoint(0, 0));
    QApplication::processEvents();
    for (int index = 0; index < listWidget->count(); ++index)
        QVERIFY(listWidget->viewport()->rect().contains(listWidget->visualItemRect(listWidget->item(index))));
    const QString artifacts = qEnvironmentVariable("SNIPASTE_TEST_ARTIFACTS");
    if (!artifacts.isEmpty())
    {
        QVERIFY(QDir().mkpath(artifacts));
        QVERIFY(menu->grab().save(QDir(artifacts).filePath(QStringLiteral("arrow-dropdown.png"))));
    }

    // Item 8 is Hollow arrow
    auto *item8 = listWidget->item(7);
    QVERIFY(item8);
    QCOMPARE(item8->data(Qt::UserRole).toInt(), int(core::ArrowHead::Hollow));
    QCOMPARE(item8->sizeHint().height(), 24);

    // Select Item 8 and verify canvas update
    QTest::mouseClick(listWidget->viewport(), Qt::LeftButton, Qt::NoModifier,
                      listWidget->visualItemRect(item8).center());
    QCOMPARE(canvas.startArrowHead(), core::ArrowHead::Hollow);
}

void AnnotationSelectionTest::lineStyleMenusAndHistory()
{
    Canvas canvas(background());
    canvas.resize(640, 420);
    ui::AnnotationOptions options(nullptr, [] {});
    options.setCanvas(&canvas);
    canvas.show();
    for (const auto tool : {Canvas::Tool::Rectangle, Canvas::Tool::Line, Canvas::Tool::Pen})
        canvas.setTool(tool);
    const QList<Qt::PenStyle> expected{
        Qt::SolidLine, Qt::DashLine, Qt::DotLine, Qt::DashDotLine, Qt::DashDotDotLine};
    const auto menus = options.findChildren<QToolButton *>(QStringLiteral("annotationLineStyle"));
    QCOMPARE(menus.size(), 3); // Shapes, lines/arrows, pencil.
    for (auto *button : menus)
    {
        const auto actions = button->menu()->actions();
        QCOMPARE(actions.size(), expected.size());
        const QString artifacts = qEnvironmentVariable("SNIPASTE_TEST_ARTIFACTS");
        if (!artifacts.isEmpty() && button == menus.first())
        {
            QVERIFY(QDir().mkpath(artifacts));
            button->menu()->popup(QPoint(0, 0));
            QApplication::processEvents();
            QVERIFY(button->menu()->grab().save(QDir(artifacts).filePath(QStringLiteral("line-style-dropdown.png"))));
            button->menu()->close();
        }
        for (qsizetype index = 0; index < expected.size(); ++index)
        {
            QCOMPARE(actions.at(index)->data().toInt(), int(expected.at(index)));
            actions.at(index)->trigger();
            QCOMPARE(canvas.annotationStyle().penStyle, expected.at(index));
        }
    }
    canvas.setTool(Canvas::Tool::Pen);
    menus.last()->menu()->actions().last()->trigger();
    canvas.setTool(Canvas::Tool::Marker);
    canvas.setTool(Canvas::Tool::Pen);
    QCOMPARE(canvas.annotationStyle().penStyle, Qt::DashDotDotLine);
    canvas.setTool(Canvas::Tool::Line);
    canvas.setPenStyle(Qt::DashDotDotLine);
    drag(&canvas, {80, 80}, {500, 200});
    const QImage doubleDot = canvas.renderedImage();
    canvas.setPenStyle(Qt::DotLine);
    const QImage dotted = canvas.renderedImage();
    QVERIFY(dotted != doubleDot);
    canvas.undo();
    QCOMPARE(canvas.renderedImage(), doubleDot);
    QCOMPARE(canvas.document().shapes().first().penStyle, Qt::DashDotDotLine);
    // Undo clears selection; selecting the restored line synchronizes the controls.
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, {290, 140});
    QCOMPARE(canvas.annotationStyle().penStyle, Qt::DashDotDotLine);
    for (auto *button : menus)
        QVERIFY(button->menu()->actions().last()->isChecked());
    canvas.redo();
    QCOMPARE(canvas.renderedImage(), dotted);

    ui::AnnotationDialog dialog(background());
    auto *combo = dialog.findChild<QComboBox *>(QStringLiteral("annotationLineStyle"));
    QVERIFY(combo);
    QCOMPARE(combo->count(), expected.size());
    for (int index = 0; index < combo->count(); ++index)
        QCOMPARE(combo->itemData(index).toInt(), int(expected.at(index)));
    combo->setCurrentIndex(4);
    QCOMPARE(dialog.findChild<Canvas *>()->annotationStyle().penStyle, Qt::DashDotDotLine);
}

QTEST_MAIN(AnnotationSelectionTest)
#include "annotation_selection_test.moc"
