#include "ui/paste/clipboard_content.h"

#include <QAbstractTextDocumentLayout>
#include <QDir>
#include <QFileInfo>
#include <QFontMetrics>
#include <QPainter>
#include <QRegularExpression>
#include <QTextDocument>

namespace snipaste::ui {

QImage clipboardTextImage(const QString &text,
                          const snipaste::core::ConfigStore *config)
{
    const QStringList lines = text.split(QChar::LineFeed);
    QFont font;
    font.setPixelSize(18);
    int margin = 16;
    int maxWidth = 0;
    QColor background = Qt::white;
    QColor foreground = Qt::black;
    if (config) {
        margin = qBound(0, config->value(QStringLiteral("paste/textMargin"), 12).toInt(), 100);
        maxWidth = qMax(100, config->value(QStringLiteral("paste/textMaxWidth"), 750).toInt());
        const QString fontSpec = config->value(QStringLiteral("paste/textFont"),
                                                QStringLiteral("Microsoft YaHei UI, 11")).toString();
        const QStringList fontParts = fontSpec.split(QChar(','), Qt::SkipEmptyParts);
        if (!fontParts.isEmpty())
            font.setFamily(fontParts.first().trimmed());
        if (fontParts.size() > 1) {
            bool ok = false;
            const double pointSize = fontParts.at(1).trimmed().toDouble(&ok);
            if (ok && pointSize > 0.0)
                font.setPointSizeF(pointSize);
        }
        const QColor configuredBackground = QColor::fromString(
            config->value(QStringLiteral("paste/textBackgroundColor"),
                          QStringLiteral("#ffffffff")).toString());
        const QColor configuredForeground = QColor::fromString(
            config->value(QStringLiteral("paste/textColor"),
                          QStringLiteral("#ff111111")).toString());
        if (configuredBackground.isValid())
            background = configuredBackground;
        if (configuredForeground.isValid())
            foreground = configuredForeground;
    }
    const QFontMetrics metrics(font);
    int width = 320;
    for (const QString &line : lines)
        width = qMax(width, metrics.horizontalAdvance(line) + margin * 2);
    if (maxWidth > 0)
        width = qMin(width, maxWidth);
    const int textWidth = qMax(1, width - margin * 2);
    const QRect measured = metrics.boundingRect(QRect(0, 0, textWidth, 100000),
                                                Qt::TextWordWrap, text);
    const QRect textRect(margin, margin, textWidth,
                         qMax(1, measured.height()));
    const int height = qMax(64, textRect.bottom() + margin + 1);
    QImage image(QSize(width, height), QImage::Format_ARGB32_Premultiplied);
    image.fill(background);
    QPainter painter(&image);
    painter.setFont(font);
    painter.setPen(foreground);
    painter.drawText(textRect,
                     Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                     text);
    return image;
}

QColor parseClipboardColor(const QString &text)
{
    const QString value = text.trimmed();
    if (value.startsWith(QChar('#'))) {
        const QColor color(value);
        return color.isValid() ? color : QColor();
    }

    const QStringList components = value.split(QRegularExpression(QStringLiteral("[,;\\s]+")),
                                               Qt::SkipEmptyParts);
    if (components.size() != 3 && components.size() != 4)
        return {};
    QVector<double> numbers;
    numbers.reserve(components.size());
    for (const QString &component : components) {
        bool ok = false;
        const double number = component.toDouble(&ok);
        if (!ok || number < 0.0)
            return {};
        numbers.append(number);
    }
    const bool normalized = std::all_of(numbers.cbegin(), numbers.cend(), [](double value) {
        return value <= 1.0;
    });
    const auto channel = [normalized](double value) {
        return qBound(0, qRound(normalized ? value * 255.0 : value), 255);
    };
    QColor color(channel(numbers.at(0)), channel(numbers.at(1)), channel(numbers.at(2)));
    if (numbers.size() == 4)
        color.setAlpha(channel(numbers.at(3)));
    return color;
}

QImage clipboardColorImage(const QColor &color)
{
    if (!color.isValid())
        return {};
    QImage image(QSize(240, 160), QImage::Format_ARGB32_Premultiplied);
    image.fill(color);
    return image;
}

QImage clipboardHtmlImage(const QString &html, bool forceWrap)
{
    QTextDocument document;
    document.setDefaultFont(QFont(QStringLiteral("Microsoft YaHei UI"), 10));
    document.setHtml(html);
    document.setTextWidth(forceWrap ? 720 : -1);
    const QSizeF documentSize = document.documentLayout()->documentSize();
    const QSize size(qMax(64, qCeil(documentSize.width()) + 24),
                     qMax(48, qCeil(documentSize.height()) + 24));
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.translate(12, 12);
    document.drawContents(&painter);
    return image;
}

QStringList resolvePasteFileArguments(const QStringList &arguments)
{
    QStringList resolved;
    QString currentFolder;
    for (const QString &argument : arguments) {
        if (argument.trimmed().isEmpty())
            continue;
        const QString expanded = QDir::fromNativeSeparators(argument);
        const QFileInfo info(expanded);
        const bool explicitFolder = expanded.endsWith(QChar('/'))
                                    || expanded.endsWith(QChar('\\'));
        if (info.isDir() || explicitFolder) {
            currentFolder = info.isAbsolute() ? info.absoluteFilePath()
                                              : QDir::current().absoluteFilePath(expanded);
            continue;
        }
        if (info.isAbsolute() || currentFolder.isEmpty())
            resolved.append(info.isAbsolute() ? info.absoluteFilePath() : expanded);
        else
            resolved.append(QDir(currentFolder).filePath(expanded));
    }
    return resolved;
}

} // namespace snipaste::ui
