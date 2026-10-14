#include "core/update/release_info.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSysInfo>
#include <QStringList>
#include <array>

namespace {

struct Version {
    std::array<quint64, 3> numbers{};
    QStringList prerelease;
    bool valid = false;
};

Version parseVersion(const QString &text)
{
    static const QRegularExpression pattern(QStringLiteral(
        "^[vV]?(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)"
        "(?:-([0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*))?"
        "(?:\\+([0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*))?$"));
    const auto match = pattern.match(text.trimmed());
    Version version;
    if (!match.hasMatch())
        return version;
    for (int i = 0; i < 3; ++i) {
        bool ok = false;
        version.numbers[i] = match.captured(i + 1).toULongLong(&ok);
        if (!ok)
            return version;
    }
    if (!match.captured(4).isEmpty()) {
        version.prerelease = match.captured(4).split(QLatin1Char('.'));
        static const QRegularExpression numeric(QStringLiteral("^[0-9]+$"));
        for (const QString &part : version.prerelease)
            if (numeric.match(part).hasMatch() && part.size() > 1 && part.startsWith(QLatin1Char('0')))
                return version;
    }
    version.valid = true;
    return version;
}

int compareIdentifiers(const QString &left, const QString &right)
{
    static const QRegularExpression numeric(QStringLiteral("^[0-9]+$"));
    const bool leftNumeric = numeric.match(left).hasMatch();
    const bool rightNumeric = numeric.match(right).hasMatch();
    if (leftNumeric != rightNumeric)
        return leftNumeric ? -1 : 1;
    if (leftNumeric && left.size() != right.size())
        return left.size() < right.size() ? -1 : 1;
    const int result = QString::compare(left, right, Qt::CaseSensitive);
    return result < 0 ? -1 : result > 0 ? 1 : 0;
}

bool isReleaseUrl(const QUrl &url, const QString &repository, const QString &kind)
{
    return url.scheme() == QStringLiteral("https")
        && url.host() == QStringLiteral("github.com") && url.userInfo().isEmpty()
        && (url.port() == -1 || url.port() == 443)
        && url.path().startsWith(QLatin1Char('/') + repository + QStringLiteral("/releases/") + kind);
}

}

namespace snipaste::core {

int compareReleaseVersions(const QString &left, const QString &right, bool *valid)
{
    const Version a = parseVersion(left);
    const Version b = parseVersion(right);
    if (valid)
        *valid = a.valid && b.valid;
    if (!a.valid || !b.valid)
        return 0;
    if (a.numbers != b.numbers)
        return a.numbers < b.numbers ? -1 : 1;
    if (a.prerelease.isEmpty() != b.prerelease.isEmpty())
        return a.prerelease.isEmpty() ? 1 : -1;
    for (qsizetype i = 0; i < qMin(a.prerelease.size(), b.prerelease.size()); ++i) {
        const int order = compareIdentifiers(a.prerelease[i], b.prerelease[i]);
        if (order != 0)
            return order;
    }
    return a.prerelease.size() < b.prerelease.size() ? -1
        : a.prerelease.size() > b.prerelease.size() ? 1 : 0;
}

ReleaseResult selectGitHubRelease(const QByteArray &json, const QString &currentVersion,
                                 bool includeBeta, const QString &platform,
                                 const QString &repository)
{
    ReleaseResult result;
    if (!parseVersion(currentVersion).valid) {
        result.error = QStringLiteral("当前软件版本格式无效。");
        return result;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        result.error = QStringLiteral("GitHub 返回的更新信息格式无效。");
        return result;
    }
    QJsonObject selected;
    for (const auto &item : document.array()) {
        const auto release = item.toObject();
        const QString version = release.value(QStringLiteral("tag_name")).toString();
        const Version parsed = parseVersion(version);
        if (!parsed.valid || release.value(QStringLiteral("draft")).toBool())
            continue;
        if (!includeBeta && (release.value(QStringLiteral("prerelease")).toBool()
                             || !parsed.prerelease.isEmpty()))
            continue;
        const QUrl page(release.value(QStringLiteral("html_url")).toString());
        if (!isReleaseUrl(page, repository, QStringLiteral("tag/")))
            continue;
        if (!result.hasRelease || compareReleaseVersions(version, result.release.version) > 0) {
            selected = release;
            result.hasRelease = true;
            result.release.version = version;
            result.release.pageUrl = page;
        }
    }
    if (!result.hasRelease)
        return result;
    result.newer = compareReleaseVersions(result.release.version, currentVersion) > 0;
    result.release.title = selected.value(QStringLiteral("name")).toString(result.release.version);
    result.release.notes = selected.value(QStringLiteral("body")).toString();
    QString version = result.release.version;
    if (version.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
        version.remove(0, 1);
    QStringList platforms{platform};
    if (platform.startsWith(QStringLiteral("macos-")) && platform != QStringLiteral("macos-universal"))
        platforms.append(QStringLiteral("macos-universal"));
    const auto assets = selected.value(QStringLiteral("assets")).toArray();
    for (const QString &target : platforms) {
        const QString expected = QStringLiteral("snipaste-%1-%2.zip").arg(version, target);
        for (const auto &item : assets) {
            const auto asset = item.toObject();
            const QUrl url(asset.value(QStringLiteral("browser_download_url")).toString());
            if (asset.value(QStringLiteral("name")).toString() != expected
                || !isReleaseUrl(url, repository, QStringLiteral("download/")))
                continue;
            result.release.assetName = expected;
            result.release.assetUrl = url;
            result.release.assetSize = asset.value(QStringLiteral("size")).toInteger();
            const QString digest = asset.value(QStringLiteral("digest")).toString();
            static const QRegularExpression sha(QStringLiteral("^sha256:([0-9a-fA-F]{64})$"));
            const auto match = sha.match(digest);
            if (match.hasMatch())
                result.release.sha256 = match.captured(1).toLatin1().toLower();
            for (const auto &candidate : assets) {
                const auto checksum = candidate.toObject();
                const QUrl checksumUrl(checksum.value(QStringLiteral("browser_download_url")).toString());
                if (checksum.value(QStringLiteral("name")).toString() == expected + QStringLiteral(".sha256")
                    && isReleaseUrl(checksumUrl, repository, QStringLiteral("download/")))
                    result.release.checksumUrl = checksumUrl;
            }
            return result;
        }
    }
    return result;
}

QString updatePlatform()
{
    const QString architecture = QSysInfo::buildCpuArchitecture() == QStringLiteral("arm64")
        ? QStringLiteral("arm64") : QStringLiteral("x64");
#ifdef Q_OS_WIN
    return QStringLiteral("windows-") + architecture;
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos-") + architecture;
#else
    return QStringLiteral("unsupported");
#endif
}

} // namespace snipaste::core
