#pragma once

#include <QImage>
#include <utility>

namespace snipaste::core {

// Bounded sampling is only a motion gate. The stitcher still matches and
// preserves native pixels, and rejects frames without a reliable overlap.
class ScrollCaptureStability final
{
public:
    void reset() { m_sample = {}; m_stableSince = -1; }

    bool observe(const QImage &frame, qint64 elapsedMs)
    {
        if (frame.isNull()) {
            reset();
            return false;
        }
        QImage sample = frame.scaled(QSize(160, 120), Qt::IgnoreAspectRatio, Qt::FastTransformation)
                             .convertToFormat(QImage::Format_RGB32);
        if (sample != m_sample) {
            m_sample = std::move(sample);
            m_stableSince = elapsedMs;
            return false;
        }
        return m_stableSince >= 0 && elapsedMs - m_stableSince >= 80;
    }

private:
    QImage m_sample;
    qint64 m_stableSince = -1;
};

} // namespace snipaste::core
