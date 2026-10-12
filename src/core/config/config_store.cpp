#include "core/config/config_store.h"

#include <QDir>
#include <QDebug>
#include <QFileInfo>

namespace snipate::core {

ConfigStore::ConfigStore(QString filePath)
    : m_filePath(std::move(filePath))
    , m_settings(std::make_unique<QSettings>(m_filePath, QSettings::IniFormat))
{
}

bool ConfigStore::load()
{
    m_settings->sync();
    if (m_settings->status() != QSettings::NoError) {
        qWarning() << "failed to load configuration:" << m_filePath;
        return false;
    }
    return true;
}

bool ConfigStore::save()
{
    QDir().mkpath(QFileInfo(m_filePath).absolutePath());
    m_settings->sync();
    if (m_settings->status() != QSettings::NoError) {
        qWarning() << "failed to save configuration:" << m_filePath;
        return false;
    }
    return true;
}

bool ConfigStore::relocate(const QString &filePath)
{
    const QString destination = QFileInfo(filePath).absoluteFilePath();
    if (destination.isEmpty()) return false;
    QSettings next(destination, QSettings::IniFormat);
    for (const QString &key : m_settings->allKeys()) next.setValue(key, m_settings->value(key));
    next.sync();
    if (next.status() != QSettings::NoError) return false;
    m_filePath = destination;
    m_settings = std::make_unique<QSettings>(m_filePath, QSettings::IniFormat);
    return true;
}

QVariant ConfigStore::value(const QString &key, const QVariant &fallback) const
{
    if (m_settings->contains(key))
        return m_settings->value(key, fallback);

    // Official Snipaste configuration files use section names such as
    // [Paste], [Snip] and [Misc], while the application stores its runtime
    // keys in lower-case namespaces.  QSettings treats key casing as
    // significant, so accept a case-insensitive spelling here.  This keeps
    // hand-written config.ini files compatible without duplicating every
    // lookup at the call site.
    const QString folded = key.toCaseFolded();
    for (const QString &candidate : m_settings->allKeys()) {
        if (candidate.toCaseFolded() == folded)
            return m_settings->value(candidate, fallback);
    }
    return fallback;
}

void ConfigStore::setValue(const QString &key, const QVariant &value)
{
    m_settings->setValue(key, value);
}

QString ConfigStore::filePath() const
{
    return m_filePath;
}

} // namespace snipate::core
