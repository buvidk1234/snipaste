#include "ui/output/image_output.h"

#include "core/command/argument_parser.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QPrintDialog>
#include <QPrinter>
#include <QStandardPaths>

namespace snipaste::ui {
using core::expandOutputFilePattern;

QImage addDropShadow(const QImage &source)
{
    if (source.isNull())
        return {};
    constexpr int margin = 12;
    QImage result(source.size() + QSize(margin * 2, margin * 2),
                  QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setOpacity(0.18);
    for (int offset = 4; offset >= 1; --offset)
        painter.drawImage(margin + offset, margin + offset, source);
    painter.setOpacity(1.0);
    painter.drawImage(margin, margin, source);
    return result;
}

QImage removeRedundantAlpha(const QImage &source, bool enabled)
{
    if (!enabled || source.isNull() || !source.hasAlphaChannel())
        return source;
    for (int y = 0; y < source.height(); ++y) {
        for (int x = 0; x < source.width(); ++x)
            if (qAlpha(source.pixel(x, y)) != 255)
                return source;
    }
    return source.convertToFormat(QImage::Format_RGB32);
}

QImage addSoftwareMetadata(const QImage &source, bool enabled)
{
    if (!enabled || source.isNull())
        return source;
    QImage result = source;
    result.setText(QStringLiteral("Software"),
                   QStringLiteral("snipaste %1").arg(QCoreApplication::applicationVersion()));
    result.setText(QStringLiteral("Comment"), QStringLiteral("Created by snipaste"));
    return result;
}

bool printImage(const QImage &image)
{
    if (image.isNull())
        return false;
    QPrinter printer(QPrinter::HighResolution);
    printer.setDocName(QStringLiteral("snipaste screenshot"));
    QPrintDialog dialog(&printer);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    QPainter painter(&printer);
    if (!painter.isActive())
        return false;
    const QRect page = printer.pageRect(QPrinter::DevicePixel).toRect();
    const QImage scaled = image.scaled(page.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const QPoint topLeft(page.left() + (page.width() - scaled.width()) / 2,
                         page.top() + (page.height() - scaled.height()) / 2);
    painter.drawImage(topLeft, scaled);
    return true;
}

QString outputFilePath(const QString &value, const snipaste::core::ConfigStore &config)
{
    QString path = value.trimmed();
    if (path.isEmpty())
        return {};
    path = expandOutputFilePattern(path, QDateTime::currentDateTime());
    if (QFileInfo(path).isRelative()) {
        const QString directory = config.value(QStringLiteral("output/saveDirectory"),
                                               QStandardPaths::writableLocation(QStandardPaths::PicturesLocation))
                                      .toString();
        path = QDir(directory).filePath(path);
    }
    return QDir::cleanPath(path);
}

} // namespace snipaste::ui
