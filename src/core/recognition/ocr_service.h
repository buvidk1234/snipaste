#pragma once

#include <QImage>
#include <QString>

namespace snipaste::core {

struct OcrResult {
    bool success = false;
    QString text;
    QString error;
};

class OcrService final
{
public:
    // engine accepts the preference labels (for example "系统自带" or
    // "Tesseract"); language accepts the preference labels or a native
    // language code. Empty values preserve the historical automatic behavior.
    static OcrResult recognize(const QImage &image,
                               const QString &engine = QString(),
                               const QString &language = QString());
};

} // namespace snipaste::core
