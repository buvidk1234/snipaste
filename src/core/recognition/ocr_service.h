#pragma once

#include <QByteArray>
#include <QImage>
#include <QRectF>
#include <QString>
#include <QVector>

namespace snipaste::core {

struct OcrTextBox {
    QRectF bounds; // Original image pixels, independent of recognition scaling/DPR.
    qsizetype start = 0;
    qsizetype length = 0;
    int line = 0;
};

struct OcrResult {
    bool success = false;
    QString text;
    QString error;
    QVector<OcrTextBox> boxes{};
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
    static OcrResult parseTesseractTsv(const QByteArray &data);
};

} // namespace snipaste::core
