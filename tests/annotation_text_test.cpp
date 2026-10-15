#include "platform/keyboard.h"
#include "ui/annotation/annotation_canvas.h"
#include "ui/annotation/annotation_options.h"
#include "ui/annotation/annotation_renderer.h"
#include "ui/annotation/annotation_selection_geometry.h"
#include "ui/snip/snip_overlay.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QLineF>
#include <QPlainTextEdit>
#include <QMouseEvent>
#include <QScrollBar>
#include <QPointer>
#include <QScreen>
#include <QSignalSpy>
#include <QStackedLayout>
#include <QTest>
#include <QToolButton>
#include <QVBoxLayout>

using namespace snipaste;

class AnnotationTextTest : public QObject
{
    Q_OBJECT
  private slots:
    void reselectMoveAndReedit();
    void toolbarKeepsEditingTransaction();
    void preserveLayersAndRotation();
    void noOpPreservesRedoAndAnchor();
    void outputWhileReediting_data();
    void outputWhileReediting();
    void multilineViewport_data();
    void multilineViewport();
    void textBoxResizeAndPointer();
    void textScaling_data();
    void textScaling();
    void textScalingGeometry();
};

void AnnotationTextTest::reselectMoveAndReedit()
{
    QImage image(640, 420, QImage::Format_RGB32);
    image.fill(Qt::white);
    ui::AnnotationCanvas canvas(image);
    canvas.resize(image.size());
    canvas.show();
    canvas.setTool(ui::AnnotationCanvas::Tool::Text);
    auto *editor = canvas.findChild<QPlainTextEdit *>(QStringLiteral("annotationTextEditor"));
    QVERIFY(editor);
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(80, 80));
    editor->setPlainText(QStringLiteral("Original 中文\nSecond line"));
    QTest::keyClick(editor, Qt::Key_Return, platform::primaryModifier());
    const QImage original = canvas.renderedImage();

    QTest::keyClick(&canvas, Qt::Key_Escape);
    QTest::mouseMove(&canvas, QPoint(120, 95));
    QCOMPARE(canvas.cursor().shape(), Qt::IBeamCursor);
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(120, 95));
    QVERIFY(editor->isVisible());
    canvas.finishTextInput();
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(120, 95));
    QMouseEvent selectOnFirstClick(QEvent::MouseMove, QPointF(180, 95),
                                   QPointF(canvas.mapToGlobal(QPoint(180, 95))),
                                   Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &selectOnFirstClick);
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(180, 95));
    QVERIFY(editor->textCursor().hasSelection());
    canvas.finishTextInput();
    // The border moves the object, while its interior edits characters.
    const QPoint border(100, 77);
    QTest::mouseMove(&canvas, border);
    QCOMPARE(canvas.cursor().shape(), Qt::SizeAllCursor);
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, border);
    QTest::mouseMove(&canvas, border + QPoint(50, 30));
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, border + QPoint(50, 30));
    const QImage moved = canvas.renderedImage();
    QVERIFY(moved != original);
    canvas.undo(); QCOMPARE(canvas.renderedImage(), original);
    canvas.redo(); QCOMPARE(canvas.renderedImage(), moved);

    QTest::mouseDClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(170, 125));
    QVERIFY(editor->isVisible());
    QCOMPARE(editor->toPlainText(), QStringLiteral("Original 中文\nSecond line"));
    QCOMPARE(canvas.renderedImage(false), moved);
    QCOMPARE(canvas.displayImage(), image); // Hide the old glyphs under the input editor.
    editor->selectAll();
    QTest::keyClicks(editor, "Replacement");
    QTest::keyClick(editor, Qt::Key_Return);
    QTest::keyClicks(editor, "Two lines");
    const QImage pending = canvas.renderedImage();
    QVERIFY(pending != moved);
    QTest::keyClick(editor, Qt::Key_Return, platform::primaryModifier());
    QCOMPARE(canvas.renderedImage(), pending);
    canvas.undo(); QCOMPARE(canvas.renderedImage(), moved);
    canvas.redo(); QCOMPARE(canvas.renderedImage(), pending);

    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(170, 125));
    QTest::keyClick(&canvas, Qt::Key_F2);
    QVERIFY(editor->isVisible());
    QCOMPARE(editor->toPlainText(), QStringLiteral("Replacement\nTwo lines"));
    editor->selectAll();
    QTest::keyClick(editor, Qt::Key_Backspace);
    QCOMPARE(canvas.renderedImage(), image);
    canvas.finishTextInput();
    QCOMPARE(canvas.renderedImage(), image);
    canvas.undo(); QCOMPARE(canvas.renderedImage(), pending);
    canvas.undo(); QCOMPARE(canvas.renderedImage(), moved);
    canvas.undo(); QCOMPARE(canvas.renderedImage(), original);
    canvas.undo(); QCOMPARE(canvas.renderedImage(), image);
}

