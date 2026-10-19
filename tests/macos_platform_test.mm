#include "platform/macos/capture.h"

#include <QColorSpace>
#include <QTest>

#import <CoreGraphics/CoreGraphics.h>

using namespace snipaste::platform;

class MacPlatformTest : public QObject
{
    Q_OBJECT

private slots:
    void screenshotPreservesRowOrder();
    void screenshotPreservesAlpha();
    void screenshotRetainsPhysicalPixels();
    void nullScreenshotIsEmpty();
};

void MacPlatformTest::screenshotPreservesRowOrder()
{
    QImage source(4, 2, QImage::Format_RGBA8888_Premultiplied);
    source.setColorSpace(QColorSpace::SRgb);
    source.fill(Qt::red);
    for (int x = 0; x < source.width(); ++x)
        source.setPixelColor(x, 1, Qt::blue);

    CGImageRef native = source.toCGImage();
    QVERIFY(native);
    const QImage actual = imageFromMacCGImage(native);
    CGImageRelease(native);

    QCOMPARE(actual.size(), source.size());
    QCOMPARE(actual.pixelColor(0, 0), QColor(Qt::red));
    QCOMPARE(actual.pixelColor(0, 1), QColor(Qt::blue));
    QCOMPARE(actual.colorSpace(), QColorSpace(QColorSpace::SRgb));
}

void MacPlatformTest::screenshotPreservesAlpha()
{
    QImage source(2, 1, QImage::Format_RGBA8888_Premultiplied);
    source.setColorSpace(QColorSpace::SRgb);
    source.setPixelColor(0, 0, QColor(180, 80, 20, 128));
    source.setPixelColor(1, 0, Qt::transparent);

    CGImageRef native = source.toCGImage();
    QVERIFY(native);
    const QImage actual = imageFromMacCGImage(native);
    CGImageRelease(native);

    QVERIFY(!actual.isNull());
    const QColor translucent = actual.pixelColor(0, 0);
    QCOMPARE(translucent.alpha(), 128);
    QVERIFY(qAbs(translucent.red() - 180) <= 2);
    QVERIFY(qAbs(translucent.green() - 80) <= 2);
    QVERIFY(qAbs(translucent.blue() - 20) <= 2);
    QCOMPARE(actual.pixelColor(1, 0).alpha(), 0);
}

void MacPlatformTest::screenshotRetainsPhysicalPixels()
{
    QImage source(16, 12, QImage::Format_RGBA8888_Premultiplied);
    source.setDevicePixelRatio(2.0);
    source.fill(Qt::green);

    CGImageRef native = source.toCGImage();
    QVERIFY(native);
    const QImage actual = imageFromMacCGImage(native);
    CGImageRelease(native);

    QCOMPARE(actual.size(), QSize(16, 12));
    QCOMPARE(actual.devicePixelRatio(), 1.0);
    QCOMPARE(actual.pixelColor(15, 11), QColor(Qt::green));
}

void MacPlatformTest::nullScreenshotIsEmpty()
{
    QVERIFY(imageFromMacCGImage(nullptr).isNull());
}

QTEST_APPLESS_MAIN(MacPlatformTest)
#include "macos_platform_test.moc"
