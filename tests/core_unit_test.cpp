#include "core/annotation/annotation_document.h"
#include "core/command/argument_parser.h"
#include "core/config/config_store.h"
#include "core/history/screenshot_history.h"
#include "core/history/screenshot_history_writer.h"
#include "core/paste/paste_state_store.h"
#include "core/scroll_stitcher.h"
#include "core/scroll_capture_stability.h"

#include <QColorSpace>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <atomic>

using namespace snipaste::core;

class CoreUnitTest final : public QObject
{
    Q_OBJECT

  private slots:
    void parsesGeometrySeparators();
    void rejectsMalformedGeometry();
    void normalizesMultiValueOptions();
    void expandsOutputPatterns();
    void persistsHistoryWithoutRenamingExistingFiles();
    void historyGeometryStaysWithEachCapture();
    void loadsLegacyHistoryWithoutInventingGeometry();
    void historyWriterCoalescesPendingImages();
    void configLookupCachePreservesCompatibility();
    void stitchesVerticalScrollFrames();
    void scrollStripsPreservePixelsAndSnapshots();
    void scrollStripsHandleLimitsAndNormalization();
    void scrollMatchesSmallAndFastMovements_data();
    void scrollMatchesSmallAndFastMovements();
    void scrollRejectsGapsAndRecovers();
    void scrollBottomDoesNotDuplicatePixels();
    void scrollRejectsAmbiguousContent();
    void scrollMatchesSparsePageContent();
    void scrollMatchesContentBetweenSampleColumns();
    void scrollMatchesLowContrastContent();
    void scrollMatchesWithSmallPixelChanges();
    void scrollCaptureWaitsForStableFrames();
    void annotationGesturesAndHistory();
    void annotationTranslationPreservesRedo();
    void pasteRestoreSharesSourcePixels();
};

void CoreUnitTest::pasteRestoreSharesSourcePixels()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QImage image(64, 48, QImage::Format_RGB32);
    image.fill(Qt::green);
    PasteSessionState state;
    PasteGroupState group;
    group.name = QStringLiteral("shared source");
    group.windows.append({image, QJsonObject{{QStringLiteral("x"), 11}}, 0, {}});
    group.windows.append({image, QJsonObject{{QStringLiteral("x"), 37}}, 0, {}});
    state.groups.append(group);
    QVERIFY(PasteStateStore::save(state, directory.path()));
    auto restored = PasteStateStore::load(directory.path());
    QVERIFY(restored.has_value());
    QCOMPARE(restored->groups.size(), 1);
    auto &windows = restored->groups.first().windows;
    QCOMPARE(windows.size(), 2);
    QCOMPARE(windows.first().image.cacheKey(), windows.last().image.cacheKey());
    QCOMPARE(windows.first().options.value(QStringLiteral("x")).toInt(), 11);
    QCOMPARE(windows.last().options.value(QStringLiteral("x")).toInt(), 37);
    windows.first().image.fill(Qt::cyan);
    QCOMPARE(windows.last().image, image);
    QVERIFY(windows.first().image != windows.last().image);
}

void CoreUnitTest::annotationGesturesAndHistory()
{
    AnnotationDocument document;
    QVERIFY(!document.commit());
    AnnotationShape shape;
    shape.start = {10, 20};
    shape.end = {50, 60};
    const int index = document.append(shape);
    QVERIFY(document.commit());
    const auto revision = document.revision();
    // Multiple pointer moves form a single history operation.
    document.editShape(index, [](auto &value) { value.end = {60, 70}; });
    document.editShape(index, [](auto &value) { value.end = {90, 100}; });
    QVERIFY(document.revision() > revision);
    QVERIFY(document.commit());
    QVERIFY(document.undo());
    QCOMPARE(document.shapes().at(index).end, shape.end);
    QVERIFY(document.redo());
    QCOMPARE(document.shapes().at(index).end, QPointF(90, 100));
    const auto unchanged = document.revision();
    QVERIFY(!document.remove(-1));
    QVERIFY(!document.editShape(9, [](auto &value) { value.end = {}; }));
    QCOMPARE(document.revision(), unchanged);
    QVERIFY(!document.commit());
    QVERIFY(document.undo());
    QVERIFY(document.remove(index));
    QVERIFY(document.commit());
    QVERIFY(!document.canRedo());
    QVERIFY(document.shapes().isEmpty());
    QVERIFY(document.undo());
    QCOMPARE(document.shapes().size(), 1);
}

