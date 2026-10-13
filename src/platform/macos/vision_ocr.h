#pragma once

#include "core/recognition/ocr_service.h"

#include <QImage>

namespace snipate::platform {

// Runs Apple's Vision text recognizer for a still image.  The implementation
// lives in Objective-C++ so the rest of the application can remain portable
// C++/Qt code.
core::OcrResult recognizeWithMacVision(const QImage &image);

} // namespace snipate::platform
