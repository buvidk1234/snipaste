#pragma once

#include <QObject>
#include <QLocalServer>
#include <QStringList>

class QLocalSocket;

namespace snipate::platform {

class SingleInstance final : public QObject
{
    Q_OBJECT

public:
    explicit SingleInstance(QObject *parent = nullptr);
    bool acquire(const QString &key);
    bool sendMessage(const QString &key, const QStringList &arguments) const;
    bool sendMessageAndWait(const QString &key, const QStringList &arguments,
                            int timeoutMs = 120000) const;
    void finishMessage(QLocalSocket *socket) const;

signals:
    void messageReceived(const QStringList &arguments);
    void messageReceivedWithSocket(const QStringList &arguments, QLocalSocket *socket);

private:
    QLocalServer m_server;
};

} // namespace snipate::platform
