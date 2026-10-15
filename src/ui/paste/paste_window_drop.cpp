#include "ui/paste/paste_manager.h"
#include "ui/paste/paste_window.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>

namespace snipaste::ui
{
void PasteWindow::applyDroppedImage(const QImage &image)
{
    if (image.isNull())
        return;
    if (m_keepOnDrag && m_pasteManager)
    {
        m_pasteManager->createPasteAtPosition(image, frameGeometry().topLeft());
        return;
    }
    setSourceImage(image);
}
void PasteWindow::dragEnterEvent(QDragEnterEvent *event)
{
    const QMimeData *mime = event->mimeData();
    if (mime && (mime->hasImage() || mime->hasUrls()))
        event->acceptProposedAction();
    else
        event->ignore();
}
void PasteWindow::dropEvent(QDropEvent *event)
{
    const QMimeData *mime = event->mimeData();
    if (!mime)
    {
        event->ignore();
        return;
    }
    QImage image;
    if (mime->hasImage())
        image = qvariant_cast<QImage>(mime->imageData());
    // Browsers usually expose dragged images as text/html instead of a
    // file URL.  Resolve the first image source so dragging directly from a
    // page behaves like copying the image and pasting it.
    if (image.isNull() && mime->hasHtml())
    {
        static const QRegularExpression imageSource(QStringLiteral("<img[^>]+src\\s*=\\s*['\"]([^'\"]+)"),
                                                    QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch match = imageSource.match(mime->html());
        if (match.hasMatch())
        {
            const QUrl sourceUrl(match.captured(1));
            if (sourceUrl.isValid() && sourceUrl.scheme() == QStringLiteral("data"))
            {
                const QByteArray encoded = sourceUrl.toString().section(QChar(','), 1).toUtf8();
                image = QImage::fromData(QByteArray::fromBase64(encoded));
            }
            else if (sourceUrl.isValid() && (sourceUrl.scheme() == QStringLiteral("http") ||
                                             sourceUrl.scheme() == QStringLiteral("https")))
            {
                downloadDroppedImage(sourceUrl);
                event->acceptProposedAction();
                return;
            }
            else if (sourceUrl.isLocalFile())
            {
                image.load(sourceUrl.toLocalFile());
            }
        }
    }
    if (image.isNull() && mime->hasUrls())
    {
        for (const QUrl &url : mime->urls())
        {
            if (url.isLocalFile())
            {
                QImage candidate(url.toLocalFile());
                if (!candidate.isNull())
                {
                    image = std::move(candidate);
                    break;
                }
            }
            else if (url.scheme() == QStringLiteral("http") || url.scheme() == QStringLiteral("https"))
            {
                downloadDroppedImage(url);
                event->acceptProposedAction();
                return;
            }
        }
    }
    if (image.isNull())
    {
        event->ignore();
        return;
    }
    applyDroppedImage(image);
    event->acceptProposedAction();
}
void PasteWindow::downloadDroppedImage(const QUrl &url)
{
    auto *network = new QNetworkAccessManager(this);
    auto *reply = network->get(QNetworkRequest(url));
    // Abort excessive downloads while receiving, rather than buffering an
    // arbitrarily large response and checking its size only after completion.
    connect(reply, &QNetworkReply::downloadProgress, reply, [this, reply](qint64 received, qint64 total) {
        if (m_maxNetworkImageBytes > 0 &&
            (received > m_maxNetworkImageBytes || total > m_maxNetworkImageBytes))
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, network] {
        if (reply->error() == QNetworkReply::NoError)
        {
            const auto data = reply->readAll();
            if (m_maxNetworkImageBytes <= 0 || data.size() <= m_maxNetworkImageBytes)
            {
                const auto image = QImage::fromData(data);
                if (!image.isNull())
                    applyDroppedImage(image);
            }
        }
        reply->deleteLater();
        network->deleteLater();
    });
}
} // namespace snipaste::ui
