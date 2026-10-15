#include "core/ai/ai_client.h"
#include "core/config/config_store.h"

#include <QBuffer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace snipaste::core
{
AiSettings AiSettings::fromConfig(const ConfigStore &config)
{
    AiSettings settings;
    settings.apiKey = config.value(QStringLiteral("ai/apiKey")).toString().trimmed();
    settings.baseUrl = config.value(QStringLiteral("ai/baseUrl"), settings.baseUrl).toString().trimmed();
    settings.model = config.value(QStringLiteral("ai/model")).toString().trimmed();
    return settings;
}

AiClient::AiClient(AiSettings settings, QImage image, QObject *parent)
    : QObject(parent), m_settings(std::move(settings)), m_image(std::move(image))
{
}

AiClient::~AiClient()
{
    cancel();
}

void AiClient::cancel()
{
    if (!m_reply)
        return;
    auto *reply = m_reply.data();
    m_reply.clear();
    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
}

void AiClient::send(const QString &question)
{
    if (isBusy() || question.trimmed().isEmpty())
        return;
    if (m_settings.apiKey.isEmpty() || m_settings.model.isEmpty())
    {
        emit failed(QStringLiteral("请先在「首选项 → AI」中填写 API Key 和支持图片输入的模型。"));
        return;
    }
    QUrl url(m_settings.baseUrl);
    if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty() || url.hasQuery() ||
        url.hasFragment() || (url.scheme() != QStringLiteral("https") && url.scheme() != QStringLiteral("http")))
    {
        emit failed(QStringLiteral("请在「首选项 → AI」中填写有效的接口地址。"));
        return;
    }
    QString path = url.path();
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    if (!path.endsWith(QStringLiteral("/responses")))
        path += QStringLiteral("/responses");
    url.setPath(path);

    QJsonArray content{QJsonObject{{QStringLiteral("type"), QStringLiteral("input_text")},
                                 {QStringLiteral("text"), question.trimmed()}}};
    if (m_history.isEmpty())
    {
        QByteArray png;
        QBuffer buffer(&png);
        if (m_image.isNull() || !buffer.open(QIODevice::WriteOnly) || !m_image.save(&buffer, "PNG"))
        {
            emit failed(QStringLiteral("无法读取选区图片，请重新截图。"));
            return;
        }
        content.append(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("input_image")},
            {QStringLiteral("image_url"), QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64())}});
    }
    QJsonArray input = m_history;
    input.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                             {QStringLiteral("content"), content}});
    const QJsonObject body{{QStringLiteral("model"), m_settings.model},
                           {QStringLiteral("input"), input},
                           {QStringLiteral("tools"), QJsonArray{QJsonObject{
                               {QStringLiteral("type"), QStringLiteral("image_generation")}}}},
                           {QStringLiteral("store"), false},
                           {QStringLiteral("include"), QJsonArray{QStringLiteral("reasoning.encrypted_content")}}};
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", "Bearer " + m_settings.apiKey.toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(300000);
    auto *reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, input] {
        m_reply.clear();
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300)
        {
            QString error;
            if (status == 400)
                error = QStringLiteral("请求被服务拒绝，请确认接口和模型支持 Responses API 及图片生成工具。");
            else if (status == 401 || status == 403)
                error = QStringLiteral("认证失败，请检查 API Key 和模型访问权限。");
            else if (status == 429)
                error = QStringLiteral("请求受限或额度不足，请稍后重试。");
            else if (status == 404 || status == 405)
                error = QStringLiteral("接口或模型不可用，请检查地址、模型名以及服务是否支持 Responses API。");
            else if (status >= 300)
                error = QStringLiteral("AI 请求失败（HTTP %1），请检查接口配置或稍后重试。").arg(status);
            else
                error = QStringLiteral("连接 AI 服务失败：%1").arg(reply->errorString());
            emit failed(error);
            return;
        }
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(reply->readAll(), &parseError);
        const auto response = document.object();
        const QString state = response.value(QStringLiteral("status")).toString();
        if (parseError.error != QJsonParseError::NoError || !document.isObject() ||
            (response.contains(QStringLiteral("error")) && !response.value(QStringLiteral("error")).isNull()) ||
            (!state.isEmpty() && state != QStringLiteral("completed")))
        {
            emit failed(QStringLiteral("AI 未能完成回答，请重试或检查服务的 Responses API 支持。"));
            return;
        }
        const QJsonArray output = response.value(QStringLiteral("output")).toArray();
        QStringList texts;
        QList<QImage> images;
        for (const auto &item : output)
        {
            const auto message = item.toObject();
            if (message.value(QStringLiteral("type")) == QStringLiteral("image_generation_call"))
            {
                const QString imageState = message.value(QStringLiteral("status")).toString();
                if (!imageState.isEmpty() && imageState != QStringLiteral("completed"))
                {
                    emit failed(QStringLiteral("AI 未能生成图片，请重试或检查服务的生图支持。"));
                    return;
                }
                const auto image = QImage::fromData(QByteArray::fromBase64(
                    message.value(QStringLiteral("result")).toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors));
                if (image.isNull())
                {
                    emit failed(QStringLiteral("AI 返回的图片无法读取，请重试。"));
                    return;
                }
                images.append(image);
                continue;
            }
            if (message.value(QStringLiteral("type")) != QStringLiteral("message"))
                continue;
            for (const auto &part : message.value(QStringLiteral("content")).toArray())
            {
                const auto value = part.toObject();
                if (value.value(QStringLiteral("type")) == QStringLiteral("output_text"))
                    texts.append(value.value(QStringLiteral("text")).toString());
                else if (value.value(QStringLiteral("type")) == QStringLiteral("refusal"))
                    texts.append(value.value(QStringLiteral("refusal")).toString());
            }
        }
        const QString text = texts.join(QLatin1Char('\n')).trimmed();
        if (text.isEmpty() && images.isEmpty())
        {
            emit failed(QStringLiteral("AI 没有返回文字或图片，请检查模型及接口支持。"));
            return;
        }
        m_history = input;
        // Replay the complete output, including encrypted reasoning, on follow-up turns.
        for (const auto &item : output)
            m_history.append(item);
        m_image = {};
        emit answered(text, images);
    });
}
} // namespace snipaste::core