void AnnotationTextTest::multilineViewport_data()
{
    QTest::addColumn<qreal>("scale");
    QTest::newRow("half-size-preview") << qreal(0.5);
    QTest::newRow("native-size") << qreal(1);
    QTest::newRow("enlarged-preview") << qreal(1.5);
}

void AnnotationTextTest::multilineViewport()
{
    QFETCH(qreal, scale);
    QImage image(1280, 840, QImage::Format_RGB32);
    image.fill(Qt::white);
    ui::AnnotationCanvas canvas(image);
    canvas.resize(qRound(image.width() * scale), qRound(image.height() * scale));
    canvas.show();
    canvas.setTool(ui::AnnotationCanvas::Tool::Text);
    auto *editor = canvas.findChild<QPlainTextEdit *>(QStringLiteral("annotationTextEditor"));
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(80, 80));
    QTest::keyClicks(editor, "First line");
    const QPoint originalAnchor = editor->pos() + editor->viewport()->pos();
    QTest::keyClick(editor, Qt::Key_Return);
    QTest::keyClicks(editor, "Second line");
    QTest::keyClick(editor, Qt::Key_Return);
    QCoreApplication::processEvents();
    QCOMPARE(editor->toPlainText(), QStringLiteral("First line\nSecond line\n"));
    QCOMPARE(editor->cursorForPosition(QPoint(0, 0)).blockNumber(), 0);
    QCOMPARE(editor->verticalScrollBar()->value(), 0);
    QCOMPARE(editor->pos() + editor->viewport()->pos(), originalAnchor);
    QVERIFY(editor->viewport()->rect().contains(editor->cursorRect()));
    const QImage visible = editor->viewport()->grab().toImage();
    int firstLinePixels = 0;
    const int lineHeight = qRound(editor->fontMetrics().height() * visible.devicePixelRatio());
    for (int y = 0; y < qMin(lineHeight, visible.height()); ++y)
        for (int x = 0; x < visible.width(); ++x) {
            const QColor color = visible.pixelColor(x, y);
            if (color.red() > 140 && color.green() < 140 && color.blue() < 140)
                ++firstLinePixels;
        }
    QVERIFY2(firstLinePixels > 8, "The first line must remain painted in the live editor after Enter.");
    const QImage pending = canvas.renderedImage();
    const QString artifacts = qEnvironmentVariable("SNIPASTE_TEST_ARTIFACTS");
    if (!artifacts.isEmpty()) {
        QDir().mkpath(artifacts);
        QVERIFY(canvas.grab().save(QDir(artifacts).filePath(QStringLiteral("text-multiline-%1.png").arg(scale))));
    }
    canvas.finishTextInput();
    QCOMPARE(canvas.renderedImage(), pending);
}

