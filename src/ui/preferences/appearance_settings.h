#pragma once

#include <QColor>
#include <QIcon>
#include <QStringList>

namespace snipate::core { class ConfigStore; }

namespace snipate::ui {

QStringList defaultAnnotationPalette();
QIcon applicationIcon();
void applyApplicationAppearance(const core::ConfigStore &config);

struct CaptureAppearance {
    bool automaticMagnifier = true;
    bool magnifierGrid = true;
    bool magnifierCoordinates = true;
    bool magnifierHints = true;
    bool hideHexPrefix = false;
    bool uppercaseHex = false;
    int magnifierZoom = 9;
    QStringList colorFormats{QStringLiteral("RGB")};
    bool rememberToolColors = true;
    bool showShare = true;
    int penCount = 3;
    QStringList palette = defaultAnnotationPalette();
    QColor accent = QColor(QStringLiteral("#2889ed"));

    static CaptureAppearance fromConfig(const core::ConfigStore &config);
};

} // namespace snipate::ui
