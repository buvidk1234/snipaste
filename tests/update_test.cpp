#include "core/update/github_update_client.h"
#include "core/update/release_info.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QNetworkProxy>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

using namespace snipaste::core;

namespace {

const QString repository = QStringLiteral("test-owner/snipaste");

QJsonObject asset(const QString &version, const QString &platform)
{
    const QString name = QStringLiteral("snipaste-%1-%2.zip").arg(version, platform);
    return {{QStringLiteral("name"), name}, {QStringLiteral("size"), 123},
            {QStringLiteral("browser_download_url"),
             QStringLiteral("https://github.com/%1/releases/download/v%2/%3").arg(repository, version, name)}};
}

QJsonObject release(const QString &version, bool prerelease = false, bool draft = false)
{
    return {{QStringLiteral("tag_name"), QStringLiteral("v") + version},
            {QStringLiteral("prerelease"), prerelease}, {QStringLiteral("draft"), draft},
            {QStringLiteral("html_url"), QStringLiteral("https://github.com/%1/releases/tag/v%2").arg(repository, version)},
            {QStringLiteral("assets"), QJsonArray{asset(version, QStringLiteral("windows-x64")),
                                                  asset(version, QStringLiteral("macos-universal"))}}};
}

class HttpFixture : public QTcpServer
{
public:
    QByteArray body;
    int status = 200;
    int requests = 0;
    bool respond = true;
    QHash<QByteArray, QByteArray> responses;

    HttpFixture()
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                QTcpSocket *socket = nextPendingConnection();
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    const auto buffer = socket->property("request").toByteArray() + socket->readAll();
                    socket->setProperty("request", buffer);
                    if (!buffer.contains("\r\n\r\n") || socket->property("answered").toBool())
                        return;
                    socket->setProperty("answered", true);
                    ++requests;
                    if (!respond)
                        return;
                    const QByteArray path = buffer.split(' ').value(1);
                    const QByteArray payload = responses.value(path, body);
                    const QByteArray headers = "HTTP/1.1 " + QByteArray::number(status)
                        + " Test\r\nContent-Length: " + QByteArray::number(payload.size())
                        + "\r\nConnection: close\r\n\r\n";
                    socket->write(headers + payload);
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
        const bool ok = listen(QHostAddress::LocalHost);
        Q_ASSERT(ok);
    }

    QUrl url() const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(serverPort()));
    }
};

}

class UpdateTest final : public QObject
{
    Q_OBJECT
private slots:
    void versions_data();
    void versions();
    void rejectsInvalidVersions();
    void selectsStableAndBeta();
    void selectsMatchingAssetsAndRejectsForeignUrls();
    void handlesEmptyAndInvalidCatalogs();
    void networkCheckAndDuplicateRequests();
    void networkErrorsAndCancellation();
    void downloadsAndChecksHash();
    void downloadsUsingChecksumFile();
    void discardsCorruptDownload();
};

void UpdateTest::versions_data()
{
    QTest::addColumn<QString>("left");
    QTest::addColumn<QString>("right");
    QTest::addColumn<int>("order");
    QTest::newRow("numeric") << QStringLiteral("0.10.0") << QStringLiteral("0.9.9") << 1;
    QTest::newRow("older") << QStringLiteral("0.1.0") << QStringLiteral("0.1.1") << -1;
    QTest::newRow("tag") << QStringLiteral("v0.1.1") << QStringLiteral("0.1.1") << 0;
    QTest::newRow("stable") << QStringLiteral("1.0.0") << QStringLiteral("1.0.0-beta.10") << 1;
    QTest::newRow("beta-numbers") << QStringLiteral("1.0.0-beta.10") << QStringLiteral("1.0.0-beta.2") << 1;
    QTest::newRow("numeric-prerelease") << QStringLiteral("1.0.0-1") << QStringLiteral("1.0.0-alpha") << -1;
    QTest::newRow("identifier-length") << QStringLiteral("1.0.0-alpha") << QStringLiteral("1.0.0-alpha.1") << -1;
    QTest::newRow("metadata") << QStringLiteral("1.0.0+build.4") << QStringLiteral("1.0.0+build.5") << 0;
}

