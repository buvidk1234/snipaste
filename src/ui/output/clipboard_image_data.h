#pragma once

#include <QImage>
#include <QMimeData>

namespace snipaste::ui
{

// Keep one shared image while offering the Bitmap compatibility format.
// Clipboard consumers request the encoded bytes only when they paste.
class ClipboardImageData final : public QMimeData
{
  public:
    ClipboardImageData(const QImage &image, bool includeWindowsBitmap);
    QStringList formats() const override;
    bool hasFormat(const QString &mimeType) const override;

  protected:
    QVariant retrieveData(const QString &mimeType, QMetaType preferredType) const override;

  private:
    bool m_includeWindowsBitmap;
};

} // namespace snipaste::ui
