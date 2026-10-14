#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>

namespace snipaste::core {

struct ReleaseInfo {
    QString version;
    QString title;
    QString notes;
    QUrl pageUrl;
    QString assetName;
    QUrl assetUrl;
    QUrl checksumUrl;
    QByteArray sha256;
    qint64 assetSize = 0;
};

struct ReleaseResult {
    ReleaseInfo release;
    QString error;
    bool hasRelease = false;
    bool newer = false;
};

// Returns -1, 0 or 1; invalid semantic versions set valid to false.
int compareReleaseVersions(const QString &left, const QString &right, bool *valid = nullptr);
ReleaseResult selectGitHubRelease(const QByteArray &json, const QString &currentVersion,
                                 bool includeBeta, const QString &platform,
                                 const QString &repository);
QString updatePlatform();

} // namespace snipaste::core
