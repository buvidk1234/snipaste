#include "core/command/argument_parser.h"
#include "core/history/screenshot_history.h"

#include <QDir>
#include <QTemporaryDir>
#include <QTest>

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
};

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
    const QStringList input{
        QStringLiteral("snipaste"), QStringLiteral("snip"), QStringLiteral("--area"),
        QStringLiteral("-10"), QStringLiteral("20"), QStringLiteral("300"),
        QStringLiteral("400"), QStringLiteral("--block")};
    const QStringList normalized = normalizeMultiValueOptions(input);
    const QStringList expected{
        QStringLiteral("snipaste"), QStringLiteral("snip"),
        QStringLiteral("--area=-10,20,300,400"), QStringLiteral("--block=")};
    QCOMPARE(normalized, expected);
}

void CoreUnitTest::expandsOutputPatterns()
{
    const QDateTime dateTime(QDate(2026, 10, 13), QTime(9, 5, 7));
    QCOMPARE(expandOutputFilePattern(QStringLiteral("shot_$yyyyMMdd$_$HHmmss$.png"), dateTime),
             QStringLiteral("shot_20261013_090507.png"));
    const QStringList expected{QStringLiteral("clipboard"), QStringLiteral("save"),
                               QStringLiteral("pin")};
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

    const QStringList firstFiles = QDir(directory.path()).entryList(
        {QStringLiteral("*.png"), QStringLiteral("*.background.tiles")}, QDir::Files, QDir::Name);
    QCOMPARE(firstFiles.size(), 2);

    QImage second(first.size(), first.format());
    second.fill(Qt::blue);
    history.add(second, firstBackground);
    history.saveToDirectory(directory.path());
    history.rebaseBackgroundPaths(directory.path());

    const QStringList secondFiles = QDir(directory.path()).entryList(
        {QStringLiteral("*.png"), QStringLiteral("*.background.tiles")}, QDir::Files, QDir::Name);
    QCOMPARE(secondFiles.size(), 4);
    for (const QString &file : firstFiles)
        QVERIFY2(secondFiles.contains(file), qPrintable(file));
    QCOMPARE(QDir(directory.path() + QStringLiteral("/background-tiles")).entryList(
                 {QStringLiteral("*.png")}, QDir::Files).size(), 1);

    ScreenshotHistory loaded(10);
    loaded.loadFromDirectory(directory.path());
    QCOMPARE(loaded.size(), 2);
    QCOMPARE(loaded.imageAt(0).pixelColor(0, 0), QColor(Qt::blue));
    QCOMPARE(loaded.imageAt(1).pixelColor(0, 0), QColor(Qt::red));
    QCOMPARE(loaded.backgroundAt(0).pixelColor(0, 0), QColor(Qt::black));
    QCOMPARE(loaded.backgroundAt(1).pixelColor(0, 0), QColor(Qt::black));
}

QTEST_MAIN(CoreUnitTest)

#include "core_unit_test.moc"
