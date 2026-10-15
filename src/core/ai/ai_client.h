#pragma once

#include <QImage>
#include <QJsonArray>
#include <QList>
#include <QNetworkAccessManager>
#include <QPointer>

class QNetworkReply;

namespace snipaste::core
{
class ConfigStore;

struct AiSettings
{
    QString apiKey;
    QString baseUrl = QStringLiteral("https://api.openai.com/v1");
    QString model;

    static AiSettings fromConfig(const ConfigStore &config);
};

class AiClient final : public QObject
{
    Q_OBJECT
  public:
    AiClient(AiSettings settings, QImage image, QObject *parent = nullptr);
    ~AiClient() override;
    bool isBusy() const { return !m_reply.isNull(); }
    void send(const QString &question);
    void cancel();

  signals:
    void answered(const QString &text, const QList<QImage> &images);
    void failed(const QString &message);

  private:
    AiSettings m_settings;
    QImage m_image;
    QJsonArray m_history;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
};
} // namespace snipaste::core
