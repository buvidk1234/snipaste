#include "core/scroll_frame_matcher.h"

#include <QHashFunctions>
#include <QVector>
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace snipaste::core {
namespace {
constexpr int columns = 64;
constexpr int maximumError = 12 * 1024;

QVector<size_t> rowHashes(int width, int height, const std::function<const QRgb *(int)> &read)
{
    QVector<size_t> result(height);
    for (int y = 0; y < height; ++y)
        result[y] = qHashBits(read(y), size_t(width) * sizeof(QRgb), 0);
    return result;
}

bool hasDistinctRows(const QVector<size_t> &rows, int count)
{
    // A blank strip alone cannot establish where the page moved. Require
    // at least three distinct rows even when all pixels agree exactly.
    const auto first = rows.first();
    auto second = first;
    for (int y = 1; y < count; ++y) {
        if (rows[y] != first) {
            if (second != first && rows[y] != second)
                return true;
            second = rows[y];
        }
    }
    return false;
}

int distance(QRgb a, QRgb b)
{
    return qAbs(qRed(a) - qRed(b)) + qAbs(qGreen(a) - qGreen(b)) + qAbs(qBlue(a) - qBlue(b));
}

struct Sample {
    QRgb pixel;
    bool edge;
};
using Row = std::array<Sample, columns>;

QVector<Row> sampleRows(int width, int height, const std::function<const QRgb *(int)> &read)
{
    QVector<Row> result(height);
    for (int y = 0; y < height; ++y) {
        const auto *pixels = read(y);
        const auto *adjacent = read(y + 1 < height ? y + 1 : qMax(0, y - 1));
        for (int i = 0; i < columns; ++i) {
            // Pixel centers avoid sampling only periodic column boundaries.
            const int x = qMin(width - 1, (2 * i + 1) * width / (2 * columns));
            const auto pixel = pixels[x];
            const int neighbor = x + 1 < width ? x + 1 : qMax(0, x - 1);
            result[y][i] = {pixel, distance(pixel, pixels[neighbor]) + distance(pixel, adjacent[x]) > 16};
        }
    }
    return result;
}

struct Quality {
    int error;
    int features;
    int badFeatures;
};

Quality compare(const QVector<Row> &base, const QVector<Row> &frame, int overlap, bool exhaustive)
{
    qint64 total = 0, featureTotal = 0;
    int count = 0, features = 0, bad = 0;
    const int rows = exhaustive ? overlap : qMin(overlap, 16);
    for (int i = 0; i < rows; ++i) {
        const int y = rows == 1 ? 0 : i * (overlap - 1) / (rows - 1);
        const auto &left = base.at(base.size() - overlap + y);
        const auto &right = frame.at(y);
        for (int x = 0; x < columns; x += exhaustive ? 1 : 2) {
            const int error = distance(left[x].pixel, right[x].pixel);
            total += error;
            ++count;
            if (left[x].edge || right[x].edge) {
                featureTotal += error;
                ++features;
                bad += error > 48;
            }
        }
    }
    const int mean = int(total * 1024 / qMax(1, count));
    const int featureMean = int(featureTotal * 1024 / qMax(1, features));
    return {qMax(mean, featureMean), features, bad};
}
} // namespace

ScrollFrameMatch matchScrollFrame(const QImage &frame, int accumulatedHeight,
                                 const std::function<const QRgb *(int)> &readRow)
{
    const int tailHeight = qMin(accumulatedHeight, frame.height());
    const int start = accumulatedHeight - tailHeight;
    const auto readBase = [&](int y) { return readRow(start + y); };
    const auto readNext = [&](int y) {
        return reinterpret_cast<const QRgb *>(frame.constScanLine(y));
    };
    const auto baseHashes = rowHashes(frame.width(), tailHeight, readBase);
    const auto nextHashes = rowHashes(frame.width(), frame.height(), readNext);
    const auto exact = [&](int overlap) {
        if (!std::equal(baseHashes.cend() - overlap, baseHashes.cend(), nextHashes.cbegin()))
            return false;
        // Hashes are only a filter. Verify every original pixel before
        // accepting an overlap or declaring that the viewport is unchanged.
        for (int y = 0; y < overlap; ++y) {
            if (std::memcmp(readBase(tailHeight - overlap + y), readNext(y),
                            size_t(frame.width()) * sizeof(QRgb)) != 0)
                return false;
        }
        return true;
    };
    if (tailHeight == frame.height() && exact(tailHeight))
        return {ScrollFrameMatch::Status::Unchanged, tailHeight};

    const int minimum = qMax(3, qMin(64, frame.height() / 8));
    int exactOverlap = 0;
    for (int overlap = qMin(tailHeight, frame.height() - 1); overlap >= minimum; --overlap) {
        // Reject blank prefixes before reading their full pixel buffers.
        if (!hasDistinctRows(nextHashes, overlap) || !exact(overlap))
            continue;
        if (exactOverlap)
            return {ScrollFrameMatch::Status::Ambiguous, 0};
        exactOverlap = overlap;
    }
    if (exactOverlap)
        return {ScrollFrameMatch::Status::Matched, exactOverlap};

    // Exact matches include narrow marks between sample columns and smooth
    // gradients with no strong edges. Use tolerant sampling only when small
    // pixel changes prevent a full-width exact match.
    const auto base = sampleRows(frame.width(), tailHeight, readBase);
    const auto next = sampleRows(frame.width(), frame.height(), readNext);
    if (tailHeight == frame.height()) {
        const auto same = compare(base, next, tailHeight, true);
        int unchangedRows = 0;
        for (int y = 0; y < tailHeight; ++y)
            unchangedRows += baseHashes[y] == nextHashes[y];
        if (unchangedRows * 20 >= tailHeight * 19 && same.features >= 8
            && same.error <= 2 * 1024 && same.badFeatures * 20 <= same.features)
            return {ScrollFrameMatch::Status::Unchanged, tailHeight};
    }

    struct Candidate { int overlap; int error; };
    QVector<Candidate> candidates;
    // Check every native row: a strided search misses even-pixel scrolls on
    // high-frequency content, regardless of how well the best row is refined.
    for (int overlap = qMin(tailHeight, frame.height() - 1); overlap >= minimum; --overlap) {
        const auto quality = compare(base, next, overlap, false);
        if (quality.features >= 8 && quality.error <= maximumError
            && quality.badFeatures * 4 <= quality.features)
            candidates.append({overlap, quality.error});
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) {
        return a.error < b.error;
    });
    int best = 0;
    int bestError = std::numeric_limits<int>::max();
    int secondError = std::numeric_limits<int>::max();
    // Bound validation work on repetitive pages. If the candidates cannot be
    // disambiguated, reject the frame instead of inventing missing content.
    const qsizetype count = qMin<qsizetype>(32, candidates.size());
    for (qsizetype i = 0; i < count; ++i) {
        const auto &candidate = candidates.at(i);
        const auto quality = compare(base, next, candidate.overlap, true);
        if (quality.features < 8 || quality.error > maximumError
            || quality.badFeatures * 10 > quality.features)
            continue;
        if (quality.error < bestError) {
            secondError = bestError;
            bestError = quality.error;
            best = candidate.overlap;
        } else {
            secondError = qMin(secondError, quality.error);
        }
    }
    if (!best)
        return {candidates.isEmpty() ? ScrollFrameMatch::Status::NoMatch
                                     : ScrollFrameMatch::Status::Ambiguous, 0};
    if (secondError <= bestError + 2 * 1024)
        return {ScrollFrameMatch::Status::Ambiguous, 0};
    return {ScrollFrameMatch::Status::Matched, best};
}
} // namespace snipaste::core
