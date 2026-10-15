#include "ui/update/update_controller.h"

#include "core/config/config_store.h"
#include "ui/update/update_installer.h"

#include <QApplication>
#include <QDebug>
#include <QDesktopServices>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkProxy>
#include <QProgressDialog>
#include <QPushButton>

namespace snipaste::ui {

UpdateController::UpdateController(core::ConfigStore &config, QObject *parent)
    : QObject(parent), m_config(config),
      m_client(QStringLiteral(SNIPASTE_UPDATE_REPOSITORY), QCoreApplication::applicationVersion(), this),
      m_lastCheck(QDateTime::currentDateTimeUtc())
{
    connect(&m_client, &core::GitHubUpdateClient::checked, this, [this](const auto &result) {
        closeProgress();
        handleResult(result);
    });
    connect(&m_client, &core::GitHubUpdateClient::failed, this, [this](const QString &error) {
        closeProgress();
        if (m_manual || m_downloading)
            QMessageBox::warning(m_parent, QStringLiteral("更新失败"), error);
        else
            qWarning() << "background update check failed:" << error;
        m_downloading = false;
    });
    connect(&m_client, &core::GitHubUpdateClient::canceled, this, [this] {
        closeProgress();
        m_downloading = false;
    });
    connect(&m_client, &core::GitHubUpdateClient::downloadProgress, this, [this](qint64 done, qint64 total) {
        if (!m_progress)
            return;
        if (total > 0) {
            m_progress->setMaximum(static_cast<int>(qMin(total, 1024LL * 1024 * 1024)));
            m_progress->setValue(static_cast<int>(qMin(done, total)));
        }
        m_progress->setLabelText(QStringLiteral("正在下载新版… %1 / %2 MB")
                                    .arg(done / 1048576.0, 0, 'f', 1)
                                    .arg(total / 1048576.0, 0, 'f', 1));
    });
    connect(&m_client, &core::GitHubUpdateClient::downloaded, this,
            [this](const QString &archive, const QString &directory) {
        closeProgress();
        m_downloading = false;
        if (!m_install) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
            QMessageBox::information(m_parent, QStringLiteral("下载完成"),
                (archive.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)
                     ? QStringLiteral("新版已下载并通过校验，请运行安装包：\n%1")
                     : QStringLiteral("新版已下载并通过校验，请解压安装：\n%1")).arg(archive));
            return;
        }
        QString status, error;
        if (!launchUpdateInstaller(archive, directory, m_config.filePath(), &status, &error)) {
            QMessageBox::warning(m_parent, QStringLiteral("安装更新失败"), error);
            QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
            return;
        }
        m_config.setValue(QStringLiteral("update/installStatusFile"), status);
        m_config.save();
        QCoreApplication::quit();
    });
    connect(&m_periodic, &QTimer::timeout, this, [this] {
        const qint64 interval = qBound(1, m_config.value(QStringLiteral("update/checkIntervalHours"), 24).toInt(), 720) * 3600LL;
        if (m_config.value(QStringLiteral("update/checkIntervalEnabled"), true).toBool()
            && m_lastCheck.secsTo(QDateTime::currentDateTimeUtc()) >= interval)
            check(false);
    });
}

void UpdateController::start()
{
    m_periodic.start(60000);
    QTimer::singleShot(3000, this, [this] {
        const QString statusPath = m_config.value(QStringLiteral("update/installStatusFile")).toString();
        if (!statusPath.isEmpty()) {
            QFile file(statusPath);
            if (file.open(QIODevice::ReadOnly)) {
                QByteArray data = file.readAll();
                if (data.startsWith("\xef\xbb\xbf"))
                    data.remove(0, 3);
                const auto status = QJsonDocument::fromJson(data).object();
                if (status.value(QStringLiteral("state")).toString() == QStringLiteral("failed"))
                    QMessageBox::warning(nullptr, QStringLiteral("更新未完成"),
                        QStringLiteral("更新安装失败，已恢复原版本。\n%1")
                            .arg(status.value(QStringLiteral("message")).toString()));
            }
            m_config.setValue(QStringLiteral("update/installStatusFile"), QString());
            m_config.save();
        }
        if (m_config.value(QStringLiteral("update/checkAtStartup"),
                            m_config.value(QStringLiteral("general/autoUpdate"), true)).toBool())
            check(false);
    });
}

