#include "core/recognition/ocr_service.h"
#include "core/recognition/ocr_image.h"
#include "platform/keyboard.h"
#include "ui/annotation/annotation_canvas.h"
#include "ui/recognition/ocr_overlay.h"
#include "ui/snip/snip_overlay.h"
#ifdef Q_OS_MACOS
#include "platform/macos/vision_ocr.h"
#endif
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFrame>
#include <QFontMetrics>
#include <QFutureWatcher>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QScopeGuard>
#include <QScreen>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>
#include <QWheelEvent>
#include <QtConcurrent/QtConcurrentRun>
#include <memory>

using namespace snipaste;

namespace {
QImage sourceImage()
{
    QImage image(600, 300, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    QFont font(QStringLiteral("Arial"));
    font.setPixelSize(24);
    painter.setFont(font);
    painter.setPen(Qt::black);
    painter.drawText(QRect(30, 40, 70, 24), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Hello"));
    painter.drawText(QRect(120, 40, 70, 24), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("world"));
    painter.drawText(QRect(30, 100, 112, 24), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("中文测试"));
    return image;
}

core::OcrResult recognition()
{
    return {true, QStringLiteral("Hello world\n中文测试"), {}, {
        {QRectF(30, 40, 70, 24), 0, 5, 0},
        {QRectF(120, 40, 70, 24), 6, 5, 0},
        {QRectF(30, 100, 112, 24), 12, 4, 1}}};
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
} // namespace

class OcrOverlayTest final : public QObject
{
    Q_OBJECT
private slots:
    void tesseractCoordinatesAndUtf8();
    void tesseractRejectsInvalidRows();
    void textSelection_data();
    void textSelection();
    void graphemeSelection();
    void clipboardAndPixels();
    void errorAndUnlocatedText();
    void lateCompletionIsIgnored();
    void captureSessionKeepsAnnotations();
    void windowsSystemOcr();
    void windowsPrintedText_data();
    void windowsPrintedText();
    void windowsUnavailableLanguage();
    void macVisionOcr_data();
    void macVisionOcr();
    void contrastKeepsSourceAndHighContrastImage();
    void recoveryPreservesOriginalCode();
};

void OcrOverlayTest::tesseractCoordinatesAndUtf8()
{
    const QByteArray tsv = QStringLiteral(
        "level\tpage_num\tblock_num\tpar_num\tline_num\tword_num\tleft\ttop\twidth\theight\tconf\ttext\r\n"
        "5\t1\t1\t1\t1\t1\t30\t40\t70\t24\t99\tHello\r\n"
        "5\t1\t1\t1\t1\t2\t120\t40\t70\t24\t98\tworld\r\n"
        "5\t1\t1\t1\t2\t1\t30\t100\t56\t24\t95\t中文\r\n"
        "5\t1\t1\t1\t2\t2\t86\t100\t56\t24\t96\t测试\r\n"
        "5\t1\t2\t1\t2\t1\t300\t100\t70\t24\t97\tOther\r\n").toUtf8();
    const auto result = core::OcrService::parseTesseractTsv(tsv);
    QVERIFY(result.success);
    QCOMPARE(result.text, QStringLiteral("Hello world\n中文测试\nOther"));
    QCOMPARE(result.boxes.size(), 5);
    QCOMPARE(result.boxes[1].bounds, QRectF(120, 40, 70, 24));
    QCOMPARE(result.boxes[1].start, 6);
    QCOMPARE(result.boxes[2].start, 12);
    QCOMPARE(result.boxes[3].start, 14);
    QCOMPARE(result.boxes[4].start, 17);
    QCOMPARE(result.boxes[4].line, 2);
    QCOMPARE(result.text.mid(result.boxes[3].start, result.boxes[3].length), QStringLiteral("测试"));
}

void OcrOverlayTest::tesseractRejectsInvalidRows()
{
    const auto result = core::OcrService::parseTesseractTsv(
        "4\t1\t1\t1\t1\t1\t30\t40\t70\t24\t99\tNot-a-word\n"
        "5\t1\t1\t1\t1\t1\t30\t40\t0\t24\t99\tEmpty-box\n"
        "5\t1\t1\t1\t1\t1\tx\t40\t70\t24\t99\tBad-number\n"
        "5\t1\t1\t1\t1\t1\t30\t40\t70\t24\t99\t\n"
        "5\ttoo-short\n");
    QVERIFY(!result.success);
    QVERIFY(result.text.isEmpty() && result.boxes.isEmpty());
    QVERIFY(!result.error.isEmpty());
}

void OcrOverlayTest::textSelection_data()
{
    QTest::addColumn<qreal>("scale");
    QTest::addColumn<qreal>("imageDpr");
    QTest::newRow("half-preview-high-dpi") << qreal(0.5) << qreal(2);
    QTest::newRow("native-preview") << qreal(1) << qreal(1);
    QTest::newRow("enlarged-preview-high-dpi") << qreal(1.5) << qreal(2);
}

void OcrOverlayTest::textSelection()
{
    QFETCH(qreal, scale);
    QFETCH(qreal, imageDpr);
    QImage image = sourceImage();
    image.setDevicePixelRatio(imageDpr);
    const QPointF origin(50, 100);
    const auto point = [=](qreal x, qreal y) {
        return (origin + QPointF(x * scale, y * scale)).toPoint();
    };
    ui::OcrOverlay overlay(image, QRectF(origin, QSizeF(600 * scale, 300 * scale)));
    overlay.resize(1050, 650);
    overlay.setResult(recognition());
    overlay.show();
    move(&overlay, point(50, 52));
    QCOMPARE(overlay.cursor().shape(), Qt::IBeamCursor);
    move(&overlay, point(250, 52));
    QCOMPARE(overlay.cursor().shape(), Qt::ArrowCursor);

    drag(&overlay, point(31, 52), point(189, 52));
    QCOMPARE(overlay.selectedText(), QStringLiteral("Hello world"));
    drag(&overlay, point(141, 112), point(31, 52));
    QCOMPARE(overlay.selectedText(), QStringLiteral("Hello world\n中文测试"));
    drag(&overlay, point(58, 112), point(114, 112));
    QCOMPARE(overlay.selectedText(), QStringLiteral("文测"));
    QTest::mouseDClick(&overlay, Qt::LeftButton, Qt::NoModifier, point(150, 52));
    QCOMPARE(overlay.selectedText(), QStringLiteral("world"));
    QTest::mouseClick(&overlay, Qt::LeftButton, Qt::ShiftModifier, point(141, 112));
    QCOMPARE(overlay.selectedText(), QStringLiteral("world\n中文测试"));
    QTest::mouseClick(&overlay, Qt::LeftButton, Qt::NoModifier, point(250, 200));
    QVERIFY(overlay.selectedText().isEmpty());
    QTest::keyClick(&overlay, Qt::Key_A, platform::primaryModifier());
    QCOMPARE(overlay.selectedText(), recognition().text);
    QSignalSpy exit(&overlay, &ui::OcrOverlay::exitRequested);
    QTest::keyClick(&overlay, Qt::Key_Escape);
    QCOMPARE(exit.count(), 1);
}

void OcrOverlayTest::graphemeSelection()
{
    QImage image(400, 100, QImage::Format_RGB32);
    image.fill(Qt::white);
    ui::OcrOverlay overlay(image, QRectF(30, 100, 400, 100));
    overlay.resize(800, 400);
    const QString text = QString::fromUtf8("e\xCC\x81"); // e + combining acute accent.
    overlay.setResult({true, text, {}, {{QRectF(30, 30, 120, 30), 0, text.size(), 0}}});
    overlay.show();
    drag(&overlay, {61, 145}, {150, 145});
    QCOMPARE(overlay.selectedText(), text); // Never split a combining sequence.
}

void OcrOverlayTest::clipboardAndPixels()
{
    const QImage image = sourceImage();
    const auto imageKey = image.cacheKey();
    ui::OcrOverlay overlay(image, QRectF(50, 100, 600, 300));
    overlay.resize(850, 500);
    QApplication::clipboard()->setText(QStringLiteral("previous clipboard"));
    overlay.setResult(recognition());
    overlay.show();
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("previous clipboard"));
    const QImage unselected = overlay.grab().toImage();
    drag(&overlay, {81, 152}, {239, 152});
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("previous clipboard"));
    auto *copy = overlay.findChild<QToolButton *>(QStringLiteral("ocrCopySelection"));
    QVERIFY(copy && copy->isEnabled());
    QTest::mouseClick(copy, Qt::LeftButton);
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("Hello world"));
    QTest::keyClick(&overlay, Qt::Key_A, platform::primaryModifier());
    QTest::keyClick(&overlay, Qt::Key_C, platform::primaryModifier());
    QCOMPARE(QApplication::clipboard()->text(), recognition().text);
    QTest::mouseClick(&overlay, Qt::LeftButton, Qt::NoModifier, {400, 300});
    QVERIFY(!copy->isEnabled());
    QTest::mouseClick(overlay.findChild<QToolButton *>(QStringLiteral("ocrCopyAll")), Qt::LeftButton);
    QCOMPARE(QApplication::clipboard()->text(), recognition().text);