void AnnotationTextTest::textBoxResizeAndPointer()
{
    QImage image(640, 420, QImage::Format_RGB32);
    image.fill(Qt::white);
    ui::AnnotationCanvas canvas(image);
    canvas.resize(image.size());
    canvas.show();
    canvas.setTool(ui::AnnotationCanvas::Tool::Text);
    auto *editor = canvas.findChild<QPlainTextEdit *>(QStringLiteral("annotationTextEditor"));
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(80, 80));
    QTest::keyClicks(editor, "Text box content");
    const QFont originalFont = editor->font();
    const QSize originalSize = editor->size();
    const QPoint inside(20, 12);
    QTest::mouseMove(editor->viewport(), inside);
    QCOMPARE(editor->viewport()->cursor().shape(), Qt::IBeamCursor);
    const QPoint edge(1, editor->height() / 2);
    QTest::mouseMove(editor, edge);
    QCOMPARE(editor->cursor().shape(), Qt::SizeAllCursor);
    // The border moves the pending object without starting a second object.
    const QPoint previousPosition = editor->pos();
    QTest::mousePress(editor, Qt::LeftButton, Qt::NoModifier, edge);
    QMouseEvent moveBorder(QEvent::MouseMove, QPointF(edge + QPoint(25, 20)),
                           QPointF(editor->mapToGlobal(edge + QPoint(25, 20))),
                           Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(editor, &moveBorder);
    QTest::mouseRelease(editor, Qt::LeftButton, Qt::NoModifier, edge);
    QCOMPARE(editor->pos(), previousPosition + QPoint(25, 20));
    const QPoint start = editor->rect().bottomRight() - QPoint(1, 1);
    QTest::mouseMove(editor, start);
    QCOMPARE(editor->cursor().shape(), Qt::SizeFDiagCursor);
    QTest::mousePress(editor, Qt::LeftButton, Qt::NoModifier, start);
    QMouseEvent move(QEvent::MouseMove, QPointF(start + QPoint(90, 50)),
                     QPointF(editor->mapToGlobal(start + QPoint(90, 50))),
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(editor, &move);
    QTest::mouseRelease(editor, Qt::LeftButton, Qt::NoModifier, start + QPoint(90, 50));
    QVERIFY(editor->width() > originalSize.width() + 70);
    QVERIFY(editor->height() > originalSize.height());
    QVERIFY(editor->font().pixelSize() > originalFont.pixelSize());
    const QFont scaledFont = editor->font();
    QCOMPARE(editor->toPlainText(), QStringLiteral("Text box content"));
    const QImage pending = canvas.renderedImage();
    canvas.finishTextInput();
    QCOMPARE(canvas.renderedImage(), pending);
    const QRect textFrame = editor->geometry();
    for (const QPoint &borderPoint : {QPoint(textFrame.center().x(), textFrame.top()),
                                     QPoint(textFrame.center().x(), textFrame.bottom()),
                                     QPoint(textFrame.left(), textFrame.center().y()),
                                     QPoint(textFrame.right(), textFrame.center().y())}) {
        canvas.updatePointerCursor(borderPoint);
        QCOMPARE(canvas.cursor().shape(), Qt::SizeAllCursor);
    }
    canvas.undo(); QCOMPARE(canvas.renderedImage(), image);
    canvas.redo(); QCOMPARE(canvas.renderedImage(), pending);
    const QPoint textHit = editor->geometry().topLeft() + QPoint(24, 19);
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, textHit);
    QVERIFY(editor->isVisible());
    canvas.finishTextInput();
    const QPoint committedCorner = editor->geometry().bottomRight();
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, committedCorner);
    QTest::mouseMove(&canvas, committedCorner + QPoint(40, 25));
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, committedCorner + QPoint(40, 25));
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, textHit);
    QVERIFY(editor->isVisible());
    QVERIFY(editor->font().pixelSize() > scaledFont.pixelSize());
    QVERIFY(editor->width() > originalSize.width() + 110);
    canvas.finishTextInput();
    canvas.undo();
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, textHit);
    QVERIFY(editor->isVisible());
    QVERIFY(editor->width() < originalSize.width() + 110);
    QCOMPARE(editor->font(), scaledFont);
    QTest::mousePress(editor->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(10, 12));
    QMouseEvent selectText(QEvent::MouseMove, QPointF(70, 12),
                           QPointF(editor->viewport()->mapToGlobal(QPoint(70, 12))),
                           Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(editor->viewport(), &selectText);
    QTest::mouseRelease(editor->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(70, 12));
    QVERIFY(editor->textCursor().hasSelection());
    canvas.finishTextInput();
    QTest::keyClick(&canvas, Qt::Key_Escape);
    QTest::mouseMove(&canvas, textHit);
    QCOMPARE(canvas.cursor().shape(), Qt::IBeamCursor);
    const QString artifacts = qEnvironmentVariable("SNIPASTE_TEST_ARTIFACTS");
    if (!artifacts.isEmpty()) {
        QDir().mkpath(artifacts);
        QVERIFY(canvas.grab().save(QDir(artifacts).filePath(QStringLiteral("text-hover-frame.png"))));
    }
    QCOMPARE(canvas.renderedImage(), pending); // Hover borders are never exported.
}

