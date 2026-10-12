#include "platform/windows/single_instance.h"

#include <QDataStream>
#include <QLocalSocket>
#include <QVariant>

namespace {

constexpr quint32 kMaxMessageSize = 16 * 1024 * 1024;
constexpr char kCompletionByte = '\x06';

QByteArray encodeArguments(const QStringList &arguments)
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream << arguments;
    return payload;
}

}

namespace snipaste::platform {

SingleInstance::SingleInstance(QObject *parent)
    : QObject(parent)
{
}

bool SingleInstance::acquire(const QString &key)
{
    QLocalSocket probe;
    probe.connectToServer(key, QIODevice::WriteOnly);
    if (probe.waitForConnected(150)) {
        probe.disconnectFromServer();
        return false;
    }

    QLocalServer::removeServer(key);
    if (!m_server.listen(key))
        return false;
    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (m_server.hasPendingConnections()) {
            QLocalSocket *socket = m_server.nextPendingConnection();
            connect(socket, &QLocalSocket::readyRead, socket, [this, socket] {
                QByteArray buffer = socket->property("snipasteMessageBuffer").toByteArray();
                buffer.append(socket->readAll());
                QDataStream stream(buffer);
                while (stream.status() == QDataStream::Ok) {
                    const qint64 messageStart = stream.device()->pos();
                    if (stream.device()->bytesAvailable() < qint64(sizeof(quint32)))
                        break;
                    quint32 length = 0;
                    stream >> length;
                    if (stream.status() != QDataStream::Ok || length > kMaxMessageSize)
                        break;
                    if (stream.device()->bytesAvailable() < length) {
                        stream.resetStatus();
                        stream.device()->seek(messageStart);
                        break;
                    }
                    QByteArray payload;
                    payload.resize(int(length));
                    if (stream.readRawData(payload.data(), int(length)) != int(length)) {
                        stream.resetStatus();
                        stream.device()->seek(messageStart);
                        break;
                    }
                    QDataStream payloadStream(payload);
                    QStringList arguments;
                    payloadStream >> arguments;
                    if (payloadStream.status() == QDataStream::Ok) {
                        socket->setProperty("snipasteLastArguments", arguments);
                        emit messageReceived(arguments);
                        emit messageReceivedWithSocket(arguments, socket);
                    }
                }
                const qint64 consumed = stream.device()->pos();
                if (consumed > 0 && consumed <= buffer.size())
                    buffer.remove(0, int(consumed));
                socket->setProperty("snipasteMessageBuffer", buffer);
                const QStringList arguments = socket->property("snipasteLastArguments").toStringList();
                const bool awaitCompletion = arguments.contains(QStringLiteral("--snipaste-await"));
                if (buffer.isEmpty() && !awaitCompletion)
                    socket->disconnectFromServer();
            });
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    return true;
}

bool SingleInstance::sendMessage(const QString &key, const QStringList &arguments) const
{
    QLocalSocket socket;
    socket.connectToServer(key, QIODevice::WriteOnly);
    if (!socket.waitForConnected(250))
        return false;
    const QByteArray payload = encodeArguments(arguments);
    if (payload.size() > int(kMaxMessageSize))
        return false;
    QDataStream stream(&socket);
    stream << quint32(payload.size());
    if (payload.isEmpty())
        return false;
    stream.writeRawData(payload.constData(), payload.size());
    if (stream.status() != QDataStream::Ok)
        return false;
    socket.flush();
    return socket.waitForBytesWritten(250);
}

bool SingleInstance::sendMessageAndWait(const QString &key, const QStringList &arguments,
                                        int timeoutMs) const
{
    QLocalSocket socket;
    socket.connectToServer(key, QIODevice::ReadWrite);
    if (!socket.waitForConnected(500))
        return false;
    QStringList request = arguments;
    if (!request.contains(QStringLiteral("--snipaste-await")))
        request.append(QStringLiteral("--snipaste-await"));
    const QByteArray payload = encodeArguments(request);
    if (payload.isEmpty() || payload.size() > int(kMaxMessageSize))
        return false;
    QDataStream stream(&socket);
    stream << quint32(payload.size());
    stream.writeRawData(payload.constData(), payload.size());
    if (stream.status() != QDataStream::Ok || !socket.waitForBytesWritten(500))
        return false;
    const int boundedTimeout = qBound(1, timeoutMs, 24 * 60 * 60 * 1000);
    if (!socket.waitForReadyRead(boundedTimeout))
        return false;
    return socket.readAll().contains(kCompletionByte);
}

void SingleInstance::finishMessage(QLocalSocket *socket) const
{
    if (!socket || socket->state() != QLocalSocket::ConnectedState)
        return;
    socket->write(QByteArray(1, kCompletionByte));
    socket->flush();
    socket->waitForBytesWritten(500);
    socket->disconnectFromServer();
}

} // namespace snipaste::platform