    overlay.selectAll();
    const QImage selected = overlay.grab().toImage();
    QVERIFY(selected != unselected);
    const qreal dpr = selected.devicePixelRatio();
    QVERIFY(selected.pixelColor(qRound(300 * dpr), qRound(200 * dpr)).red() < 200); // Mask.
    QVERIFY(selected.pixelColor(qRound(82 * dpr), qRound(153 * dpr)).blue()
            > selected.pixelColor(qRound(82 * dpr), qRound(153 * dpr)).red()); // Selection tint.
    QCOMPARE(overlay.sourceImage(), image);
    QCOMPARE(overlay.sourceImage().cacheKey(), imageKey); // No detached source pixel copy.
    const QString artifacts = qEnvironmentVariable("SNIPASTE_TEST_ARTIFACTS");
    if (!artifacts.isEmpty()) {
        QVERIFY(QDir().mkpath(artifacts));
        QVERIFY(selected.save(artifacts + QStringLiteral("/ocr-selection.png")));
        QVERIFY(unselected.save(artifacts + QStringLiteral("/ocr-recognized.png")));
    }
}

void OcrOverlayTest::errorAndUnlocatedText()
{
    ui::OcrOverlay overlay(sourceImage(), QRectF(50, 100, 600, 300));
    overlay.resize(850, 500);
    overlay.setResult({false, {}, QStringLiteral("没有识别到文字")});
    overlay.show();
    auto *copyAll = overlay.findChild<QToolButton *>(QStringLiteral("ocrCopyAll"));
    auto *copy = overlay.findChild<QToolButton *>(QStringLiteral("ocrCopySelection"));
    auto *status = overlay.findChild<QLabel *>(QStringLiteral("ocrStatus"));
    QVERIFY(copyAll && copy && status);
    QVERIFY(!copyAll->isEnabled() && !copy->isEnabled());
    QCOMPARE(status->text(), QStringLiteral("没有识别到文字"));
    auto result = recognition();
    result.boxes = {{QRectF(0, 0, 10, 10), -1, 2, 0},
                    {QRectF(0, 0, 10, 10), 200, 3, 0},
                    {QRectF(700, 500, 10, 10), 0, 3, 0}};
    overlay.setResult(result);
    QVERIFY(copyAll->isEnabled());
    QVERIFY(status->text().contains(QStringLiteral("无法定位")));
    QApplication::clipboard()->setText(QStringLiteral("unchanged"));
    overlay.copyAll();
    QCOMPARE(QApplication::clipboard()->text(), result.text);
    QVERIFY(overlay.rect().contains(overlay.findChild<QFrame *>(QStringLiteral("ocrToolbar"))->geometry()));
}

