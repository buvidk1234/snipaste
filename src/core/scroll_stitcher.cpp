#include "core/scroll_stitcher.h"

#include <QPainter>
#include <QtGlobal>
#include <algorithm>

namespace snipaste::core {

namespace {

QImage normalized(const QImage &image)
{
    QImage result = image.convertToFormat(QImage::Format_RGB32);
    result.setDevicePixelRatio(1.0);
    return result;
}

int channelDistance(const QRgb left, const QRgb right)
{
    return qAbs(qRed(left) - qRed(right))
        + qAbs(qGreen(left) - qGreen(right))
        + qAbs(qBlue(left) - qBlue(right));
}

} // namespace

VerticalScrollStitcher::VerticalScrollStitcher(int maximumHeight)
    : m_maximumHeight(qMax(1, maximumHeight))
{
}

void VerticalScrollStitcher::clear()
{
    m_image = {};
    m_lastFrame = {};
    m_frameCount = 0;
    m_lastOverlap = 0;
}

bool VerticalScrollStitcher::sameFrame(const QImage &left, const QImage &right)
{
    if (left.size() != right.size())
        return false;
    if (left.isNull() || right.isNull())
        return left.isNull() && right.isNull();

    const int width = left.width();
    const int height = left.height();
    const int xStep = qMax(1, width / 12);
    const int yStep = qMax(1, height / 12);
    qint64 error = 0;
    int samples = 0;
    for (int y = 0; y < height; y += yStep) {
        for (int x = 0; x < width; x += xStep) {
            error += channelDistance(left.pixel(x, y), right.pixel(x, y));
            ++samples;
        }
    }
    return samples > 0 && error / samples <= 2;
}

int VerticalScrollStitcher::overlapError(const QImage &base,
                                         const QImage &frame,
                                         int overlap)
{
    if (overlap <= 0 || base.width() != frame.width()
        || overlap > base.height() || overlap > frame.height()) {
        return 255 * 3;
    }

    const int xStep = qMax(1, frame.width() / 16);
    const int yStep = qMax(1, overlap / 18);
    qint64 error = 0;
    int samples = 0;
    for (int y = 0; y < overlap; y += yStep) {
        const int baseY = base.height() - overlap + y;
        for (int x = 0; x < frame.width(); x += xStep) {
            error += channelDistance(base.pixel(x, baseY), frame.pixel(x, y));
            ++samples;
        }
    }
    return samples > 0 ? int(error / samples) : 255 * 3;
}

bool VerticalScrollStitcher::append(const QImage &source)
{
    if (source.isNull() || source.width() <= 0 || source.height() <= 0)
        return false;

    QImage frame = normalized(source);
    if (m_image.isNull()) {
        if (frame.height() > m_maximumHeight)
            frame = frame.copy(0, 0, frame.width(), m_maximumHeight);
        m_image = frame;
        m_lastFrame = frame;
        m_frameCount = 1;
        m_lastOverlap = 0;
        return true;
    }

    if (sameFrame(m_lastFrame, frame)) {
        m_lastFrame = frame;
        return false;
    }

    if (frame.width() != m_image.width())
        frame = frame.scaled(QSize(m_image.width(),
                                   qMax(1, qRound(qreal(frame.height())
                                                  * m_image.width() / frame.width()))),
                              Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    const int maxOverlap = qMin(m_image.height(), frame.height() - 1);
    const int minOverlap = qMax(1, qMin(maxOverlap, frame.height() / 12));
    int bestOverlap = 0;
    int bestError = 255 * 3;
    // Prefer the largest matching overlap.  A user may scroll by a small
    // amount, so checking from large to small also avoids a short accidental
    // match inside a repeated text row.
    for (int overlap = maxOverlap; overlap >= minOverlap; overlap -= 2) {
        const int error = overlapError(m_image, frame, overlap);
        if (error < bestError) {
            bestError = error;
            bestOverlap = overlap;
            if (error == 0)
                break;
        }
    }

    // The coarse pass is strided to keep a tall capture cheap, but the real
    // scroll distance may land between sampled rows. Refine the best
    // neighborhood so an eight-pixel overlap is not missed when the coarse
    // pass visited seven and nine.
    if (bestOverlap > 0) {
        const int first = qMax(minOverlap, bestOverlap - 2);
        const int last = qMin(maxOverlap, bestOverlap + 2);
        for (int overlap = first; overlap <= last; ++overlap) {
            const int error = overlapError(m_image, frame, overlap);
            if (error < bestError) {
                bestError = error;
                bestOverlap = overlap;
                if (error == 0)
                    break;
            }
        }
    }

    if (bestError > 28)
        bestOverlap = 0;

    const int addedHeight = frame.height() - bestOverlap;
    if (addedHeight <= 2) {
        m_lastFrame = frame;
        m_lastOverlap = bestOverlap;
        return false;
    }
    const int available = m_maximumHeight - m_image.height();
    if (available <= 0) {
        m_lastFrame = frame;
        m_lastOverlap = bestOverlap;
        return false;
    }

    const int copyHeight = qMin(available, addedHeight);
    QImage combined(m_image.width(), m_image.height() + copyHeight,
                    QImage::Format_RGB32);
    combined.fill(Qt::black);
    QPainter painter(&combined);
    painter.drawImage(0, 0, m_image);
    painter.drawImage(0, m_image.height(), frame.copy(0, bestOverlap,
                                                      frame.width(), copyHeight));
    painter.end();

    m_image = std::move(combined);
    m_lastFrame = frame;
    m_lastOverlap = bestOverlap;
    ++m_frameCount;
    return true;
}

} // namespace snipaste::core
