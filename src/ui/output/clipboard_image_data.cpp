#include "ui/output/clipboard_image_data.h"

#include <QBuffer>

namespace snipaste::ui
{

ClipboardImageData::ClipboardImageData(const QImage &image, bool includeWindowsBitmap)
    : m_includeWindowsBitmap(includeWindowsBitmap && !image.isNull())
{
    setImageData(image);
}

QStringList ClipboardImageData::formats() const
{
    QStringList result = QMimeData::formats();
    if (m_includeWindowsBitmap)
        result.append(QStringLiteral("image/bmp"));
    return result;
}

bool ClipboardImageData::hasFormat(const QString &mimeType) const
{
    return (m_includeWindowsBitmap && mimeType == QStringLiteral("image/bmp")) ||
           QMimeData::hasFormat(mimeType);
}

QVariant ClipboardImageData::retrieveData(const QString &mimeType, QMetaType preferredType) const
{
    if (m_includeWindowsBitmap && mimeType == QStringLiteral("image/bmp"))
    {
        QByteArray bytes;
        QBuffer buffer(&bytes);
        if (!buffer.open(QIODevice::WriteOnly) || !qvariant_cast<QImage>(imageData()).save(&buffer, "BMP"))
            return {};
        // Do not cache the encoded copy: the shared image is sufficient for
        // later requests, and retaining both costs another full frame at idle.
        return bytes;
    }
    return QMimeData::retrieveData(mimeType, preferredType);
}

} // namespace snipaste::ui
