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
    static OcrResult recognize(const QImage &image);
};

} // namespace snipaste::core