void AnnotationTextTest::textScaling_data()
{
    QTest::addColumn<qreal>("scale");
    QTest::newRow("half") << qreal(0.5);
    QTest::newRow("native") << qreal(1);
    QTest::newRow("enlarged") << qreal(1.5);
}

void AnnotationTextTest::textScaling()
{
    QFETCH(qreal, scale);
    QImage image(1280, 840, QImage::Format_RGB32);
    image.fill(Qt::white);
    ui::AnnotationCanvas canvas(image);
    ui::AnnotationOptions options(nullptr, [] {});
    options.setCanvas(&canvas);
    canvas.resize(qRound(image.width() * scale), qRound(image.height() * scale));
    canvas.show();
    canvas.setTool(ui::AnnotationCanvas::Tool::Text);
    canvas.setTextFontSize(20);
    auto *editor = canvas.findChild<QPlainTextEdit *>(QStringLiteral("annotationTextEditor"));
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(80, 80));
    const QString text = QStringLiteral("First line of text\nSecond line of text");
    editor->setPlainText(text);
    QCoreApplication::processEvents();
    const QFont originalFont = editor->font();
    const auto dragCorner = [editor](const QList<QPoint> &deltas) {
        const QPoint corner = editor->rect().bottomRight() - QPoint(1, 1);
        QPoint pointer = editor->mapToGlobal(corner);
        QTest::mousePress(editor, Qt::LeftButton, Qt::NoModifier, corner);
        for (const QPoint &delta : deltas) {
            pointer += delta;
            QMouseEvent event(QEvent::MouseMove, QPointF(editor->mapFromGlobal(pointer)), QPointF(pointer),
                              Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(editor, &event);
            QCoreApplication::processEvents();
        }
        QTest::mouseRelease(editor, Qt::LeftButton, Qt::NoModifier, editor->mapFromGlobal(pointer));
        QCoreApplication::processEvents();
    };
    const QSize originalSize = editor->size();
    const QPoint originalPosition = editor->pos();
    dragCorner({QPoint(originalSize.width() / 4, originalSize.height() / 4),
                QPoint(originalSize.width() / 4, originalSize.height() / 4)});
    QVERIFY(editor->font().pixelSize() > originalFont.pixelSize());
    QVERIFY(editor->width() > originalSize.width());
    QVERIFY(editor->height() > originalSize.height());
    QCOMPARE(editor->pos(), originalPosition);
    QCOMPARE(editor->lineWrapMode(), QPlainTextEdit::NoWrap);
    QCOMPARE(editor->document()->lineCount(), 2);
    QCOMPARE(editor->toPlainText(), text);
    const QFont scaledFont = editor->font();
    auto *sizeControl = options.findChild<QComboBox *>(QStringLiteral("annotationFontSize"));
    QVERIFY(sizeControl);
    QVERIFY(qAbs(sizeControl->currentText().toDouble() * 4 + 4 - canvas.annotationStyle().fontSize) < 0.001);
    const QSize scaledSize = editor->size();
    const QImage enlarged = canvas.renderedImage();
    canvas.finishTextInput();
    QCOMPARE(canvas.renderedImage(), enlarged);
    QTest::keyClick(&canvas, Qt::Key_F2);
    QVERIFY(editor->isVisible());
    QCOMPARE(editor->font(), scaledFont);
    QCOMPARE(editor->size(), scaledSize);
    dragCorner({QPoint(-scaledSize.width() / 3, -scaledSize.height() / 3)});
    QVERIFY(editor->font().pixelSize() < scaledFont.pixelSize());
    QVERIFY(editor->width() < scaledSize.width());
    QVERIFY(editor->height() < scaledSize.height());
    QCOMPARE(editor->toPlainText(), text);
    QCOMPARE(editor->document()->lineCount(), 2);
    QCOMPARE(editor->verticalScrollBar()->value(), 0);
    QVERIFY(editor->viewport()->rect().contains(editor->cursorRect()));
    const QFont shrunkFont = editor->font();
    QVERIFY(qAbs(sizeControl->currentText().toDouble() * 4 + 4 - canvas.annotationStyle().fontSize) < 0.001);
    const QImage shrunk = canvas.renderedImage();
    QVERIFY(shrunk != enlarged);
    canvas.finishTextInput();
    QCOMPARE(canvas.renderedImage(), shrunk);
    canvas.undo();
    QCOMPARE(canvas.renderedImage(), enlarged);
    canvas.redo();
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier,
                      originalPosition + editor->viewport()->pos() + QPoint(10, 8));
    QVERIFY(editor->isVisible());
    QCOMPARE(editor->font(), shrunkFont);
}