void CoreUnitTest::annotationTranslationPreservesRedo()
{
    AnnotationDocument document;
    AnnotationShape shape;
    shape.start = {10, 20};
    shape.end = {50, 60};
    shape.text = QString::number(document.takeNumber());
    document.append(shape);
    QVERIFY(document.commit());
    document.append(shape);
    QVERIFY(document.commit());
    QVERIFY(document.undo());
    document.translate({-5, 7});
    QCOMPARE(document.shapes().first().start, QPointF(5, 27));
    QVERIFY(document.redo());
    QCOMPARE(document.shapes().size(), 2);
    QCOMPARE(document.shapes().last().end, QPointF(45, 67));
    QVERIFY(document.undo());
    QVERIFY(document.undo());
    QVERIFY(document.shapes().isEmpty());
    QCOMPARE(document.nextNumber(), 1);
}

void CoreUnitTest::parsesGeometrySeparators()
{
    QCOMPARE(parseIntegerList(QStringLiteral("10,20;30x40")), QList<int>({10, 20, 30, 40}));
    QCOMPARE(parseRectSpec(QStringLiteral("-10 20 300 400")), QRect(-10, 20, 300, 400));
    QCOMPARE(parseSizeSpec(QStringLiteral("1920x1080")), QSize(1920, 1080));
    QCOMPARE(parsePointSpec(QStringLiteral("-4;8")), QPoint(-4, 8));
}

void CoreUnitTest::rejectsMalformedGeometry()
{
    QVERIFY(parseIntegerList(QStringLiteral("10,broken")).isEmpty());
    QVERIFY(parseRectSpec(QStringLiteral("10,20,30")).isNull());
    QVERIFY(parseSizeSpec(QStringLiteral("10,20,30")).isEmpty());
    QVERIFY(parsePointSpec(QStringLiteral("10")).isNull());
}

void CoreUnitTest::normalizesMultiValueOptions()
{
    const QStringList input{QStringLiteral("snipaste"), QStringLiteral("snip"),   QStringLiteral("--area"),
                            QStringLiteral("-10"),      QStringLiteral("20"),     QStringLiteral("300"),
                            QStringLiteral("400"),      QStringLiteral("--block")};
    const QStringList normalized = normalizeMultiValueOptions(input);
    const QStringList expected{QStringLiteral("snipaste"), QStringLiteral("snip"),
                               QStringLiteral("--area=-10,20,300,400"), QStringLiteral("--block=")};
    QCOMPARE(normalized, expected);
}

void CoreUnitTest::expandsOutputPatterns()
{
    const QDateTime dateTime(QDate(2026, 10, 13), QTime(9, 5, 7));
    QCOMPARE(expandOutputFilePattern(QStringLiteral("shot_$yyyyMMdd$_$HHmmss$.png"), dateTime),
             QStringLiteral("shot_20261013_090507.png"));
    const QStringList expected{QStringLiteral("clipboard"), QStringLiteral("save"), QStringLiteral("pin")};
    QCOMPARE(splitOutputSpec(QStringLiteral("clipboard;save;;pin")), expected);
}