void UpdateTest::versions()
{
    QFETCH(QString, left);
    QFETCH(QString, right);
    QFETCH(int, order);
    bool valid = false;
    QCOMPARE(compareReleaseVersions(left, right, &valid), order);
    QVERIFY(valid);
}

void UpdateTest::rejectsInvalidVersions()
{
    for (const QString &version : {QStringLiteral("1.2"), QStringLiteral("01.2.3"),
                                   QStringLiteral("1.2.3-beta.01"), QStringLiteral("latest"),
                                   QStringLiteral("1.2.3/../../other")}) {
        bool valid = true;
        compareReleaseVersions(version, QStringLiteral("0.1.1"), &valid);
        QVERIFY(!valid);
    }
}

void UpdateTest::selectsStableAndBeta()
{
    const QByteArray json = QJsonDocument(QJsonArray{release(QStringLiteral("0.3.0-beta.2"), true),
        release(QStringLiteral("9.0.0"), false, true), release(QStringLiteral("0.1.9")),
        release(QStringLiteral("0.2.0"))}).toJson();
    auto stable = selectGitHubRelease(json, QStringLiteral("0.1.1"), false, QStringLiteral("windows-x64"), repository);
    QVERIFY(stable.hasRelease && stable.newer && stable.error.isEmpty());
    QCOMPARE(stable.release.version, QStringLiteral("v0.2.0"));
    auto beta = selectGitHubRelease(json, QStringLiteral("0.1.1"), true, QStringLiteral("windows-x64"), repository);
    QCOMPARE(beta.release.version, QStringLiteral("v0.3.0-beta.2"));
    QVERIFY(!selectGitHubRelease(json, QStringLiteral("0.2.0"), false,
                                  QStringLiteral("windows-x64"), repository).newer);
}

void UpdateTest::selectsMatchingAssetsAndRejectsForeignUrls()
{
    auto object = release(QStringLiteral("0.2.0"));
    auto json = QJsonDocument(QJsonArray{object}).toJson();
    auto mac = selectGitHubRelease(json, QStringLiteral("0.1.1"), false, QStringLiteral("macos-arm64"), repository);
    QCOMPARE(mac.release.assetName, QStringLiteral("snipaste-0.2.0-macos-universal.zip"));
    auto unavailable = selectGitHubRelease(json, QStringLiteral("0.1.1"), false, QStringLiteral("windows-arm64"), repository);
    QVERIFY(unavailable.newer && unavailable.release.assetUrl.isEmpty());
    auto foreign = asset(QStringLiteral("0.2.0"), QStringLiteral("windows-x64"));
    foreign.insert(QStringLiteral("browser_download_url"), QStringLiteral("https://example.org/download.exe"));
    object.insert(QStringLiteral("assets"), QJsonArray{foreign});
    json = QJsonDocument(QJsonArray{object}).toJson();
    QVERIFY(selectGitHubRelease(json, QStringLiteral("0.1.1"), false,
                                 QStringLiteral("windows-x64"), repository).release.assetUrl.isEmpty());
}

void UpdateTest::handlesEmptyAndInvalidCatalogs()
{
    auto empty = selectGitHubRelease("[]", QStringLiteral("0.1.1"), false, QStringLiteral("windows-x64"), repository);
    QVERIFY(!empty.hasRelease && empty.error.isEmpty());
    auto invalid = selectGitHubRelease("{bad JSON", QStringLiteral("0.1.1"), false, QStringLiteral("windows-x64"), repository);
    QVERIFY(!invalid.error.isEmpty());
}

void UpdateTest::networkCheckAndDuplicateRequests()
{
    HttpFixture server;
    server.body = QJsonDocument(QJsonArray{release(QStringLiteral("0.2.0"))}).toJson();
    GitHubUpdateClient client(repository, QStringLiteral("0.1.1"), nullptr, server.url());
    client.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    QSignalSpy checked(&client, &GitHubUpdateClient::checked);
    client.check(false);
    client.check(false);
    QTRY_COMPARE(checked.size(), 1);
    const auto result = qvariant_cast<ReleaseResult>(checked[0][0]);
    QVERIFY(result.newer);
    QCOMPARE(server.requests, 1);
    QVERIFY(!client.isBusy());
}