void AnnotationTextTest::textScalingGeometry()
{
    for (const int handle : {0, 2, 4, 6}) {
        core::AnnotationShape shape;
        shape.tool = core::AnnotationTool::Text;
        shape.start = shape.end = QPointF(80, 80);
        shape.text = QStringLiteral("First line of text\nSecond line of text");
        shape.fontSize = 20;
        shape.rotation = 27;
        const QRectF original = ui::annotation::selectionRect(shape);
        const QPointF fixedCorner = ui::annotation::selectionTransform(shape).map(
            ui::annotation::selectionHandles(original)[(handle + 4) % 8]);
        const auto scaleText = [&shape, handle](qreal factor) {
            const auto handles = ui::annotation::selectionHandles(ui::annotation::selectionRect(shape));
            const QPointF localDelta = (handles[handle] - handles[(handle + 4) % 8]) * (factor - 1);
            const QTransform transform = ui::annotation::selectionTransform(shape);
            ui::annotation::resizeShape(shape, handle, transform.map(localDelta) - transform.map(QPointF()));
        };
        scaleText(1.5);
        QVERIFY(qAbs(shape.fontSize - 30) < 0.01);
        QVERIFY(ui::annotation::selectionRect(shape).width() > original.width());
        QVERIFY(ui::annotation::selectionRect(shape).height() > original.height());
        scaleText(2.0 / 3.0);
        QVERIFY(qAbs(ui::annotation::selectionRect(shape).height() - original.height()) < 0.01);
        const QPointF restoredCorner = ui::annotation::selectionTransform(shape).map(
            ui::annotation::selectionHandles(ui::annotation::selectionRect(shape))[(handle + 4) % 8]);
        QVERIFY(QLineF(fixedCorner, restoredCorner).length() < 0.01);
        QVERIFY(qAbs(shape.fontSize - 20) < 0.01);
        scaleText(100);
        QCOMPARE(shape.fontSize, qreal(96));
        scaleText(-1);
        QCOMPARE(shape.fontSize, qreal(6));
    }
}

