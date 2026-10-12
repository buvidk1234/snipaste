#pragma once

#include <QImage>
#include <QString>

namespace snipate::core {

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

} // namespace snipate::core