void CoreUnitTest::persistsHistoryWithoutRenamingExistingFiles()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    ScreenshotHistory history(10);
    QImage first(QSize(32, 24), QImage::Format_ARGB32_Premultiplied);
    first.fill(Qt::red);
    QImage firstBackground(first.size(), first.format());
    firstBackground.fill(Qt::black);
    history.add(first, firstBackground);
    history.saveToDirectory(directory.path());
    history.rebaseBackgroundPaths(directory.path());

    const QStringList firstFiles =
        QDir(directory.path())
            .entryList({QStringLiteral("*.png"), QStringLiteral("*.background.tiles")}, QDir::Files,
                       QDir::Name);
    QCOMPARE(firstFiles.size(), 3);

    QImage second(first.size(), first.format());
    second.fill(Qt::blue);
    history.add(second, firstBackground);
    history.saveToDirectory(directory.path());
    history.rebaseBackgroundPaths(directory.path());

    const QStringList secondFiles =
        QDir(directory.path())
            .entryList({QStringLiteral("*.png"), QStringLiteral("*.background.tiles")}, QDir::Files,
                       QDir::Name);
    QCOMPARE(secondFiles.size(), 6);
    for (const QString &file : firstFiles)
        QVERIFY2(secondFiles.contains(file), qPrintable(file));
    QCOMPARE(QDir(directory.path() + QStringLiteral("/background-tiles"))
                 .entryList({QStringLiteral("*.png")}, QDir::Files)
                 .size(),
             1);

    ScreenshotHistory loaded(10);
    loaded.loadFromDirectory(directory.path());
    QCOMPARE(loaded.size(), 2);
    QCOMPARE(loaded.imageAt(0).pixelColor(0, 0), QColor(Qt::blue));
    QCOMPARE(loaded.imageAt(1).pixelColor(0, 0), QColor(Qt::red));
    QCOMPARE(loaded.backgroundAt(0).pixelColor(0, 0), QColor(Qt::black));
    QCOMPARE(loaded.backgroundAt(1).pixelColor(0, 0), QColor(Qt::black));
    QCOMPARE(loaded.imageSizeAt(0), second.size());
    QCOMPARE(loaded.thumbnailAt(0).pixelColor(0, 0), QColor(Qt::blue));
}

void CoreUnitTest::historyGeometryStaysWithEachCapture()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ScreenshotHistory history(3);
    const QSize desktopSize(640, 480);
    const QList<QRect> regions{QRect(20, 30, 80, 60), QRect(200, 150, 40, 80), QRect(20, 30, 80, 60)};
    const QList<QColor> colors{Qt::red, Qt::green, Qt::blue};
    for (int index = 0; index < regions.size(); ++index)
    {
        QImage image(regions.at(index).size(), QImage::Format_RGB32);
        image.fill(colors.at(index));
        QImage background(desktopSize * 2, QImage::Format_RGB32);
        background.fill(colors.at(index));
        background.setDevicePixelRatio(2);
        history.add(image, background, {regions.at(index), desktopSize});
        QVERIFY(history.saveToDirectory(directory.path()));
        history.rebaseBackgroundPaths(directory.path());
        history.releaseDecodedImages();
        history.releaseDecodedBackgrounds();
        QCOMPARE(history.memoryUsageBytes(), 0);
    }
    ScreenshotHistory loaded(3);
    loaded.loadFromDirectory(directory.path());
    QCOMPARE(loaded.size(), 3);
    QCOMPARE(loaded.memoryUsageBytes(), 0);
    for (int index = 0; index < loaded.size(); ++index)
    {
        const int source = 2 - index;
        QCOMPARE(loaded.geometries().at(index).region, regions.at(source));
        QCOMPARE(loaded.geometries().at(index).desktopSize, desktopSize);
        QCOMPARE(loaded.imageAt(index).pixelColor(0, 0), colors.at(source));
        QCOMPARE(loaded.backgroundAt(index).pixelColor(0, 0), colors.at(source));
        QCOMPARE(loaded.backgroundAt(index).devicePixelRatio(), 2);
    }
    loaded.setMaximumCount(2);
    QCOMPARE(loaded.geometries().size(), loaded.size());
    QVERIFY(loaded.saveToDirectory(directory.path()));
    QCOMPARE(QDir(directory.path()).entryList({QStringLiteral("*.geometry.json")}, QDir::Files).size(), 2);
    loaded.clear();
    QVERIFY(loaded.geometries().isEmpty());

    // Full-screen commands have no separately selected rectangle.
    QImage fullScreen(desktopSize, QImage::Format_RGB32);
    fullScreen.fill(Qt::cyan);
    loaded.add(fullScreen, fullScreen);
    QCOMPARE(loaded.geometries().first().region, fullScreen.rect());
    QCOMPARE(loaded.geometries().first().desktopSize, desktopSize);
}