void AnnotationTextTest::toolbarKeepsEditingTransaction()
{
    QImage image(640, 420, QImage::Format_RGB32);
    image.fill(Qt::white);
    QWidget window;
    auto *layout = new QVBoxLayout(&window);
    auto *canvas = new ui::AnnotationCanvas(image, &window);
    auto *options = new ui::AnnotationOptions(&window, [] {});
    layout->addWidget(canvas);
    layout->addWidget(options);
    options->setCanvas(canvas);
    window.show();
    canvas->setTool(ui::AnnotationCanvas::Tool::Text);
    auto *editor = canvas->findChild<QPlainTextEdit *>(QStringLiteral("annotationTextEditor"));
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(80, 80));
    QTest::keyClicks(editor, "Style original");
    canvas->finishTextInput();
    const QImage original = canvas->renderedImage();
    QTest::keyClick(canvas, Qt::Key_F2);
    QVERIFY(editor->isVisible());

    auto *size = options->findChild<QComboBox *>(QStringLiteral("annotationFontSize"));
    auto *family = options->findChild<QComboBox *>(QStringLiteral("annotationFontFamily"));
    auto *bold = options->findChild<QToolButton *>(QStringLiteral("annotationTextBold"));
    auto *italic = options->findChild<QToolButton *>(QStringLiteral("annotationTextItalic"));
    auto *outline = options->findChild<QToolButton *>(QStringLiteral("annotationTextOutline"));
    auto *background = options->findChild<QToolButton *>(QStringLiteral("annotationTextBackground"));
    QVERIFY(size && family && bold && italic && outline && background);
    QTest::mouseClick(size, Qt::LeftButton);
    QTRY_VERIFY(size->view()->isVisible());
    QTest::keyClick(size->view(), Qt::Key_End);
    QTest::keyClick(size->view(), Qt::Key_Return);
    QVERIFY(editor->isVisible());
    QCOMPARE(editor->toPlainText(), QStringLiteral("Style original"));
    QCOMPARE(canvas->annotationStyle().fontSize, qreal(44));
    family->setCurrentText(QStringLiteral("Arial"));
    QTest::mouseClick(bold, Qt::LeftButton);
    QTest::mouseClick(italic, Qt::LeftButton);
    QTest::mouseClick(background, Qt::LeftButton);
    const QImage withoutOutline = canvas->renderedImage();
    QTest::mouseClick(outline, Qt::LeftButton);
    QVERIFY(canvas->renderedImage() != withoutOutline);
    auto *blue = options->stack()->currentWidget()->findChild<QToolButton *>(QStringLiteral("annotationColor_00a2e8"));
    QVERIFY(blue);
    QTest::mouseClick(blue, Qt::LeftButton);
    QVERIFY(editor->isVisible());
    QVERIFY(editor->font().bold() && editor->font().italic());
    QCOMPARE(editor->font().family(), QStringLiteral("Arial"));
    QVERIFY(!editor->extraSelections().isEmpty());
    QVERIFY(editor->extraSelections().first().format.textOutline().widthF() > 0);
    QCOMPARE(canvas->renderedImage(false), original);

    size->setFocusPolicy(Qt::StrongFocus);
    size->setFocus();
    QCoreApplication::processEvents();
    QVERIFY(editor->isVisible());
    editor->setFocus();
    editor->moveCursor(QTextCursor::End);
    QTest::keyClicks(editor, " extended");
    editor->undo();
    QCOMPARE(editor->toPlainText(), QStringLiteral("Style original"));
    QVERIFY(editor->font().bold() && editor->font().italic());
    editor->redo();
    QCOMPARE(editor->toPlainText(), QStringLiteral("Style original extended"));
    const QImage pending = canvas->renderedImage();
    const QString artifactDir = qEnvironmentVariable("SNIPASTE_TEST_ARTIFACTS");
    if (!artifactDir.isEmpty()) {
        QDir().mkpath(artifactDir);
        QVERIFY(window.grab().save(QDir(artifactDir).filePath(QStringLiteral("text-reediting-styles.png"))));
    }
    QTest::keyClick(editor, Qt::Key_Escape);
    QCOMPARE(canvas->renderedImage(), original);
    QVERIFY(!bold->isChecked() && !italic->isChecked() && !outline->isChecked());
    QTest::keyClick(canvas, Qt::Key_F2);
    QCOMPARE(editor->toPlainText(), QStringLiteral("Style original"));
    canvas->setTextFontFamily(QStringLiteral("Arial"));
    canvas->setTextFontSize(44);
    canvas->setTextBold(true);
    canvas->setTextItalic(true);
    canvas->setTextBackground(true);
    canvas->setTextOutlineWidth(1.5);
    canvas->setPenColor(QColor(QStringLiteral("#00a2e8")));
    editor->moveCursor(QTextCursor::End);
    QTest::keyClicks(editor, " extended");
    QCOMPARE(canvas->renderedImage(), pending);
    canvas->finishTextInput();
    QCOMPARE(canvas->renderedImage(), pending);
    canvas->undo(); QCOMPARE(canvas->renderedImage(), original);
    canvas->redo(); QCOMPARE(canvas->renderedImage(), pending);
}

