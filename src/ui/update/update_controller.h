#pragma once

#include "core/update/github_update_client.h"

#include <QDateTime>
#include <QPointer>
#include <QTimer>

class QWidget;
class QProgressDialog;

namespace snipaste::core { class ConfigStore; }

namespace snipaste::ui {

class UpdateController final : public QObject
{
public:
    explicit UpdateController(core::ConfigStore &config, QObject *parent = nullptr);
    void start();
    void checkNow(QWidget *parent = nullptr);

private:
    void check(bool manual, QWidget *parent = nullptr);
    void configureProxy();
    void closeProgress();
    void handleResult(const core::ReleaseResult &result);
    void beginDownload(const core::ReleaseInfo &release);
    core::ConfigStore &m_config;
    core::GitHubUpdateClient m_client;
    QTimer m_periodic;
    QDateTime m_lastCheck;
    QPointer<QWidget> m_parent;
    QPointer<QProgressDialog> m_progress;
    bool m_manual = false;
    bool m_downloading = false;
    bool m_install = false;
    QString m_lastPromptedVersion;
};

} // namespace snipaste::ui
