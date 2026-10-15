#include "core/scroll_stitcher.h"
#include "core/scroll_frame_matcher.h"

#include <QPainter>
#include <QtGlobal>
#include <algorithm>
#include <cstring>

namespace snipaste::core {

namespace {

QImage normalized(const QImage &image)
{
    QImage result = image.convertToFormat(QImage::Format_RGB32);
    result.setDevicePixelRatio(1.0);
    return result;
}

} // namespace

VerticalScrollStitcher::VerticalScrollStitcher(int maximumHeight)
    : m_maximumHeight(qMax(1, maximumHeight))
{
}

void VerticalScrollStitcher::clear()
{
    m_strips = {};
    m_size = {};
    m_viewportSize = {};
    m_image = {};
    m_frameCount = 0;
    m_lastOverlap = 0;
    m_lastResult = AppendResult::InvalidFrame;
}

const QImage &VerticalScrollStitcher::image() const
{
    if (!m_image.isNull() || m_strips.isEmpty())
        return m_image;
    if (m_strips.size() == 1) {
        m_image = m_strips.first().pixels;
        return m_image;
    }
    QImage combined(m_size, QImage::Format_RGB32);
    if (combined.isNull())
        return m_image;
    for (const auto &strip : m_strips) {
        const qsizetype rowBytes = qsizetype(m_size.width()) * sizeof(QRgb);
        for (int y = 0; y < strip.pixels.height(); ++y)
            std::memcpy(combined.scanLine(strip.top + y), strip.pixels.constScanLine(y), rowBytes);
    }
    // Compact the stored representation too, so image() does not leave
    // both a full export cache and all original pixel strips resident.
    m_strips = {{0, combined}};
    m_image = std::move(combined);
    return m_image;
}

QImage VerticalScrollStitcher::takeImage()
{
    QImage result = image();
    clear();
    return result;
}

void VerticalScrollStitcher::draw(QPainter &painter, const QRectF &exposed) const
{
    for (const auto &strip : m_strips) {
        if (strip.top >= exposed.bottom())
            break;
        if (strip.top + strip.pixels.height() > exposed.top())
            painter.drawImage(QPoint(0, strip.top), strip.pixels);
    }
}

const uchar *VerticalScrollStitcher::scanLine(int y) const
{
    const auto next = std::upper_bound(m_strips.cbegin(), m_strips.cend(), y,
                                      [](int row, const Strip &strip) { return row < strip.top; });
    const auto &strip = *(next - 1);
    return strip.pixels.constScanLine(y - strip.top);
}

bool VerticalScrollStitcher::append(const QImage &source)
{
    m_lastResult = AppendResult::InvalidFrame;
    if (source.isNull())
        return false;
    QImage frame = normalized(source);
    if (frame.isNull())
        return false;
    if (m_strips.isEmpty()) {
        m_viewportSize = frame.size();
        if (frame.height() > m_maximumHeight)
            frame = frame.copy(0, 0, frame.width(), m_maximumHeight);
        if (frame.isNull())
            return false;
        m_strips.append({0, frame});
        m_size = frame.size();
        m_frameCount = 1;
        m_lastOverlap = 0;
        m_lastResult = AppendResult::Added;
        return true;
    }
    if (frame.size() != m_viewportSize) {
        m_lastResult = AppendResult::SizeChanged;
        return false;
    }
    if (m_size.height() >= m_maximumHeight) {
        m_lastResult = AppendResult::HeightLimit;
        return false;
    }
    const auto match = matchScrollFrame(frame, m_size.height(), [this](int y) {
        return reinterpret_cast<const QRgb *>(scanLine(y));
    });
    using Status = ScrollFrameMatch::Status;
    switch (match.status) {
    case Status::Unchanged:
        m_lastResult = AppendResult::Unchanged;
        return false;
    case Status::NoMatch:
        m_lastResult = AppendResult::NoMatch;
        return false;
    case Status::Ambiguous:
        m_lastResult = AppendResult::Ambiguous;
        return false;
    case Status::Matched:
        break;
    }
    const int copyHeight = qMin(m_maximumHeight - m_size.height(), frame.height() - match.overlap);
    QImage strip = frame.copy(0, match.overlap, frame.width(), copyHeight);
    if (strip.isNull())
        return false;
    m_strips.append({m_size.height(), std::move(strip)});
    m_size.rheight() += copyHeight;
    m_image = {};
    m_lastOverlap = match.overlap;
    m_lastResult = AppendResult::Added;
    ++m_frameCount;
    return true;
}

} // namespace snipaste::core