void AnnotationTextTest::preserveLayersAndRotation()
{
    QImage image(640, 420, QImage::Format_RGB32);
    image.fill(Qt::white);
    ui::AnnotationCanvas canvas(image);
    canvas.resize(image.size());
    canvas.show();
    canvas.setTool(ui::AnnotationCanvas::Tool::Text);
    auto *editor = canvas.findChild<QPlainTextEdit *>(QStringLiteral("annotationTextEditor"));
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(80, 100));
    QTest::keyClicks(editor, "Layer original text");
    canvas.finishTextInput();
    QTest::keyClick(&canvas, Qt::Key_Right, Qt::AltModifier);
    const QImage rotated = canvas.renderedImage();
    canvas.setTool(ui::AnnotationCanvas::Tool::Rectangle);
    canvas.setPenColor(Qt::blue);
    canvas.setShapeFilled(true);
    // Start outside the existing text so free selection does not drag it.
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(150, 60));
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(250, 150));
    const QImage layered = canvas.renderedImage();
    QCOMPARE(layered.pixelColor(190, 120), QColor(Qt::blue));
    canvas.setTool(ui::AnnotationCanvas::Tool::Text);
    QTest::mouseDClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(110, 100));
    QVERIFY(editor->isVisible());
    QCOMPARE(editor->toPlainText(), QStringLiteral("Layer original text"));
    QCOMPARE(canvas.renderedImage(), layered);
    editor->setPlainText(QStringLiteral("Layer changed content"));
    const QImage pending = canvas.renderedImage();
    QVERIFY(pending != layered);
    QCOMPARE(pending.pixelColor(190, 120), QColor(Qt::blue));
    canvas.finishTextInput();
    QCOMPARE(canvas.renderedImage(), pending);
    canvas.undo(); QCOMPARE(canvas.renderedImage(), layered);
    canvas.undo(); QCOMPARE(canvas.renderedImage(), rotated);
    canvas.redo(); QCOMPARE(canvas.renderedImage(), layered);
    canvas.redo(); QCOMPARE(canvas.renderedImage(), pending);
}

void AnnotationTextTest::noOpPreservesRedoAndAnchor()
{
    QImage image(640, 420, QImage::Format_RGB32);
    image.fill(Qt::white);
    ui::AnnotationCanvas canvas(image);
    canvas.resize(image.size());
    canvas.show();
    canvas.setTool(ui::AnnotationCanvas::Tool::Text);
    auto *editor = canvas.findChild<QPlainTextEdit *>(QStringLiteral("annotationTextEditor"));
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(620, 400));
    QTest::keyClicks(editor, "Edge original");
    const QPoint hit = editor->geometry().center();
    canvas.finishTextInput();
    const QImage original = canvas.renderedImage();
    canvas.setTextBold(true);
    const QImage bold = canvas.renderedImage();
    canvas.undo();
    QVERIFY(canvas.canRedo());
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, hit);
    for (int i = 0; i < 3; ++i) {
        QTest::keyClick(&canvas, Qt::Key_F2);
        QVERIFY(editor->isVisible());
        QCOMPARE(canvas.renderedImage(), original);
        QCOMPARE(canvas.renderedImage(false), original);
        QVERIFY(canvas.rect().contains(editor->geometry()));
        canvas.finishTextInput();
        QCOMPARE(canvas.renderedImage(), original);
        QVERIFY(canvas.canRedo());
    }
    QTest::keyClick(&canvas, Qt::Key_F2);
    editor->setPlainText(QStringLiteral("Cancelled"));
    canvas.setTextFontSize(44);
    canvas.finishTextInput(false);
    QCOMPARE(canvas.renderedImage(), original);
    QVERIFY(canvas.canRedo());
    canvas.redo(); QCOMPARE(canvas.renderedImage(), bold);
    canvas.undo(); QCOMPARE(canvas.renderedImage(), original);
    canvas.undo(); QCOMPARE(canvas.renderedImage(), image);
}

