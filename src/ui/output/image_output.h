#pragma once

#include "core/config/config_store.h"
#include <QImage>
#include <QString>

namespace snipaste::ui {

QImage addDropShadow(const QImage &source);
QImage removeRedundantAlpha(const QImage &source, bool enabled);
QImage addSoftwareMetadata(const QImage &source, bool enabled);
bool printImage(const QImage &image);
QString outputFilePath(const QString &value, const snipaste::core::ConfigStore &config);

} // namespace snipaste::ui
