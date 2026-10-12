#pragma once

#include <QImage>
#include <QString>

namespace snipate::core {

struct BarcodeResult {
    bool success = false;
    QString text;
    QString error;
};

class BarcodeService final
{
public:
    static BarcodeResult recognize(const QImage &image);
};

} // namespace snipate::core