void UpdateController::configureProxy()
{
    const QString type = m_config.value(QStringLiteral("update/proxyType"), QStringLiteral("系统代理")).toString();
    if (type == QStringLiteral("系统代理")) {
        m_client.useSystemProxy();
        return;
    }
    if (type == QStringLiteral("不使用代理")) {
        m_client.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
        return;
    }
    QNetworkProxy proxy(type == QStringLiteral("SOCKS5 代理")
                            ? QNetworkProxy::Socks5Proxy : QNetworkProxy::HttpProxy,
                        m_config.value(QStringLiteral("update/proxyServer")).toString(),
                        static_cast<quint16>(m_config.value(QStringLiteral("update/proxyPort"), 0).toUInt()));
    if (m_config.value(QStringLiteral("update/proxyRequiresAuth"), false).toBool()) {
        proxy.setUser(m_config.value(QStringLiteral("update/proxyUsername")).toString());
        proxy.setPassword(m_config.value(QStringLiteral("update/proxyPassword")).toString());
    }
    m_client.setProxy(proxy);
}

void UpdateController::checkNow(QWidget *parent)
{
    check(true, parent);
}

void UpdateController::check(bool manual, QWidget *parent)
{
    if (m_client.isBusy()) {
        if (manual && m_progress) {
            m_progress->show();
            m_progress->raise();
        }
        return;
    }
    if (!manual && QApplication::activeModalWidget())
        return;
    m_parent = parent;
    m_manual = manual;
    m_lastCheck = QDateTime::currentDateTimeUtc();
    configureProxy();
    if (manual) {
        m_progress = new QProgressDialog(QStringLiteral("正在检查 GitHub 更新…"), QStringLiteral("取消"), 0, 0, parent);
        m_progress->setWindowTitle(QStringLiteral("检查更新"));
        m_progress->setWindowModality(Qt::WindowModal);
        m_progress->setMinimumDuration(0);
        connect(m_progress, &QProgressDialog::canceled, &m_client, &core::GitHubUpdateClient::cancel);
        m_progress->show();
    }
    m_client.check(m_config.value(QStringLiteral("update/includeBeta"), false).toBool());
}

void UpdateController::closeProgress()
{
    if (m_progress) {
        disconnect(m_progress, nullptr, &m_client, nullptr);
        m_progress->close();
        m_progress->deleteLater();
        m_progress.clear();
    }
}

void UpdateController::handleResult(const core::ReleaseResult &result)
{
    if (!result.hasRelease || !result.newer) {
        if (m_manual)
            QMessageBox::information(m_parent, QStringLiteral("检查更新"),
                result.hasRelease ? QStringLiteral("当前已是最新版本（%1）。").arg(QCoreApplication::applicationVersion())
                                  : QStringLiteral("尚未发布可用版本，请稍后再检查。"));
        return;
    }
    if (!m_manual && m_lastPromptedVersion == result.release.version)
        return;
    m_lastPromptedVersion = result.release.version;
    QString reason;
    m_install = !updateInstallationDirectory(&reason).isEmpty();
    QMessageBox dialog(QMessageBox::Information, QStringLiteral("发现新版本"),
                      QStringLiteral("snipaste %1 已发布\n当前版本：%2")
                          .arg(result.release.version, QCoreApplication::applicationVersion()),
                      QMessageBox::Cancel, m_parent);
    dialog.setTextFormat(Qt::PlainText);
    dialog.setDetailedText(result.release.notes.left(20000));
    auto *page = dialog.addButton(QStringLiteral("查看发布页"), QMessageBox::ActionRole);
    QPushButton *download = nullptr;
    if (!result.release.assetUrl.isEmpty()) {
        dialog.setInformativeText(m_install ? QStringLiteral("下载校验完成后，程序会退出、安装更新并重启。")
                                             : reason);
        download = dialog.addButton(m_install ? QStringLiteral("立即更新") : QStringLiteral("下载新版"),
                                     QMessageBox::AcceptRole);
    } else {
        dialog.setInformativeText(QStringLiteral("此版本暂未提供当前平台的安装包。"));
    }
    dialog.exec();
    if (dialog.clickedButton() == page)
        QDesktopServices::openUrl(result.release.pageUrl);
    else if (download && dialog.clickedButton() == download)
        beginDownload(result.release);
}

void UpdateController::beginDownload(const core::ReleaseInfo &release)
{
    m_downloading = true;
    m_progress = new QProgressDialog(QStringLiteral("正在下载并校验新版…"), QStringLiteral("取消"), 0, 0, m_parent);
    m_progress->setWindowTitle(QStringLiteral("下载更新"));
    m_progress->setWindowModality(Qt::WindowModal);
    m_progress->setMinimumDuration(0);
    m_progress->setAutoClose(false);
    m_progress->setAutoReset(false);
    connect(m_progress, &QProgressDialog::canceled, &m_client, &core::GitHubUpdateClient::cancel);
    m_progress->show();
    m_client.download(release);
}

} // namespace snipaste::ui
