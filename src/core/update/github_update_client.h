#pragma once

#include "core/update/release_info.h"

#include <QCryptographicHash>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QSaveFile>
#include <QTemporaryDir>
#include <memory>

class QNetworkReply;

namespace snipaste::core {

class GitHubUpdateClient final : public QObject
{
    Q_OBJECT
public:
    explicit GitHubUpdateClient(QString repository, QString currentVersion,
                                QObject *parent = nullptr,
                                QUrl apiBase = QUrl(QStringLiteral("https://api.github.com")));
    ~GitHubUpdateClient() override;
    bool isBusy() const;
    void setProxy(const QNetworkProxy &proxy);
    void useSystemProxy();
    void check(bool includeBeta);
    void download(const ReleaseInfo &release);
    void cancel();

signals:
    void checked(const snipaste::core::ReleaseResult &result);
    void failed(const QString &message);
    void downloadProgress(qint64 received, qint64 total);
    void downloaded(const QString &archive, const QString &temporaryDirectory);
    void canceled();

private:
    QNetworkReply *get(const QUrl &url);
    void downloadArchive(const ReleaseInfo &release, const QByteArray &expectedHash);
    void fail(const QString &message);
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QString m_repository;
    QString m_currentVersion;
    QUrl m_apiBase;
    std::unique_ptr<QTemporaryDir> m_directory;
    std::unique_ptr<QSaveFile> m_file;
    QCryptographicHash m_hash{QCryptographicHash::Sha256};
};

} // namespace snipaste::core

Q_DECLARE_METATYPE(snipaste::core::ReleaseResult)
