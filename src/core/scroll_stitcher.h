#pragma once

#include <QImage>

namespace snipaste::core {

// Incrementally combines viewport images captured while a window is scrolled
// down.  The first frame is kept intact; later frames are aligned by matching
// their top rows against the bottom of the accumulated image.
class VerticalScrollStitcher final
{
public:
    explicit VerticalScrollStitcher(int maximumHeight = 24000);

    void clear();
    // Returns true when the frame added new content.  An unchanged viewport
    // is ignored so automatic scrolling can use the return value as an end
    // signal.
    bool append(const QImage &frame);

    const QImage &image() const { return m_image; }
    int frameCount() const { return m_frameCount; }
    int lastOverlap() const { return m_lastOverlap; }

private:
    static int overlapError(const QImage &base, const QImage &frame, int overlap);
    static bool sameFrame(const QImage &left, const QImage &right);

    QImage m_image;
    QImage m_lastFrame;
    int m_maximumHeight = 24000;
    int m_frameCount = 0;
    int m_lastOverlap = 0;
};

} // namespace snipaste::core
