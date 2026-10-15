#include "core/annotation/annotation_document.h"
#include "core/command/argument_parser.h"
#include "core/config/config_store.h"
#include "core/history/screenshot_history.h"
#include "core/history/screenshot_history_writer.h"
#include "core/paste/paste_state_store.h"
#include "core/scroll_stitcher.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
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
    for (int y = 0; y < source.height(); ++y)
        for (int x = 0; x < source.width(); ++x)
            source.setPixelColor(x, y, QColor::fromHsv((y * 17) % 360, 220, 220));

    const QImage first = source.copy(0, 0, source.width(), 16);
    const QImage second = source.copy(0, 8, source.width(), 16);
    VerticalScrollStitcher stitcher(200);
    QVERIFY(stitcher.append(first));
    QVERIFY(stitcher.append(second));
    QCOMPARE(stitcher.image().size(), QSize(12, 24));
    QCOMPARE(stitcher.image().pixelColor(0, 23), source.pixelColor(0, 23));
    QVERIFY(!stitcher.append(second));
}

QTEST_MAIN(CoreUnitTest)

#include "core_unit_test.moc"
