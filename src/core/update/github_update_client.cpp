#include "core/update/github_update_client.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>

namespace {

constexpr qint64 kMaximumPackageBytes = 1024LL * 1024 * 1024;

class SystemProxyFactory final : public QNetworkProxyFactory
{
public:
    QList<QNetworkProxy> queryProxy(const QNetworkProxyQuery &query) override
    {
        return systemProxyForQuery(query);
    }
};

QString networkError(QNetworkReply *reply)
{
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 403 || status == 429)
        return QStringLiteral("GitHub 暂时限制了访问，请稍后重试或检查代理设置。");
    if (status == 404)
        return QStringLiteral("无法访问更新发布页，请确认发布仓库公开且已有 Release。");
    return QStringLiteral("连接 GitHub 失败：%1").arg(reply->errorString());
}

}

namespace snipaste::core {

GitHubUpdateClient::GitHubUpdateClient(QString repository, QString currentVersion,
                                     QObject *parent, QUrl apiBase)
    : QObject(parent), m_repository(std::move(repository)),
      m_currentVersion(std::move(currentVersion)), m_apiBase(std::move(apiBase))
{
    useSystemProxy();
}

bool GitHubUpdateClient::isBusy() const
{
    return !m_reply.isNull();
}

GitHubUpdateClient::~GitHubUpdateClient()
{
    if (m_reply) {
        disconnect(m_reply, nullptr, this, nullptr);
        m_reply->abort();
    }
}

void GitHubUpdateClient::setProxy(const QNetworkProxy &proxy)
{
    m_network.setProxy(proxy);
}

void GitHubUpdateClient::useSystemProxy()
{
    m_network.setProxyFactory(new SystemProxyFactory);
}

QNetworkReply *GitHubUpdateClient::get(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("snipaste/%1").arg(m_currentVersion));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(30000);
    m_reply = m_network.get(request);
    return m_reply;
}

void GitHubUpdateClient::check(bool includeBeta)
{
    if (isBusy())
        return;
    QUrl url = m_apiBase;
    url.setPath(QStringLiteral("/repos/%1/releases").arg(m_repository));
    url.setQuery(QStringLiteral("per_page=100"));
    QNetworkReply *reply = get(url);
    connect(reply, &QNetworkReply::readyRead, this, [reply] {
        if (reply->bytesAvailable() > 4 * 1024 * 1024)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, includeBeta] {
        m_reply.clear();
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            fail(networkError(reply));
            return;
        }
        const auto result = selectGitHubRelease(reply->readAll(), m_currentVersion,
                                                includeBeta, updatePlatform(), m_repository);
        if (!result.error.isEmpty()) {
            fail(result.error);
            return;
        }
        emit checked(result);
    });
}

void GitHubUpdateClient::download(const ReleaseInfo &release)
{
    if (isBusy())
        return;
    if (!release.sha256.isEmpty()) {
        downloadArchive(release, release.sha256);
        return;
    }
    if (release.checksumUrl.isEmpty()) {
        fail(QStringLiteral("此版本没有提供安装包校验信息，请从发布页下载。"));
        return;
    }
    QNetworkReply *reply = get(release.checksumUrl);
    connect(reply, &QNetworkReply::readyRead, this, [reply] {
        if (reply->bytesAvailable() > 4096)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, release] {
        m_reply.clear();
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            fail(networkError(reply));
            return;
        }
        const QString checksum = QString::fromUtf8(reply->readAll()).trimmed();
        static const QRegularExpression pattern(QStringLiteral("^([0-9a-fA-F]{64})\\s+\\*?([^\\r\\n]+)$"));
        const auto match = pattern.match(checksum);
        if (!match.hasMatch() || match.captured(2).trimmed() != release.assetName) {
            fail(QStringLiteral("安装包校验文件无效，请从发布页下载。"));
            return;
        }
        downloadArchive(release, match.captured(1).toLatin1().toLower());
    });
}

void GitHubUpdateClient::downloadArchive(const ReleaseInfo &release, const QByteArray &expectedHash)
{
    if (release.assetSize <= 0 || release.assetSize > kMaximumPackageBytes
        || release.assetUrl.isEmpty() || release.assetName.contains(QLatin1Char('/'))
        || release.assetName.contains(QLatin1Char('\\'))) {
        fail(QStringLiteral("安装包信息无效，请从发布页下载。"));
        return;
    }
    m_directory = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/snipaste-update-XXXXXX"));
    if (!m_directory->isValid()) {
        fail(QStringLiteral("无法创建更新下载目录。"));
        return;
    }
    const QString archive = m_directory->filePath(release.assetName);
    m_file = std::make_unique<QSaveFile>(archive);
    if (!m_file->open(QIODevice::WriteOnly)) {
        fail(QStringLiteral("无法保存更新包：%1").arg(m_file->errorString()));
        return;
    }
    m_hash.reset();
    QNetworkReply *reply = get(release.assetUrl);
    connect(reply, &QNetworkReply::downloadProgress, this, &GitHubUpdateClient::downloadProgress);
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        const QByteArray chunk = reply->readAll();
        if (m_file->size() + chunk.size() > kMaximumPackageBytes
            || m_file->write(chunk) != chunk.size()) {
            reply->setProperty("writeFailed", true);
            reply->abort();
            return;
        }
        m_hash.addData(chunk);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, release, expectedHash, archive] {
        m_reply.clear();
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            fail(reply->property("writeFailed").toBool()
                     ? QStringLiteral("保存安装包失败，请检查磁盘空间和文件夹权限。")
                     : networkError(reply));
            return;
        }
        if (m_file->size() != release.assetSize || m_hash.result().toHex() != expectedHash.toLower()) {
            fail(QStringLiteral("安装包校验失败，下载文件已丢弃。请重试。"));
            return;
        }
        if (!m_file->commit()) {
            fail(QStringLiteral("无法保存安装包：%1").arg(m_file->errorString()));
            return;
        }
        m_file.reset();
        const QString directory = m_directory->path();
        m_directory->setAutoRemove(false);
        m_directory.reset();
        emit downloaded(archive, directory);
    });
}

void GitHubUpdateClient::fail(const QString &message)
{
    m_file.reset();
    m_directory.reset();
    emit failed(message);
}

void GitHubUpdateClient::cancel()
{
    if (m_reply) {
        disconnect(m_reply, nullptr, this, nullptr);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply.clear();
    }
    m_file.reset();
    m_directory.reset();
    emit canceled();
}

} // namespace snipaste::core