void CoreUnitTest::loadsLegacyHistoryWithoutInventingGeometry()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QDir dir(directory.path());
    QImage oldImage(QSize(32, 24), QImage::Format_RGB32);
    oldImage.fill(Qt::yellow);
    QImage oldBackground(QSize(640, 480), QImage::Format_RGB32);
    oldBackground.fill(Qt::magenta);
    QVERIFY(oldImage.save(dir.filePath(QStringLiteral("000.png"))));
    QVERIFY(oldBackground.save(dir.filePath(QStringLiteral("000.background.png"))));

    ScreenshotHistory history;
    history.loadFromDirectory(directory.path());
    QCOMPARE(history.size(), 1);
    QCOMPARE(history.memoryUsageBytes(), 0);
    QVERIFY(!history.geometries().first().isValid());
    QCOMPARE(history.imageAt(0), oldImage);
    QCOMPARE(history.backgroundAt(0), oldBackground);

    QImage newImage(QSize(20, 20), QImage::Format_RGB32);
    newImage.fill(Qt::cyan);
    history.add(newImage, oldBackground, {QRect(80, 60, 20, 20), oldBackground.size()});
    QVERIFY(history.saveToDirectory(directory.path()));
    history.rebaseBackgroundPaths(directory.path());
    history.releaseDecodedImages();
    history.releaseDecodedBackgrounds();
    QCOMPARE(history.memoryUsageBytes(), 0);
    QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("000.background.png"))));
    history.loadFromDirectory(directory.path());
    QCOMPARE(history.size(), 2);
    QVERIFY(history.geometries().first().isValid());
    QVERIFY(!history.geometries().last().isValid());
    QCOMPARE(history.imageAt(1), oldImage);
    QCOMPARE(history.backgroundAt(1), oldBackground);

    // An explicitly empty index must not resurrect the numbered legacy files.
    QFile order(dir.filePath(QStringLiteral(".snipaste-order")));
    QVERIFY(order.open(QIODevice::WriteOnly | QIODevice::Truncate));
    order.close();
    history.loadFromDirectory(directory.path());
    QCOMPARE(history.size(), 0);
}

void CoreUnitTest::historyWriterCoalescesPendingImages()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    std::atomic<int> liveImages{0};
    struct Pixels
    {
        QByteArray data;
        std::atomic<int> *liveImages;
    };
    ScreenshotHistory history(1);
    ScreenshotHistoryWriter writer;
    QSignalSpy saved(&writer, &ScreenshotHistoryWriter::saved);
    QSignalSpy failed(&writer, &ScreenshotHistoryWriter::failed);
    for (int index = 0; index < 40; ++index)
    {
        const QSize size = index == 0 ? QSize(3840, 2160) : QSize(512, 512);
        auto *pixels = new Pixels{QByteArray(size.width() * size.height() * 4, char(index)), &liveImages};
        ++liveImages;
        QImage image(
            reinterpret_cast<uchar *>(pixels->data.data()), size.width(), size.height(), size.width() * 4,
            QImage::Format_RGB32,
            [](void *data) {
                auto *pixels = static_cast<Pixels *>(data);
                --*pixels->liveImages;
                delete pixels;
            },
            pixels);
        history.add(image);
        writer.save(history, directory.path());
        image = {};
        history.clear();
        // One being saved plus the newest pending. Superseded requests must
        // release their pixels immediately rather than wait for a worker slot.
        QVERIFY2(liveImages.load() <= 2, "obsolete history requests retained image buffers");
    }
    writer.waitForDone();
    QTRY_COMPARE(saved.count(), 1);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(liveImages.load(), 0);
    ScreenshotHistory restored;
    restored.loadFromDirectory(directory.path());
    QCOMPARE(restored.size(), 1);
    QCOMPARE(restored.imageAt(0).size(), QSize(512, 512));
    QCOMPARE(restored.imageAt(0).pixel(0, 0) & 0x00ffffff, quint32(0x00272727));
}

