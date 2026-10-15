#pragma once

#include "core/config/config_store.h"
#include "ui/paste/paste_manager.h"
#include <QColor>
#include <QStringList>

namespace snipaste::app
{

bool isApplicationIgnored(const core::ConfigStore &config, const QString &key);
bool isHotkeyIgnored(const core::ConfigStore &config);
QStringList configuredProcessList(const core::ConfigStore &config, const QString &key,
                                  const QString &legacyKey = {});
void applyConfiguredTheme(const QString &theme);
qreal configuredPasteOpacity(const core::ConfigStore &config);
QColor parseColorSpec(const QString &value, const QColor &fallback = QColor(Qt::white));
void applyConfiguredProcessPriority(const core::ConfigStore &config);
void applyApplicationSettings(const core::ConfigStore &config);
void applyPasteSettings(const core::ConfigStore &config, ui::PasteManager &pasteManager);

} // namespace snipaste::app
