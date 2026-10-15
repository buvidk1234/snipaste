#include "core/recognition/ocr_image.h"
#include <array>
#include <algorithm>

namespace snipaste::core::detail {
namespace {
constexpr int tileSize = 64;

struct Contrast {
    int low = 255;
    int high = 0;
    int background = 255;
    int backgroundCount = 0;
};

Contrast contrastIn(const QImage &image, QRect tile)
{
    std::array<int, 256> histogram{};
    Contrast result;
    for (int y = tile.top(); y <= tile.bottom(); ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = tile.left(); x <= tile.right(); ++x) {
            const int value = qGray(row[x]);
            ++histogram[value];
            result.low = qMin(result.low, value);
            result.high = qMax(result.high, value);
        }
    }
    for (int value = 0; value < 256; ++value) {
        if (histogram[value] > result.backgroundCount) {
            result.background = value;
            result.backgroundCount = histogram[value];
        }
    }
    return result;
}
} // namespace

QImage enhanceOcrContrast(const QImage &source)
{
    if (source.isNull())
        return {};
    const QImage image = source.convertToFormat(QImage::Format_RGB32);
    const int columns = (image.width() + tileSize - 1) / tileSize;
    const int rows = (image.height() + tileSize - 1) / tileSize;
    QVector<Contrast> tiles;
    tiles.reserve(columns * rows);
    bool needsEnhancement = false;
    for (int y = 0; y < image.height(); y += tileSize) {
        for (int x = 0; x < image.width(); x += tileSize) {
            const QRect tile = QRect(x, y, tileSize, tileSize).intersected(image.rect());
            const Contrast contrast = contrastIn(image, tile);
            tiles.append(contrast);
            const int range = contrast.high - contrast.low;
            // A dominant flat background distinguishes printed/UI text from
            // ordinary photographic texture. Do not amplify near-flat noise.
            if (contrast.backgroundCount * 2 > tile.width() * tile.height()
                && range >= 16 && (contrast.background < 128 || range < 160))
                needsEnhancement = true;
        }
    }
    if (!needsEnhancement)
        return image;

    QImage result(image.size(), QImage::Format_RGB32);
    result.setDevicePixelRatio(source.devicePixelRatio());
    for (int y = 0; y < image.height(); ++y) {
        const auto *input = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        auto *output = reinterpret_cast<QRgb *>(result.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const Contrast &contrast = tiles[(y / tileSize) * columns + x / tileSize];
            const int range = contrast.high - contrast.low;
            int value = 255;
            if (range >= 16) {
                value = 255 * (qGray(input[x]) - contrast.low) / range;
                if (contrast.background < 128)
                    value = 255 - value;
            }
            // Preserve antialiasing rather than binarizing thin font strokes.
            output[x] = qRgb(value, value, value);
        }
    }
    return result;
}

OcrResult mergeLowContrastText(const QImage &source, const OcrResult &original,
                              const OcrResult &enhanced)
{
    if (!enhanced.success)
        return original;
    struct Line {
        QString text;
        QVector<OcrTextBox> boxes;
        QRectF bounds;
        int order = 0;
    };
    const auto linesOf = [](const OcrResult &result) {
        QVector<Line> lines;
        QVector<qsizetype> starts;
        qsizetype start = 0;
        for (const auto &text : result.text.split(QChar('\n'))) {
            Line line;
            line.text = text;
            line.order = int(lines.size());
            lines.append(std::move(line));
            starts.append(start);
            start += text.size() + 1;
        }
        for (auto box : result.boxes) {
            if (box.line < 0 || box.line >= lines.size())
                continue;
            auto &line = lines[box.line];
            const qsizetype lineStart = starts[box.line];
            if (box.start < lineStart || box.start + box.length > lineStart + line.text.size())
                continue;
            box.start -= lineStart;
            line.boxes.append(box);
            line.bounds = line.bounds.united(box.bounds);
        }
        return lines;
    };
    auto originalLines = linesOf(original);
    auto enhancedLines = linesOf(enhanced);
    QVector<Line> replacements;
    for (auto &line : enhancedLines) {
        const QRect area = line.bounds.adjusted(-2, -2, 2, 2).toAlignedRect().intersected(source.rect());
        if (area.isEmpty())
            continue;
        const auto contrast = contrastIn(source, area);
        const int range = contrast.high - contrast.low;
        if (range < 16 || range >= 160)
            continue;
        line.order = int(originalLines.size());
        for (int index = 0; index < originalLines.size(); ++index) {
            const auto &bounds = originalLines[index].bounds;
            if (bounds.intersects(line.bounds)) {
                line.order = index;
                break;
            }
            if (bounds.top() >= line.bounds.top() && bounds.left() >= line.bounds.left()) {
                line.order = index;
                break;
            }
        }
        replacements.append(std::move(line));
    }
    if (replacements.isEmpty())
        return original;

    QVector<Line> merged;
    for (auto &line : originalLines) {
        Line remaining;
        remaining.order = line.order;
        bool changed = false;
        qsizetype previousEnd = 0;
        for (auto box : line.boxes) {
            const bool replaced = std::any_of(replacements.cbegin(), replacements.cend(), [&](const Line &replacement) {
                const QRectF overlap = replacement.bounds.intersected(box.bounds);
                return overlap.width() * overlap.height() > box.bounds.width() * box.bounds.height() * 0.5;
            });
            const qsizetype end = box.start + box.length;
            if (!replaced) {
                if (!remaining.text.isEmpty())
                    remaining.text += line.text.mid(previousEnd, box.start - previousEnd);
                const QString text = line.text.mid(box.start, box.length);
                box.start = remaining.text.size();
                remaining.text += text;
                remaining.boxes.append(box);
                remaining.bounds = remaining.bounds.united(box.bounds);
            } else {
                changed = true;
            }
            previousEnd = end;
        }
        if (!changed)
            merged.append(std::move(line));
        else if (!remaining.text.isEmpty())
            merged.append(std::move(remaining));
    }
    merged += replacements;
    std::stable_sort(merged.begin(), merged.end(), [](const Line &a, const Line &b) {
        if (a.order != b.order)
            return a.order < b.order;
        if (a.bounds.top() != b.bounds.top())
            return a.bounds.top() < b.bounds.top();
        return a.bounds.left() < b.bounds.left();
    });
    OcrResult result;
    int index = 0;
    for (const auto &line : merged) {
        if (line.text.isEmpty())
            continue;
        if (!result.text.isEmpty())
            result.text += QChar('\n');
        const qsizetype start = result.text.size();
        result.text += line.text;
        for (auto box : line.boxes) {
            box.start += start;
            box.line = index;
            result.boxes.append(box);
        }
        ++index;
    }
    result.success = !result.text.isEmpty();
    return result;
}

} // namespace snipaste::core::detail
