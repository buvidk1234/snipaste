#include "platform/windows/single_instance.h"

#include <QDataStream>
#include <QElapsedTimer>
#include <QFuture>
#include <QFutureWatcher>
#include <QLocalSocket>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>
#include <QUuid>
#include <QtConcurrentRun>

using snipaste::platform::SingleInstance;

namespace {

QString serverKey()
{
    return QStringLiteral("snipaste-ipc-test-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

} // namespace

class SingleInstanceTest : public QObject
{
    Q_OBJECT

private slots:
    void messageReportsSuccessfulDelivery();
    void senderOwnsOneWayConnectionLifetime();
    void largeMessageArrivesIntact();
    void waitsForExplicitCompletion();
    void immediateCompletionIsAccepted();
    void missingCompletionTimesOut();
    void missingServerFails();
};

void SingleInstanceTest::messageReportsSuccessfulDelivery()
{
    SingleInstance server;
    const QString key = serverKey();
    QVERIFY(server.acquire(key));
    QSignalSpy received(&server, &SingleInstance::messageReceived);
    const QStringList arguments{QStringLiteral("paste"), QStringLiteral("中文参数"), QString()};

    QFutureWatcher<bool> sent;
    QSignalSpy finished(&sent, &QFutureWatcher<bool>::finished);
    QElapsedTimer elapsed;
    elapsed.start();
    sent.setFuture(QtConcurrent::run([key, arguments] {
        SingleInstance client;
        return client.sendMessage(key, arguments);
    }));
    QVERIFY(finished.size() > 0 || finished.wait(3000));
    QVERIFY2(sent.result(), qPrintable(QStringLiteral("IPC send failed after %1 ms; received %2 message(s)")
                                         .arg(elapsed.elapsed()).arg(received.size())));
    QTRY_COMPARE_WITH_TIMEOUT(received.size(), 1, 3000);
    QCOMPARE(received.first().first().toStringList(), arguments);
}

void SingleInstanceTest::senderOwnsOneWayConnectionLifetime()
{
    SingleInstance server;
    const QString key = serverKey();
    QVERIFY(server.acquire(key));
    QSignalSpy received(&server, &SingleInstance::messageReceived);
    QPointer<QLocalSocket> accepted;
    connect(&server, &SingleInstance::messageReceivedWithSocket, &server,
        [&accepted](const QStringList &, QLocalSocket *socket) { accepted = socket; });
    const QStringList arguments{QStringLiteral("paste")};
    QByteArray payload;
    QDataStream payloadStream(&payload, QIODevice::WriteOnly);
    payloadStream << arguments;

    QLocalSocket client;
    client.connectToServer(key, QIODevice::WriteOnly);
    QVERIFY(client.waitForConnected(1000));
    QDataStream stream(&client);
    stream << quint32(payload.size());
    QCOMPARE(stream.writeRawData(payload.constData(), payload.size()), payload.size());
    client.flush();
    QVERIFY(received.size() > 0 || received.wait(3000));
    QCOMPARE(received.first().first().toStringList(), arguments);
    // A receiver-side close can race Windows' final pipe-write notification.
    // The short-lived sender closes the connection after its writes complete.
    QVERIFY(accepted);
    QCOMPARE(accepted->state(), QLocalSocket::ConnectedState);
    QCOMPARE(client.state(), QLocalSocket::ConnectedState);

    client.disconnectFromServer();
    QTRY_VERIFY_WITH_TIMEOUT(accepted.isNull(), 3000);
}

void SingleInstanceTest::largeMessageArrivesIntact()
{
    SingleInstance server;
    const QString key = serverKey();
    QVERIFY(server.acquire(key));
    QSignalSpy received(&server, &SingleInstance::messageReceived);
    const QStringList arguments{QStringLiteral("large"), QString(256 * 1024, QChar(0x4e2d))};

    QFutureWatcher<bool> sent;
    QSignalSpy finished(&sent, &QFutureWatcher<bool>::finished);
    QElapsedTimer elapsed;
    elapsed.start();
    sent.setFuture(QtConcurrent::run([key, arguments] {
        SingleInstance client;
        return client.sendMessage(key, arguments);
    }));
    // Keep the receiver's event loop running while the sender drains its
    // buffer; QTRY polling sleeps between event-processing passes.
    QVERIFY(finished.size() > 0 || finished.wait(3000));
    QVERIFY2(sent.result(), qPrintable(QStringLiteral("Large IPC send failed after %1 ms")
                                         .arg(elapsed.elapsed())));
    QTRY_COMPARE_WITH_TIMEOUT(received.size(), 1, 3000);
    QCOMPARE(received.first().first().toStringList(), arguments);
}

void SingleInstanceTest::waitsForExplicitCompletion()
{
    SingleInstance server;
    const QString key = serverKey();
    QVERIFY(server.acquire(key));
    QLocalSocket *requestSocket = nullptr;
    QStringList received;
    connect(&server, &SingleInstance::messageReceivedWithSocket,
        [&requestSocket, &received](const QStringList &arguments, QLocalSocket *socket) {
            requestSocket = socket;
            received = arguments;
        });

    const auto sent = QtConcurrent::run([key] {
        SingleInstance client;
        return client.sendMessageAndWait(key, {QStringLiteral("snip")}, 5000);
    });
    QTRY_VERIFY_WITH_TIMEOUT(requestSocket, 3000);
    QCOMPARE(received, (QStringList{QStringLiteral("snip"), QStringLiteral("--snipaste-await")}));
    QVERIFY(!sent.isFinished());
    server.finishMessage(requestSocket);
    QTRY_VERIFY_WITH_TIMEOUT(sent.isFinished(), 3000);
    QVERIFY(sent.result());
}

void SingleInstanceTest::immediateCompletionIsAccepted()
{
    SingleInstance server;
    const QString key = serverKey();
    QVERIFY(server.acquire(key));
    connect(&server, &SingleInstance::messageReceivedWithSocket, &server,
        [&server](const QStringList &, QLocalSocket *socket) { server.finishMessage(socket); });

    const auto sent = QtConcurrent::run([key] {
        SingleInstance client;
        return client.sendMessageAndWait(key, {QStringLiteral("snip")}, 1000);
    });
    QTRY_VERIFY_WITH_TIMEOUT(sent.isFinished(), 3000);
    QVERIFY(sent.result());
}

void SingleInstanceTest::missingCompletionTimesOut()
{
    SingleInstance server;
    const QString key = serverKey();
    QVERIFY(server.acquire(key));
    QSignalSpy received(&server, &SingleInstance::messageReceived);

    const auto sent = QtConcurrent::run([key] {
        SingleInstance client;
        return client.sendMessageAndWait(key, {QStringLiteral("snip")}, 100);
    });
    QTRY_VERIFY_WITH_TIMEOUT(sent.isFinished(), 3000);
    QVERIFY(!sent.result());
    QTRY_COMPARE_WITH_TIMEOUT(received.size(), 1, 3000);
}

void SingleInstanceTest::missingServerFails()
{
    SingleInstance client;
    const QString key = serverKey();
    QVERIFY(!client.sendMessage(key, {QStringLiteral("snip")}));
    QVERIFY(!client.sendMessageAndWait(key, {QStringLiteral("snip")}, 10));
}

QTEST_GUILESS_MAIN(SingleInstanceTest)
#include "single_instance_test.moc"