void OcrOverlayTest::lateCompletionIsIgnored()
{
    ui::OcrOverlay overlay({}, QRectF(50, 100, 600, 300));
    overlay.resize(850, 500);
    QSignalSpy completed(&overlay, &ui::OcrOverlay::recognitionCompleted);
    overlay.startRecognition(); // Null image: deterministic asynchronous failure.
    auto *watcher = overlay.findChild<QFutureWatcherBase *>();
    QVERIFY(watcher);
    QSignalSpy finished(watcher, &QFutureWatcherBase::finished);
    overlay.setResult(recognition());
    QTRY_COMPARE(finished.count(), 1);
    overlay.selectAll();
    QCOMPARE(overlay.selectedText(), recognition().text);
    QCOMPARE(completed.count(), 1);
    QPointer<ui::OcrOverlay> temporary = new ui::OcrOverlay({}, {});
    temporary->startRecognition();
    delete temporary;
    QCoreApplication::processEvents();
    QVERIFY(temporary.isNull());
}

void OcrOverlayTest::captureSessionKeepsAnnotations()
{
    std::unique_ptr<ui::SnipOverlay> capture(ui::SnipOverlay::createPrepared(QGuiApplication::primaryScreen()));
    auto *preparedCanvas = capture->findChild<ui::AnnotationCanvas *>();
    auto *toolbar = capture->findChild<QFrame *>(QStringLiteral("captureToolbar"));
    QVERIFY(preparedCanvas && toolbar);
    QSignalSpy output(capture.get(), &ui::SnipOverlay::screenshotCaptured);
    QSignalSpy completed(capture.get(), &ui::SnipOverlay::ocrCompleted);
    QApplication::clipboard()->setText(QStringLiteral("clipboard before OCR"));
    for (int session = 0; session < 2; ++session) {
        capture->beginCapture(QGuiApplication::primaryScreen(), false, false, QRect(40, 80, 360, 220));
        capture->setExitOnFocusLoss(false);
        capture->setDetectionOptions(false, false, false);
        capture->show();
        QVERIFY(preparedCanvas->isVisible());
        preparedCanvas->setTool(ui::AnnotationCanvas::Tool::Rectangle);
        drag(preparedCanvas, {70, 60}, {200, 150});
        const QImage annotated = preparedCanvas->renderedImage();
        QVERIFY(preparedCanvas->canUndo());
        auto *button = capture->findChild<QToolButton *>(QStringLiteral("文字识别"));
        QVERIFY(button && button->isVisible());
        QTest::mouseClick(button, Qt::LeftButton);
        QPointer<ui::OcrOverlay> ocr = capture->findChild<ui::OcrOverlay *>();
        QVERIFY(ocr && ocr->isVisible());
        QVERIFY(capture->isVisible() && !toolbar->isVisible());
        QCOMPARE(output.count(), 0);
        ocr->setResult(recognition()); // Invalidate the pending engine result for deterministic interactions.
        QCOMPARE(ocr->sourceImage(), annotated);
        const int signalsBeforeExit = completed.count();
        QCoreApplication::processEvents();
        ocr->setFocus();
        QTRY_COMPARE(QApplication::focusWidget(), ocr.data());
        QTest::keyClick(ocr, Qt::Key_Tab);
        QCOMPARE(QApplication::focusWidget(), ocr.data());
        QWheelEvent wheel(QPointF(ocr->rect().center()), QPointF(ocr->mapToGlobal(ocr->rect().center())),
                          {}, QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        const qreal penWidth = preparedCanvas->penWidth();
        QApplication::sendEvent(ocr, &wheel);
        QCOMPARE(preparedCanvas->penWidth(), penWidth);
        QTest::keyClick(ocr, Qt::Key_Escape);
        QVERIFY(capture->isVisible() && toolbar->isVisible());
        QVERIFY(!ocr->isVisible());
        ocr->setResult(recognition()); // A completion before deferred deletion must not reach the app.
        QCOMPARE(completed.count(), signalsBeforeExit);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(ocr.isNull());
        QCOMPARE(preparedCanvas->renderedImage(), annotated);
        QVERIFY(preparedCanvas->canUndo());
        preparedCanvas->undo();
        QVERIFY(preparedCanvas->renderedImage() != annotated);
        preparedCanvas->redo();
        QCOMPARE(preparedCanvas->renderedImage(), annotated);
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("clipboard before OCR"));
        // Close directly during a second recognition, then reuse the prepared controls.
        QTest::mouseClick(button, Qt::LeftButton);
        ocr = capture->findChild<ui::OcrOverlay *>();
        QVERIFY(ocr);
        capture->close();
        QVERIFY(ocr.isNull());
        QVERIFY(preparedCanvas->fullRenderedImage().isNull());
        QCOMPARE(capture->findChild<ui::AnnotationCanvas *>(), preparedCanvas);
    }
}