void CoreUnitTest::configLookupCachePreservesCompatibility()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("config.ini"));
    ConfigStore config(path);
    config.setValue(QStringLiteral("Snip/Remember"), true);
    QCOMPARE(config.value(QStringLiteral("snip/remember"), false).toBool(), true);
    QCOMPARE(config.value(QStringLiteral("missing"), 41).toInt(), 41);
    config.setValue(QStringLiteral("MiSSing"), 42);
    QCOMPARE(config.value(QStringLiteral("missing"), 41).toInt(), 42);
    config.setValue(QStringLiteral("Snip/Remember"), false);
    QCOMPARE(config.value(QStringLiteral("snip/remember"), true).toBool(), false);
    QVERIFY(config.save());
    {
        QSettings external(path, QSettings::IniFormat);
        external.setValue(QStringLiteral("New/External"), 43);
        external.sync();
    }
    QVERIFY(config.load());
    QCOMPARE(config.value(QStringLiteral("new/external")).toInt(), 43);
    QVERIFY(config.relocate(directory.filePath(QStringLiteral("next.ini"))));
    QCOMPARE(config.value(QStringLiteral("new/external")).toInt(), 43);
    QCOMPARE(config.value(QStringLiteral("unknown"), 44).toInt(), 44);
}

void CoreUnitTest::stitchesVerticalScrollFrames()
{
    QImage source(QSize(12, 32), QImage::Format_RGB32);
    source.setColorSpace(QColorSpace::SRgb);
    for (int y = 0; y < source.height(); ++y)
        for (int x = 0; x < source.width(); ++x)
            source.setPixelColor(x, y, QColor::fromHsv((y * 17) % 360, 220, 220));

    const QImage first = source.copy(0, 0, source.width(), 16);
    const QImage second = source.copy(0, 8, source.width(), 16);
    VerticalScrollStitcher stitcher(200);
    QVERIFY(stitcher.append(first));
    QCOMPARE(stitcher.image().colorSpace(), source.colorSpace());
    QVERIFY(stitcher.append(second));
    QCOMPARE(stitcher.image().size(), QSize(12, 24));
    QCOMPARE(stitcher.image().colorSpace(), source.colorSpace());
    QCOMPARE(stitcher.image().copy(0, 0, first.width(), first.height()), first);
    QCOMPARE(stitcher.image().pixelColor(0, 23), source.pixelColor(0, 23));
    QVERIFY(!stitcher.append(second));
}

namespace {
QImage scrollSource(QSize size)
{
    QImage image(size, QImage::Format_RGB32);
    for (int y = 0; y < size.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < size.width(); ++x) {
            quint32 value = quint32(x * 719 + y * 51787);
            value = (value ^ (value >> 16)) * 0x7feb352du;
            value = (value ^ (value >> 15)) * 0x846ca68bu;
            value ^= value >> 16;
            row[x] = qRgb(value >> 24, (value >> 16) & 255, (value >> 8) & 255);
        }
    }
    return image;
}
}

void CoreUnitTest::scrollStripsPreservePixelsAndSnapshots()
{
    const QImage source = scrollSource(QSize(64, 211));
    VerticalScrollStitcher stitcher;
    QVERIFY(!stitcher.append({}));
    QVERIFY(stitcher.append(source.copy(0, 0, 64, 60)));
    const QImage first = stitcher.image();
    for (int offset : {19, 36, 57, 80, 103, 130, 151}) {
        QVERIFY(stitcher.append(source.copy(0, offset, 64, 60)));
        QCOMPARE(stitcher.size(), QSize(64, offset + 60));
        QCOMPARE(stitcher.image(), source.copy(0, 0, 64, offset + 60));
        // The preview paints strips without invoking image(). Verify both
        // whole-image painting and a viewport across strip boundaries.
        for (const QRect area : {stitcher.image().rect(), QRect(7, offset - 3, 31, 11)}) {
            QImage actual(area.size(), QImage::Format_RGB32);
            actual.fill(Qt::magenta);
            QPainter painter(&actual);
            painter.translate(-area.topLeft());
            stitcher.draw(painter, area);
            painter.end();
            QCOMPARE(actual, source.copy(area));
        }
    }
    QCOMPARE(first, source.copy(0, 0, 64, 60));
    const VerticalScrollStitcher snapshot = stitcher;
    const QImage result = stitcher.takeImage();
    QCOMPARE(result, source);
    QVERIFY(stitcher.size().isEmpty());
    QVERIFY(stitcher.image().isNull());
    QCOMPARE(stitcher.frameCount(), 0);
    QCOMPARE(snapshot.image(), source);
    QVERIFY(stitcher.append(source.copy(0, 0, 64, 60)));
    stitcher.clear();
    QCOMPARE(result, source);
}

