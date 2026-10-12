#pragma once

#include <QSettings>
#include <QString>
#include <memory>

namespace snipate::core {

class ConfigStore final
{
public:
    explicit ConfigStore(QString filePath);

    bool load();
    bool save();
    bool relocate(const QString &filePath);
    QVariant value(const QString &key, const QVariant &fallback = {}) const;
    void setValue(const QString &key, const QVariant &value);
    QString filePath() const;

private:
    QString m_filePath;
    std::unique_ptr<QSettings> m_settings;
};

} // namespace snipate::core
