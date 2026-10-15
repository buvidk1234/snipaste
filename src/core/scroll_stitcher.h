#pragma once

#include <QImage>
#include <QVector>

class QPainter;

namespace snipaste::core {

// Incrementally combines viewport images captured while a window is scrolled
// down.  The first frame is kept intact; later frames are aligned by matching
// their top rows against the bottom of the accumulated image.
class VerticalScrollStitcher final
{
public:
    enum class AppendResult { Added, Unchanged, NoMatch, Ambiguous, SizeChanged, HeightLimit, InvalidFrame };
    explicit VerticalScrollStitcher(int maximumHeight = 24000);

    void clear();
    // False also includes rejected/ambiguous frames. Callers must inspect
    // lastResult() before treating a non-appended frame as end of scrolling.
    bool append(const QImage &frame);

    // Flatten only for consumers that need a contiguous export. Copies of
    // the returned image remain valid when more frames are appended.
    const QImage &image() const;
    QImage takeImage();
    QSize size() const { return m_size; }
    // Draw native-pixel strips through the caller's transform and clip.
    // A scrolling preview can paint its viewport without flattening the image.
    void draw(QPainter &painter, const QRectF &exposed) const;
    int frameCount() const { return m_frameCount; }
    int lastOverlap() const { return m_lastOverlap; }
    AppendResult lastResult() const { return m_lastResult; }

private:
    const uchar *scanLine(int y) const;

    struct Strip {
        int top;
        QImage pixels;
    };
    mutable QVector<Strip> m_strips;
    QSize m_size;
    QSize m_viewportSize;
    mutable QImage m_image;
    int m_maximumHeight = 24000;
    int m_frameCount = 0;
    int m_lastOverlap = 0;
    AppendResult m_lastResult = AppendResult::InvalidFrame;
};

} // namespace snipaste::core