void AnnotationTextTest::outputWhileReediting_data()
{
    QTest::addColumn<QString>("action");
    QTest::newRow("copy") << QStringLiteral("完成");
    QTest::newRow("pin") << QStringLiteral("贴图");
    QTest::newRow("global-paste") << QStringLiteral("global-paste");
}

void AnnotationTextTest::outputWhileReediting()
{
    QPointer<ui::SnipOverlay> overlay = new ui::SnipOverlay(QGuiApplication::primaryScreen(),
                                                         false, false, QRect(30, 30, 300, 220));
    overlay->show();
    QSignalSpy captured(overlay, &ui::SnipOverlay::screenshotCaptured);
    QSignalSpy pasted(overlay, &ui::SnipOverlay::pasteRequested);
    auto *canvas = overlay->findChild<ui::AnnotationCanvas *>();
    auto *editor = canvas->findChild<QPlainTextEdit *>(QStringLiteral("annotationTextEditor"));
    QVERIFY(editor);
    QTest::mouseClick(overlay->findChild<QToolButton *>(QStringLiteral("形状")), Qt::LeftButton);
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(260, 190));
    QTest::mouseClick(overlay->findChild<QToolButton *>(QStringLiteral("文字")), Qt::LeftButton);
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(25, 25));
    QTest::keyClicks(editor, "Keep pending text");
    canvas->finishTextInput();
    QTest::mouseDClick(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(40, 30));
    QVERIFY(editor->isVisible());
    QCOMPARE(editor->toPlainText(), QStringLiteral("Keep pending text"));
    editor->selectAll();
    QTest::keyClicks(editor, "Changed pending text");
    QTest::keyClick(editor, Qt::Key_Return);
    QTest::keyClicks(editor, "Next line stays visible");
    QCoreApplication::processEvents();
    QCOMPARE(editor->verticalScrollBar()->value(), 0);
    QCOMPARE(editor->cursorForPosition(QPoint(0, 0)).blockNumber(), 0);
    const qint64 backgroundKey = canvas->displayImage().cacheKey();
    for (int i = 0; i < 3; ++i) {
        overlay->repaint();
        canvas->repaint();
        QTest::keyClicks(editor, ".");
        QCoreApplication::processEvents();
        QCOMPARE(canvas->displayImage().cacheKey(), backgroundKey);
    }
    const QImage pending = canvas->renderedImage();
    QFETCH(QString, action);
    if (action == QStringLiteral("global-paste")) overlay->pasteSelection();
    else QTest::mouseClick(overlay->findChild<QToolButton *>(action), Qt::LeftButton);
    QCOMPARE(captured.count(), 1);
    QCOMPARE(qvariant_cast<QImage>(captured.first().first()), pending);
    if (action == QStringLiteral("完成")) QCOMPARE(QApplication::clipboard()->image(), pending);
    else {
        QCOMPARE(pasted.count(), 1);
        QCOMPARE(qvariant_cast<QImage>(pasted.first().first()), pending);
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(overlay.isNull());
}

QTEST_MAIN(AnnotationTextTest)
#include "annotation_text_test.moc"