void CoreUnitTest::scrollStripsHandleLimitsAndNormalization()
{
    const QImage source = scrollSource(QSize(64, 140));
    VerticalScrollStitcher stitcher(91);
    QImage first = source.copy(0, 0, 64, 60).convertToFormat(QImage::Format_ARGB32);
    first.setDevicePixelRatio(1.5);
    QVERIFY(stitcher.append(first));
    QCOMPARE(stitcher.image().devicePixelRatio(), 1.0);
    QVERIFY(!stitcher.append(first));
    // Preserve the last one or two rows too, including a partial final scroll.
    QVERIFY(stitcher.append(source.copy(0, 1, 64, 60)));
    QVERIFY(stitcher.append(source.copy(0, 21, 64, 60)));
    QCOMPARE(stitcher.image(), source.copy(0, 0, 64, 81));
    QVERIFY(stitcher.append(source.copy(0, 44, 64, 60)));
    QCOMPARE(stitcher.image(), source.copy(0, 0, 64, 91));
    const QImage capped = stitcher.image();
    QVERIFY(!stitcher.append(source.copy(0, 70, 64, 60)));
    QCOMPARE(stitcher.image(), capped);
    VerticalScrollStitcher small(11);
    QVERIFY(small.append(first));
    QCOMPARE(small.image(), source.copy(0, 0, 64, 11));
    VerticalScrollStitcher minimum(0);
    QVERIFY(minimum.append(first));
    QCOMPARE(minimum.size(), QSize(64, 1));

    QImage transparent(64, 60, QImage::Format_ARGB32_Premultiplied);
    transparent.fill(QColor(80, 160, 240, 120));
    VerticalScrollStitcher alpha;
    QVERIFY(alpha.append(transparent));
    QVERIFY(!alpha.append(source.copy(0, 0, 32, 60)));
    QCOMPARE(alpha.lastResult(), VerticalScrollStitcher::AppendResult::SizeChanged);
    QCOMPARE(alpha.image(), transparent.convertToFormat(QImage::Format_RGB32));
}

void CoreUnitTest::scrollMatchesSmallAndFastMovements_data()
{
    QTest::addColumn<int>("offset");
    for (int offset = 1; offset <= 68; ++offset)
        QTest::newRow(qPrintable(QString::number(offset))) << offset;
}

void CoreUnitTest::scrollMatchesSmallAndFastMovements()
{
    QFETCH(int, offset);
    const QImage source = scrollSource(QSize(96, 240));
    VerticalScrollStitcher stitcher;
    QVERIFY(stitcher.append(source.copy(0, 0, 96, 80)));
    QVERIFY(stitcher.append(source.copy(0, offset, 96, 80)));
    QCOMPARE(stitcher.lastResult(), VerticalScrollStitcher::AppendResult::Added);
    QCOMPARE(stitcher.lastOverlap(), 80 - offset);
    QCOMPARE(stitcher.image(), source.copy(0, 0, 96, 80 + offset));
}