void OcrOverlayTest::windowsSystemOcr()
{
#ifdef Q_OS_WIN
    QImage image(800, 180, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    QFont font(QStringLiteral("Arial"));
    font.setPixelSize(52);
    painter.setFont(font);
    painter.setPen(Qt::black);
    painter.drawText(QRect(40, 40, 700, 80), Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("Hello world"));
    painter.end();
    // Exclude a Tesseract fallback in this process: this check exercises WinRT.
    const QByteArray previousPath = qgetenv("PATH");
    const auto restorePath = qScopeGuard([&] { qputenv("PATH", previousPath); });
    qputenv("PATH", QByteArray());
    auto future = QtConcurrent::run([image] {
        return core::OcrService::recognize(image, {}, QStringLiteral("en"));
    });
    future.waitForFinished();
    const auto result = future.result();
    if (!result.success && result.error.contains(QStringLiteral("未安装")))
        QSKIP(qPrintable(result.error));
    QVERIFY2(result.success, qPrintable(result.error));
    QVERIFY2(result.text.contains(QStringLiteral("Hello"), Qt::CaseInsensitive), qPrintable(result.text));
    QVERIFY(!result.boxes.isEmpty());
    for (const auto &box : result.boxes) {
        QVERIFY(QRectF(image.rect()).contains(box.bounds));
        QVERIFY(!box.bounds.isEmpty());
        QVERIFY(box.start >= 0 && box.start + box.length <= result.text.size());
        QVERIFY(!result.text.mid(box.start, box.length).isEmpty());
    }
    qInfo() << "Windows.Media.Ocr:" << result.text << result.boxes.size() << "word boxes";
#else
    QSKIP("Windows system OCR is only available on Windows.");
#endif
}

void OcrOverlayTest::windowsPrintedText_data()
{
    QTest::addColumn<int>("pixels");
    QTest::addColumn<bool>("dark");
    QTest::addColumn<bool>("codePanel");
    QTest::newRow("chinese-white-14") << 14 << false << false;
    QTest::newRow("chinese-dark-14") << 14 << true << false;
    QTest::newRow("mixed-columns-dark-14") << 14 << true << true;
    QTest::newRow("mixed-columns-dark-18") << 18 << true << true;
}

void OcrOverlayTest::windowsPrintedText()
{
#ifdef Q_OS_WIN
    QFETCH(int, pixels);
    QFETCH(bool, dark);
    QFETCH(bool, codePanel);
    QImage image(700, 240, QImage::Format_RGB32);
    image.fill(dark ? QColor(26, 29, 33) : QColor(Qt::white));
    QPainter painter(&image);
    QFont font(QStringLiteral("Microsoft YaHei"));
    font.setPixelSize(pixels);
    const QFontMetrics metrics(font);
    for (const QChar character : QStringLiteral("搞啥呢哪个更好")) {
        if (!metrics.inFont(character))
            QSKIP("The printed Chinese fixture requires a font with Chinese glyphs.");
    }
    painter.setFont(font);
    painter.setPen(dark ? QColor(108, 112, 117) : QColor(Qt::black));
    painter.drawText(QPoint(4, 45), QStringLiteral("搞啥呢"));
    painter.drawText(QPoint(4, 145), QStringLiteral("哪个更好"));
    if (codePanel) {
        painter.fillRect(QRect(100, 20, 580, 200), QColor(31, 35, 41));
        font.setFamily(QStringLiteral("Consolas"));
        font.setPixelSize(16);
        painter.setFont(font);
        painter.setPen(QColor(210, 215, 225));
        painter.drawText(QRect(110, 25, 560, 190), Qt::AlignLeft | Qt::TextWordWrap,
                         QStringLiteral("Get-Content -Encoding UTF8 tests\n"
                                        "/ocr_overlay_test.cpp | Select-Object\n"
                                        "-Skip 315 -First 80\n"
                                        "rg -n 'ocrEngine|ocrLanguage|系统自带'\n"
                                        "Get-ChildItem -Name scratch/*ocr*\n"
                                        "-ErrorAction SilentlyContinue"));
    }
    painter.end();
    const auto previousPath = qgetenv("PATH");
    const auto restorePath = qScopeGuard([&] { qputenv("PATH", previousPath); });
    qputenv("PATH", {});
    auto future = QtConcurrent::run([image] {
        return core::OcrService::recognize(image, {}, QStringLiteral("zh"));
    });
    future.waitForFinished();
    const auto result = future.result();
    if (!result.success && result.error.contains(QStringLiteral("未安装")))
        QSKIP(qPrintable(result.error));
    qInfo().noquote() << result.text;
    const QString artifacts = qEnvironmentVariable("SNIPASTE_TEST_ARTIFACTS");
    if (!artifacts.isEmpty()) {
        QVERIFY(QDir().mkpath(artifacts));
        QVERIFY(image.save(artifacts + QStringLiteral("/printed-")
                           + QString::fromLatin1(QTest::currentDataTag()) + QStringLiteral(".png")));
        ui::OcrOverlay view(image, QRectF(20, 90, image.width(), image.height()));
        view.resize(780, 380);
        view.setResult(result);
        view.show();
        view.selectAll();
        QVERIFY(view.grab().save(artifacts + QStringLiteral("/recognized-")
                                + QString::fromLatin1(QTest::currentDataTag()) + QStringLiteral(".png")));
    }
    QVERIFY2(result.success, qPrintable(result.error));
    QString compact = result.text;
    compact.remove(QChar(' '));
    QVERIFY2(compact.contains(QStringLiteral("搞啥呢")), qPrintable(result.text));
    QVERIFY2(compact.contains(QStringLiteral("哪个更好")), qPrintable(result.text));
    for (const auto &entry : {qMakePair(QChar(u'搞'), 45), qMakePair(QChar(u'哪'), 145)}) {
        const auto index = result.text.indexOf(entry.first);
        bool positioned = false;
        for (const auto &box : result.boxes) {
            if (box.start <= index && box.start + box.length > index) {
                QVERIFY(qAbs(box.bounds.center().y() - (entry.second - pixels / 2.0)) < pixels);
                QVERIFY(box.bounds.center().x() < 90);
                positioned = true;
            }
        }
        QVERIFY(positioned);
    }
    if (codePanel)
        QVERIFY2(result.text.contains(QStringLiteral("Get-Content -Encoding UTF8 tests")), qPrintable(result.text));
    for (const auto &box : result.boxes) {
        QVERIFY(QRectF(image.rect()).contains(box.bounds));
        QVERIFY(box.start >= 0 && box.length > 0 && box.start + box.length <= result.text.size());
    }
#else
    QSKIP("Windows system OCR is only available on Windows.");
#endif
}

void OcrOverlayTest::windowsUnavailableLanguage()
{
#ifdef Q_OS_WIN
    QImage image(80, 40, QImage::Format_RGB32);
    image.fill(Qt::white);
    const QByteArray previousPath = qgetenv("PATH");
    const auto restorePath = qScopeGuard([&] { qputenv("PATH", previousPath); });
    qputenv("PATH", {}); // Keep the missing native-language error observable.
    for (int attempt = 0; attempt < 4; ++attempt) {
        auto future = QtConcurrent::run([image] {
            return core::OcrService::recognize(image, {}, QStringLiteral("zz-ZZ"));
        });
        future.waitForFinished();
        const auto result = future.result();
        QVERIFY(!result.success);
        QVERIFY2(result.error.contains(QStringLiteral("Windows OCR 未安装所选语言包")), qPrintable(result.error));
        QVERIFY(result.text.isEmpty() && result.boxes.isEmpty());
    }
#else
    QSKIP("Windows system OCR is only available on Windows.");
#endif
}

void OcrOverlayTest::macVisionOcr_data()
{
    QTest::addColumn<QString>("language");
    QTest::addColumn<QString>("secondLine");
    QTest::newRow("english") << QStringLiteral("en") << QStringLiteral("Native screenshot");
    QTest::newRow("mixed-chinese") << QString() << QStringLiteral("中文测试");
}

void OcrOverlayTest::macVisionOcr()
{
#ifdef Q_OS_MACOS
    QFETCH(QString, language);
    QFETCH(QString, secondLine);
    QImage image(900, 320, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    QFont font(QStringLiteral("PingFang SC"));
    font.setPixelSize(48);
    painter.setFont(font);
    painter.setPen(Qt::black);
    painter.drawText(QPoint(40, 90), QStringLiteral("Hello world"));
    painter.drawText(QPoint(40, 230), secondLine);
    painter.end();
    // Vision must return source pixel coordinates even for Retina metadata.
    image.setDevicePixelRatio(2);
    auto future = QtConcurrent::run([image, language] {
        return platform::recognizeWithMacVision(image, language);
    });
    future.waitForFinished();
    const auto result = future.result();
    QVERIFY2(result.success, qPrintable(result.error));
    QVERIFY2(result.text.contains(QStringLiteral("Hello")), qPrintable(result.text));
    QVERIFY2(result.text.contains(secondLine), qPrintable(result.text));
    QVERIFY(result.boxes.size() >= 2);
    for (const auto &box : result.boxes) {
        QVERIFY(!box.bounds.isEmpty());
        QVERIFY(QRectF(image.rect()).contains(box.bounds));
        QVERIFY(box.start >= 0 && box.length > 0 && box.start + box.length <= result.text.size());
        if (result.text.mid(box.start, box.length).contains(QStringLiteral("Hello")))
            QVERIFY(qAbs(box.bounds.center().y() - 70) < 35);
        if (result.text.mid(box.start, box.length).contains(secondLine))
            QVERIFY(qAbs(box.bounds.center().y() - 210) < 35);
    }
#else
    QSKIP("macOS Vision OCR requires macOS.");
#endif
}

void OcrOverlayTest::contrastKeepsSourceAndHighContrastImage()
{
    QImage image(128, 64, QImage::Format_RGB32);
    image.fill(Qt::white);
    {
        QPainter painter(&image);
        painter.fillRect(QRect(10, 10, 20, 20), Qt::black);
    }
    const auto originalKey = image.cacheKey();
    const QImage unchanged = core::detail::enhanceOcrContrast(image);
    QCOMPARE(unchanged.cacheKey(), originalKey);
    QCOMPARE(unchanged, image);
    QImage dark(128, 64, QImage::Format_RGB32);
    dark.fill(QColor(26, 29, 33));
    {
        QPainter painter(&dark);
        painter.fillRect(QRect(10, 10, 20, 20), QColor(108, 112, 117));
    }
    dark.setDevicePixelRatio(2);
    const auto darkKey = dark.cacheKey();
    const QImage improved = core::detail::enhanceOcrContrast(dark);
    QCOMPARE(improved.size(), dark.size());
    QCOMPARE(improved.devicePixelRatio(), dark.devicePixelRatio());
    QCOMPARE(improved.pixelColor(15, 15), QColor(Qt::black));
    QCOMPARE(improved.pixelColor(5, 5), QColor(Qt::white));
    QCOMPARE(dark.cacheKey(), darkKey);
    QCOMPARE(dark.pixelColor(15, 15), QColor(108, 112, 117));
}

void OcrOverlayTest::recoveryPreservesOriginalCode()
{
    QImage source(300, 120, QImage::Format_RGB32);
    source.fill(QColor(26, 29, 33));
    {
        QPainter painter(&source);
        painter.fillRect(QRect(30, 40, 60, 20), QColor(108, 112, 117));
        painter.fillRect(QRect(120, 40, 60, 20), QColor(210, 215, 225));
    }
    const core::OcrResult original{true, QStringLiteral("错字\nHello world"), {}, {
        {QRectF(30, 40, 60, 20), 0, 2, 0}, {QRectF(120, 40, 60, 20), 3, 11, 1}}};
    const core::OcrResult enhanced{true, QStringLiteral("正确\nHe110"), {}, {
        {QRectF(30, 40, 60, 20), 0, 2, 0}, {QRectF(120, 40, 60, 20), 3, 5, 1}}};
    const auto merged = core::detail::mergeLowContrastText(source, original, enhanced);
    QVERIFY(merged.success);
    QCOMPARE(merged.text, QStringLiteral("正确\nHello world"));
    QCOMPARE(merged.boxes.size(), 2);
    QCOMPARE(merged.boxes[1].start, 3);
    QCOMPARE(merged.boxes[1].length, 11);
    QCOMPARE(merged.boxes[1].line, 1);
    QCOMPARE(merged.boxes[1].bounds, original.boxes[1].bounds);
    const auto unchanged = core::detail::mergeLowContrastText(source, original, {false, {}, QStringLiteral("failed")});
    QCOMPARE(unchanged.text, original.text);
    // A missing region is added; an original row containing other words keeps
    // those words even when its dim prefix is replaced by the retry.
    auto sameRow = original;
    sameRow.text = QStringLiteral("错字 Hello world");
    sameRow.boxes[1].line = 0;
    QCOMPARE(core::detail::mergeLowContrastText(source, sameRow, enhanced).text,
             QStringLiteral("正确\nHello world"));
    auto missing = original;
    missing.text = QStringLiteral("Hello world");
    missing.boxes = {{QRectF(120, 40, 60, 20), 0, 11, 0}};
    QCOMPARE(core::detail::mergeLowContrastText(source, missing, enhanced).text,
             QStringLiteral("正确\nHello world"));
}

QTEST_MAIN(OcrOverlayTest)
#include "ocr_overlay_test.moc"
