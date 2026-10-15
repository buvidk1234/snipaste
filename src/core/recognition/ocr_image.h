#pragma once

#include <QImage>
#include "core/recognition/ocr_service.h"

namespace snipaste::core::detail {

// Keep the original screenshot untouched. Normal high-contrast light images
// share their existing pixels; dark/low-contrast text gets a recognition copy.
QImage enhanceOcrContrast(const QImage &source);
OcrResult mergeLowContrastText(const QImage &source, const OcrResult &original,
                              const OcrResult &enhanced);

} // namespace snipaste::core::detail