void CoreUnitTest::scrollRejectsGapsAndRecovers()
{
    const QImage source = scrollSource(QSize(96, 400));
    VerticalScrollStitcher stitcher;
    QVERIFY(stitcher.append(source.copy(0, 0, 96, 80)));
    const QImage before = stitcher.image();
    // A jump larger than the viewport has missing pixels that no matcher
    // can reconstruct. Never append the entire unrelated frame.
    QVERIFY(!stitcher.append(source.copy(0, 180, 96, 80)));
    QCOMPARE(stitcher.lastResult(), VerticalScrollStitcher::AppendResult::NoMatch);
    QCOMPARE(stitcher.image(), before);
    QCOMPARE(stitcher.frameCount(), 1);
    QVERIFY(!stitcher.append(source.copy(0, 0, 96, 80)));
    QCOMPARE(stitcher.lastResult(), VerticalScrollStitcher::AppendResult::Unchanged);
    QVERIFY(stitcher.append(source.copy(0, 40, 96, 80)));
    QCOMPARE(stitcher.image(), source.copy(0, 0, 96, 120));
    QVERIFY(!stitcher.append(source.copy(0, 40, 96, 79)));
    QCOMPARE(stitcher.lastResult(), VerticalScrollStitcher::AppendResult::SizeChanged);
}

void CoreUnitTest::scrollBottomDoesNotDuplicatePixels()
{
    const QImage source = scrollSource(QSize(96, 161));
    VerticalScrollStitcher stitcher;
    QVERIFY(stitcher.append(source.copy(0, 0, 96, 80)));
    for (int offset : {40, 79, 80, 81})
        QVERIFY(stitcher.append(source.copy(0, offset, 96, 80)));
    QCOMPARE(stitcher.image(), source);
    QImage bottom = source.copy(0, 81, 96, 80);
    // A tiny blinking indicator must not append another entire viewport.
    bottom.setPixelColor(3, 3, Qt::red);
    for (int i = 0; i < 20; ++i) {
        QVERIFY(!stitcher.append(bottom));
        QCOMPARE(stitcher.lastResult(), VerticalScrollStitcher::AppendResult::Unchanged);
        QCOMPARE(stitcher.image(), source);
    }
    QCOMPARE(stitcher.frameCount(), 5);
}

void CoreUnitTest::scrollRejectsAmbiguousContent()
{
    const QImage tile = scrollSource(QSize(96, 8));
    QImage repeated(96, 160, QImage::Format_RGB32);
    QPainter painter(&repeated);
    for (int y = 0; y < repeated.height(); y += 8)
        painter.drawImage(0, y, tile);
    painter.end();
    VerticalScrollStitcher stitcher;
    QVERIFY(stitcher.append(repeated.copy(0, 0, 96, 80)));
    QVERIFY(!stitcher.append(repeated.copy(0, 2, 96, 80)));
    QCOMPARE(stitcher.lastResult(), VerticalScrollStitcher::AppendResult::Ambiguous);
    QCOMPARE(stitcher.size(), QSize(96, 80));
    QImage blank(96, 80, QImage::Format_RGB32);
    blank.fill(Qt::white);
    QVERIFY(!stitcher.append(blank));
    QCOMPARE(stitcher.size(), QSize(96, 80));

    // A large blank area and a blinking mark supply no reliable alignment.
    // Do not repeatedly verify full-width blank pixels for every overlap.
    QImage emptyPage(1920, 1080, QImage::Format_RGB32);
    emptyPage.fill(Qt::white);
    VerticalScrollStitcher lowDetail;
    QVERIFY(lowDetail.append(emptyPage));
    emptyPage.setPixelColor(3, 500, Qt::black);
    QVERIFY(!lowDetail.append(emptyPage));
    QCOMPARE(lowDetail.size(), QSize(1920, 1080));
}

void CoreUnitTest::scrollCaptureWaitsForStableFrames()
{
    const QImage source = scrollSource(QSize(96, 240));
    ScrollCaptureStability stability;
    for (int i = 0; i < 8; ++i)
        QVERIFY(!stability.observe(source.copy(0, i * 11, 96, 80), i * 40));
    const QImage stopped = source.copy(0, 90, 96, 80);
    QVERIFY(!stability.observe(stopped, 320));
    QVERIFY(!stability.observe(stopped, 360));
    QVERIFY(stability.observe(stopped, 400));
    stability.reset();
    QVERIFY(!stability.observe(stopped, 440));
    QVERIFY(!stability.observe({}, 480));
    QVERIFY(!stability.observe(stopped, 520));
    QVERIFY(stability.observe(stopped, 600));
}

