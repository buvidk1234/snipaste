#include "app/runtime_settings.h"

#include "platform/desktop_integration.h"
#include <QApplication>
#include <QRegularExpression>
#include <QStyle>

namespace snipaste::app {

bool isApplicationIgnored(const snipaste::core::ConfigStore &config, const QString &key)
{
    QStringList ignored = config.value(key).toStringList();
    if (ignored.isEmpty())
        ignored = config.value(key).toString().split(QRegularExpression(QStringLiteral("[,;\\n]+")),
                                                     Qt::SkipEmptyParts);
    if (ignored.isEmpty())
        return false;
    return snipaste::platform::foregroundApplicationMatches(ignored);
}



bool isHotkeyIgnored(const snipaste::core::ConfigStore &config)
{
    return isApplicationIgnored(config, QStringLiteral("hotkeys/ignoreApplications"));
}

QStringList configuredProcessList(const snipaste::core::ConfigStore &config,
                                  const QString &key, const QString &legacyKey)
{
    QVariant value = config.value(key);
    if (!value.isValid() && !legacyKey.isEmpty())
        value = config.value(legacyKey);
    QStringList entries = value.toStringList();
    if (entries.isEmpty())
        entries = value.toString().split(QRegularExpression(QStringLiteral("[,;\\n]+")),
                                         Qt::SkipEmptyParts);
    for (QString &entry : entries)
        entry = entry.trimmed();
    entries.removeAll(QString());
    return entries;
}

void applyConfiguredTheme(const QString &theme)
{
    QPalette palette = QApplication::style()->standardPalette();
    if (theme == QStringLiteral("深色")) {
        palette.setColor(QPalette::Window, QColor(45, 45, 48));
        palette.setColor(QPalette::WindowText, QColor(235, 235, 235));
        palette.setColor(QPalette::Base, QColor(30, 30, 30));
        palette.setColor(QPalette::AlternateBase, QColor(45, 45, 48));
        palette.setColor(QPalette::Text, QColor(235, 235, 235));
        palette.setColor(QPalette::Button, QColor(60, 60, 64));
        palette.setColor(QPalette::ButtonText, QColor(235, 235, 235));
        palette.setColor(QPalette::Highlight, QColor(42, 130, 218));
        palette.setColor(QPalette::HighlightedText, Qt::white);
    }
    QApplication::setPalette(palette);
}

qreal configuredPasteOpacity(const snipaste::core::ConfigStore &config)
{
    // Older development builds wrote the value as a normalized fraction
    // (0.1..1.0), while the preferences page writes a percentage
    // (10..100).  Accept both formats so an old 0.8 value cannot become
    // 0.008 and get clamped to a nearly invisible paste.
    const double raw = config.value(QStringLiteral("paste/opacity"), 100).toDouble();
    const double normalized = raw > 1.0 ? raw / 100.0 : raw;
    return qBound<qreal>(0.1, qreal(normalized), 1.0);
}

QColor parseColorSpec(const QString &value, const QColor &fallback)
{
    const QString text = value.trimmed();
    if (text.isEmpty())
        return fallback;
    QColor color(text);
    return color.isValid() ? color : fallback;
}

void applyConfiguredProcessPriority(const snipaste::core::ConfigStore &config)
{
    const int configured = qBound(0, config.value(QStringLiteral("general/process_priority"),
                                                  config.value(QStringLiteral("process_priority"), 0)).toInt(), 3);
    snipaste::platform::setProcessPriority(configured);
}

void applyPasteSettings(const snipaste::core::ConfigStore &config, snipaste::ui::PasteManager &pasteManager)
{
    const auto configuredPasteMouseActions = [&config] {
        QStringList actions;
        actions.reserve(18);
        for (int index = 0; index < 18; ++index)
            actions.append(config.value(QStringLiteral("control/mouseAction%1").arg(index)).toString());
        return actions;
    };
    pasteManager.setRestoreCount(config.value(QStringLiteral("paste/restoreCount"), 1).toInt());
    pasteManager.setUseCapturePosition(config.value(QStringLiteral("paste/useCapturePosition"), true).toBool());
    pasteManager.setDefaultOpacity(configuredPasteOpacity(config));
    pasteManager.setShadowEnabled(config.value(QStringLiteral("paste/shadow"), true).toBool());
    pasteManager.setResizeEnabled(!config.value(QStringLiteral("paste/disable_frame_resize"), false).toBool());
    pasteManager.setMagneticAttachThreshold(config.value(QStringLiteral("paste/magnetic_attach_threshold"),
                                                         config.value(QStringLiteral("magnetic_attach_threshold"), 50)).toInt());
    pasteManager.setExchangeNavigationKeys(
        config.value(QStringLiteral("hotkeys/exchange_wasd_and_arrow_keys"), false).toBool());
    const qreal maxDownloadMb = config.value(QStringLiteral("misc/auto_download_max_image_size_mb"),
                                             config.value(QStringLiteral("auto_download_max_image_size_mb"), 20.0)).toDouble();
    pasteManager.setMaxNetworkImageBytes(maxDownloadMb <= 0.0
                                             ? 0
                                             : qRound64(maxDownloadMb * 1024.0 * 1024.0));
    pasteManager.setMaxSize(config.value(QStringLiteral("paste/maxSize"), 12000).toInt());
    pasteManager.setThumbnailSize(QSize(
        config.value(QStringLiteral("paste/thumbWidth"), 62).toInt(),
        config.value(QStringLiteral("paste/thumbHeight"), 62).toInt()));
    pasteManager.setWheelCenter(config.value(QStringLiteral("paste/wheelCenter"), false).toBool());
    pasteManager.setResetTransform(config.value(QStringLiteral("paste/resetTransform"), true).toBool());
    pasteManager.setHelpKeys(config.value(QStringLiteral("paste/helpKeys"), false).toBool());
    pasteManager.setDisableDpiScaling(
        config.value(QStringLiteral("general/disablePasteDpiScaling"), false).toBool());
    pasteManager.setCloseConfirm(config.value(QStringLiteral("paste/closeConfirm"), true).toBool());
    pasteManager.setDestroyConfirm(config.value(QStringLiteral("paste/destroyConfirm"), true).toBool());
    pasteManager.setCopyAsFileMode(config.value(QStringLiteral("output/copyAsFileMode"), 1).toInt());
    pasteManager.setBackgroundMode(
        config.value(QStringLiteral("paste/backgroundMode"), QStringLiteral("伪透明")).toString());
    pasteManager.setSmoothScale(config.value(QStringLiteral("paste/smoothScale"), true).toBool());
    pasteManager.setVirtualDesktopPinned(config.value(QStringLiteral("paste/virtualDesktop"), false).toBool());
    pasteManager.setFlashNew(config.value(QStringLiteral("paste/flashNew"), true).toBool());
    pasteManager.setActivateNew(config.value(QStringLiteral("paste/activateNew"), true).toBool());
    pasteManager.setFlashTransparency(config.value(QStringLiteral("paste/flashTransparency"), true).toBool());
    pasteManager.setFlashGroupSwitch(config.value(QStringLiteral("paste/flashGroupSwitch"), true).toBool());
    pasteManager.setHideAllOnPasteKey(config.value(QStringLiteral("paste/hideAllOnPasteKey"), false).toBool());
    pasteManager.setKeepOnDrag(config.value(QStringLiteral("paste/keepOnDrag"), false).toBool());
    pasteManager.setEscapeActions(
        config.value(QStringLiteral("paste/escapeAction"), QStringLiteral("关闭贴图")).toString(),
        config.value(QStringLiteral("paste/shiftEscapeAction"), QStringLiteral("销毁贴图")).toString());
    pasteManager.setMouseActions(configuredPasteMouseActions());
}

} // namespace snipaste::app