void UpdateTest::networkErrorsAndCancellation()
{
    HttpFixture server;
    server.status = 403;
    GitHubUpdateClient client(repository, QStringLiteral("0.1.1"), nullptr, server.url());
    client.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    QSignalSpy failed(&client, &GitHubUpdateClient::failed);
    QSignalSpy checked(&client, &GitHubUpdateClient::checked);
    client.check(false);
    QTRY_COMPARE(failed.size(), 1);
    QCOMPARE(checked.size(), 0);
    server.respond = false;
    client.check(false);
    QTRY_COMPARE(server.requests, 2);
    QSignalSpy canceled(&client, &GitHubUpdateClient::canceled);
    client.cancel();
    QCOMPARE(canceled.size(), 1);
    QVERIFY(!client.isBusy());
    QCOMPARE(failed.size(), 1);
}

void UpdateTest::downloadsAndChecksHash()
{
    HttpFixture server;
    server.body = QByteArray(8192, 'x');
    GitHubUpdateClient client(repository, QStringLiteral("0.1.1"), nullptr, server.url());
    client.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    ReleaseInfo info;
    info.assetName = QStringLiteral("snipaste-0.2.0-windows-x64.zip");
    info.assetUrl = server.url();
    info.assetSize = server.body.size();
    info.sha256 = QCryptographicHash::hash(server.body, QCryptographicHash::Sha256).toHex();
    QSignalSpy downloaded(&client, &GitHubUpdateClient::downloaded);
    client.download(info);
    QTRY_COMPARE(downloaded.size(), 1);
    QFile file(downloaded[0][0].toString());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), server.body);
    file.close();
    const QString directory = downloaded[0][1].toString();
    QVERIFY(directory.startsWith(QDir::tempPath() + QStringLiteral("/snipaste-update-")));
    QVERIFY(QDir(directory).removeRecursively());
}

void UpdateTest::discardsCorruptDownload()
{
    HttpFixture server;
    server.body = "corrupt payload";
    GitHubUpdateClient client(repository, QStringLiteral("0.1.1"), nullptr, server.url());
    client.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    ReleaseInfo info;
    info.assetName = QStringLiteral("snipaste-0.2.0-windows-x64.zip");
    info.assetUrl = server.url();
    info.assetSize = server.body.size();
    info.sha256 = QByteArray(64, '0');
    QSignalSpy failed(&client, &GitHubUpdateClient::failed);
    QSignalSpy downloaded(&client, &GitHubUpdateClient::downloaded);
    client.download(info);
    QTRY_COMPARE(failed.size(), 1);
    QCOMPARE(downloaded.size(), 0);
    QVERIFY(!client.isBusy());
}

void UpdateTest::downloadsUsingChecksumFile()
{
    HttpFixture server;
    const QByteArray payload(4096, 'z');
    const QString name = QStringLiteral("snipaste-0.2.0-windows-x64.zip");
    server.responses.insert("/checksum", QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex()
                                        + " *" + name.toUtf8() + "\n");
    server.responses.insert("/archive", payload);
    GitHubUpdateClient client(repository, QStringLiteral("0.1.1"), nullptr, server.url());
    client.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    ReleaseInfo info;
    info.assetName = name;
    info.assetUrl = server.url().resolved(QUrl(QStringLiteral("/archive")));
    info.checksumUrl = server.url().resolved(QUrl(QStringLiteral("/checksum")));
    info.assetSize = payload.size();
    QSignalSpy downloaded(&client, &GitHubUpdateClient::downloaded);
    client.download(info);
    QTRY_COMPARE(downloaded.size(), 1);
    QCOMPARE(server.requests, 2);
    const QString directory = downloaded[0][1].toString();
    QVERIFY(directory.startsWith(QDir::tempPath() + QStringLiteral("/snipaste-update-")));
    QVERIFY(QDir(directory).removeRecursively());

    server.responses.insert("/checksum", QByteArray(64, '0') + " *another-package.zip\n");
    QSignalSpy failed(&client, &GitHubUpdateClient::failed);
    client.download(info);
    QTRY_COMPARE(failed.size(), 1);
    QCOMPARE(server.requests, 3); // Never download a package using someone else's checksum.
    QCOMPARE(downloaded.size(), 1);
}

QTEST_GUILESS_MAIN(UpdateTest)
#include "update_test.moc"
