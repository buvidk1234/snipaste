#pragma once

#include "core/config/config_store.h"
#include <QImage>
#include <QColor>
#include <QStringList>

namespace snipaste::ui {

QImage clipboardTextImage(const QString &text,
                          const snipaste::core::ConfigStore *config = nullptr);
QColor parseClipboardColor(const QString &text);
QImage clipboardColorImage(const QColor &color);
QImage clipboardHtmlImage(const QString &html, bool forceWrap = true);
QStringList resolvePasteFileArguments(const QStringList &arguments);

} // namespace snipaste::ui
