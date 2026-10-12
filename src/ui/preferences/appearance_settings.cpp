#include "ui/preferences/appearance_settings.h"
#include "core/config/config_store.h"

#include <QApplication>
#include <QFont>
#include <QPainter>
#include <QPalette>
#include <QStyle>

namespace snipaste::ui {

QStringList defaultAnnotationPalette()
{
    return {QStringLiteral("#000000"), QStringLiteral("#808080"), QStringLiteral("#800000"),
        QStringLiteral("#ed1c24"), QStringLiteral("#ff7f27"), QStringLiteral("#fff200"),
        QStringLiteral("#22b14c"), QStringLiteral("#00a2e8"), QStringLiteral("#3f48cc"), QStringLiteral("#a349a4"),
        QStringLiteral("#ffffff"), QStringLiteral("#c3c3c3"), QStringLiteral("#b97a57"),
        QStringLiteral("#ffaec9"), QStringLiteral("#ffc90e"), QStringLiteral("#efe4b0"),
        QStringLiteral("#b5e61d"), QStringLiteral("#99d9ea"), QStringLiteral("#7092be"), QStringLiteral("#c8bfe7")};
}

QIcon applicationIcon()
{
    QIcon icon;
    for (int extent : {16, 20, 24, 32, 48, 64}) {
        QPixmap pixmap(extent, extent);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.scale(extent / 24.0, extent / 24.0);
        p.fillRect(QRectF(4, 1, 7, 9), QColor("#378bff"));
        p.fillRect(QRectF(13, 1, 7, 9), QColor("#ffda39"));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#f04b67"));
        p.drawPolygon(QPolygonF{QPointF(4, 12), QPointF(11, 12), QPointF(11, 18), QPointF(4, 23)});
        p.setBrush(QColor("#36c785"));
        p.drawPolygon(QPolygonF{QPointF(13, 12), QPointF(20, 12), QPointF(20, 23), QPointF(13, 18)});
        icon.addPixmap(pixmap);
    }
    return icon;
}

void applyApplicationAppearance(const core::ConfigStore &config)
{
    QFont font(QStringLiteral("Microsoft YaHei UI"), 9);
    const QString savedFont = config.value(QStringLiteral("interface/font")).toString();
    if (!savedFont.isEmpty()) font.fromString(savedFont);
    QApplication::setFont(font);
    QPalette palette = QApplication::style()->standardPalette();
    if (!config.value(QStringLiteral("interface/followSystemColor"), false).toBool()) {
        const QColor color(config.value(QStringLiteral("interface/accentColor"), QStringLiteral("#2889ed")).toString());
        if (color.isValid()) {
            palette.setColor(QPalette::Highlight, color);
            palette.setColor(QPalette::HighlightedText, Qt::white);
        }
    }
    QApplication::setPalette(palette);
    QApplication::setWindowIcon(applicationIcon());
}

CaptureAppearance CaptureAppearance::fromConfig(const core::ConfigStore &config)
{
    CaptureAppearance result;
    result.automaticMagnifier = config.value(QStringLiteral("interface/magnifier/automatic"), true).toBool();
    result.magnifierGrid = config.value(QStringLiteral("interface/magnifier/grid"), true).toBool();
    result.magnifierCoordinates = config.value(QStringLiteral("interface/magnifier/coordinates"), true).toBool();
    result.magnifierHints = config.value(QStringLiteral("interface/magnifier/hints"), true).toBool();
    result.hideHexPrefix = config.value(QStringLiteral("interface/magnifier/hideHash"), false).toBool();
    result.uppercaseHex = config.value(QStringLiteral("interface/magnifier/uppercase"), false).toBool();
    result.magnifierZoom = qBound(3, config.value(QStringLiteral("interface/magnifier/zoom"), 9).toInt(), 30);
    const QStringList formats = config.value(QStringLiteral("interface/magnifier/formats"), result.colorFormats).toStringList();
    result.colorFormats.clear();
    for (const QString &format : formats)
        if ((format == QStringLiteral("RGB") || format == QStringLiteral("HEX")) && !result.colorFormats.contains(format))
            result.colorFormats.append(format);
    if (result.colorFormats.isEmpty()) result.colorFormats.append(QStringLiteral("RGB"));
    result.rememberToolColors = config.value(QStringLiteral("interface/toolbar/rememberColors"), true).toBool();
    result.showShare = config.value(QStringLiteral("interface/toolbar/showShare"), true).toBool();
    result.penCount = qBound(1, config.value(QStringLiteral("interface/toolbar/penCount"), 3).toInt(), 5);
    const QStringList colors = config.value(QStringLiteral("interface/toolbar/palette"), result.palette).toStringList();
    result.palette.clear();
    for (const QString &color : colors)
        if (QColor(color).isValid() && result.palette.size() < 20) result.palette.append(QColor(color).name(QColor::HexArgb));
    if (result.palette.isEmpty()) result.palette = defaultAnnotationPalette();
    if (config.value(QStringLiteral("interface/followSystemColor"), false).toBool())
        result.accent = QApplication::palette().color(QPalette::Highlight);
    else {
        const QColor accent(config.value(QStringLiteral("interface/accentColor"), result.accent.name()).toString());
        if (accent.isValid()) result.accent = accent;
    }
    return result;
}

} // namespace snipaste::ui
