#pragma once

#include <QImage>
#include <functional>

namespace snipaste::core {

struct ScrollFrameMatch {
    enum class Status { Matched, Unchanged, NoMatch, Ambiguous };
    Status status = Status::NoMatch;
    int overlap = 0;
};

// The reader exposes accumulated rows without materializing a long image.
ScrollFrameMatch matchScrollFrame(const QImage &frame, int accumulatedHeight,
                                 const std::function<const QRgb *(int)> &readRow);

} // namespace snipaste::core
