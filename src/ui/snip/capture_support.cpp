#include "ui/snip/capture_support.h"

#include "platform/screen_capture.h"
#include <QCoreApplication>
#include <QCursor>
#include <QDebug>
#include <QGuiApplication>
#include <QMessageBox>
#include <QPainter>
#include <QScreen>
#ifdef Q_OS_MACOS
#include "platform/macos/system.h"
#endif

namespace snipaste::ui {

bool ensureMacScreenCaptureAccess(const QString &operation)
{
#if defined(Q_OS_MACOS)
    if (snipaste::platform::macScreenCaptureAccessGranted()
        || snipaste::platform::requestMacScreenCaptureAccess())
        return true;
    QString applicationPath = QCoreApplication::applicationFilePath();
    const qsizetype bundleSuffix = applicationPath.lastIndexOf(
        QStringLiteral(".app/Contents/MacOS/"), -1, Qt::CaseInsensitive);
    if (bundleSuffix >= 0)
        applicationPath.truncate(bundleSuffix + 4);
    qWarning().noquote() << "macOS Screen Recording permission check/request failed for" << applicationPath;
    QMessageBox::warning(nullptr,
                          QStringLiteral("需要屏幕录制权限"),
                          QStringLiteral("请在“系统设置 → 隐私与安全性 → 屏幕录制”中允许 snipaste。\n\n"
                                         "如果权限已开启但仍被拒绝，请用“−”移除旧的 snipaste 记录，"
                                         "再用“+”添加下面的当前应用：\n%2\n\n"
                                         "设置完成后，请完全退出并重新打开此应用，然后再开始%1。")
                              .arg(operation, applicationPath));
    return false;
#else
    Q_UNUSED(operation)
    return true;
#endif
}

QImage captureScreens(const QList<QScreen *> &screens)
{
    if (screens.isEmpty())
        return {};
    if (!ensureMacScreenCaptureAccess(QStringLiteral("截图")))
        return {};

    const QRect virtualGeometry = screens.first()->virtualGeometry();
    QImage result(virtualGeometry.size(), QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::black);
    QPainter painter(&result);
    for (QScreen *screen : screens) {
        if (!screen)
            continue;
        QImage image = snipaste::platform::captureScreen(screen);
        const QRect target = screen->geometry().translated(-virtualGeometry.topLeft());
        if (image.size() != target.size())
            image = image.scaled(target.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        painter.drawImage(target.topLeft(), image);
    }
    return result;
}

QImage captureAllScreens()
{
    return captureScreens(QGuiApplication::screens());
}

QColor colorAtCursor()
{
#if defined(Q_OS_MACOS)
    if (!ensureMacScreenCaptureAccess(QStringLiteral("取色")))
        return {};
#endif
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
        return {};
    const QPoint local = QCursor::pos() - screen->geometry().topLeft();
    const QImage image = snipaste::platform::captureScreen(screen);
    if (image.isNull() || screen->geometry().width() <= 0 || screen->geometry().height() <= 0)
        return {};
    const qreal scaleX = qreal(image.width()) / qreal(screen->geometry().width());
    const qreal scaleY = qreal(image.height()) / qreal(screen->geometry().height());
    const QPoint pixel(qBound(0, qRound(local.x() * scaleX), image.width() - 1),
                       qBound(0, qRound(local.y() * scaleY), image.height() - 1));
    return image.pixelColor(pixel);
}

} // namespace snipaste::ui