void CoreUnitTest::scrollMatchesSparsePageContent()
{
    // Most page pixels are blank. A low global mean error alone must not
    // make unrelated blank margins look like a reliable overlap.
    QImage page(480, 1000, QImage::Format_RGB32);
    page.fill(Qt::white);
    QPainter painter(&page);
    for (int line = 0; line < 70; ++line) {
        for (int word = 0; word < 7; ++word) {
            const int width = 8 + (line * 17 + word * 13) % 38;
            painter.fillRect(17 + word * 62, line * 14 + 3, width, 3 + line % 4, Qt::black);
        }
    }
    painter.end();
    for (int step : {1, 2, 17, 64, 127, 153}) {
        VerticalScrollStitcher stitcher;
        QVERIFY(stitcher.append(page.copy(0, 0, 480, 180)));
        QVERIFY2(stitcher.append(page.copy(0, step, 480, 180)), qPrintable(QString::number(step)));
        QCOMPARE(stitcher.lastOverlap(), 180 - step);
        QCOMPARE(stitcher.image(), page.copy(0, 0, 480, 180 + step));
    }
}

void CoreUnitTest::scrollMatchesContentBetweenSampleColumns()
{
    QImage page(1920, 240, QImage::Format_RGB32);
    page.fill(Qt::white);
    // A single-pixel column that never intersects the 64 sample columns.
    // It is the only evidence of movement, and must not be called unchanged.
    for (int y = 0; y < page.height(); ++y)
        page.setPixel(3, y, qRgb(y, 0, 0));
    for (int step : {1, 2, 17, 64, 68}) {
        VerticalScrollStitcher stitcher;
        QVERIFY(stitcher.append(page.copy(0, 0, page.width(), 80)));
        const QImage before = stitcher.image();
        QVERIFY(!stitcher.append(page.copy(0, 140, page.width(), 80)));
        QCOMPARE(stitcher.lastResult(), VerticalScrollStitcher::AppendResult::NoMatch);
        QCOMPARE(stitcher.image(), before);
        QVERIFY(stitcher.append(page.copy(0, step, page.width(), 80)));
        QCOMPARE(stitcher.lastOverlap(), 80 - step);
        QCOMPARE(stitcher.image(), page.copy(0, 0, page.width(), 80 + step));
    }
}

void CoreUnitTest::scrollMatchesLowContrastContent()
{
    // Each row is distinct, but neighboring pixels are too similar to
    // produce strong edge features for the tolerant matcher.
    QImage page(320, 240, QImage::Format_RGB32);
    for (int y = 0; y < page.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(page.scanLine(y));
        std::fill_n(row, page.width(), qRgb(y, y, y));
    }
    for (int step : {1, 2, 17, 64, 68}) {
        VerticalScrollStitcher stitcher;
        QVERIFY(stitcher.append(page.copy(0, 0, page.width(), 80)));
        QVERIFY(stitcher.append(page.copy(0, step, page.width(), 80)));
        QCOMPARE(stitcher.lastOverlap(), 80 - step);
        QCOMPARE(stitcher.image(), page.copy(0, 0, page.width(), 80 + step));
    }
}

void CoreUnitTest::scrollMatchesWithSmallPixelChanges()
{
    const QImage source = scrollSource(QSize(96, 160));
    VerticalScrollStitcher stitcher;
    QVERIFY(stitcher.append(source.copy(0, 0, 96, 80)));
    QImage moved = source.copy(0, 23, 96, 80);
    moved.setPixelColor(3, 10, Qt::red);
    QVERIFY(stitcher.append(moved));
    QCOMPARE(stitcher.lastOverlap(), 57);
    QCOMPARE(stitcher.image(), source.copy(0, 0, 96, 103));
}

QTEST_MAIN(CoreUnitTest)

#include "core_unit_test.moc"
