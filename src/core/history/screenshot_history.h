#pragma once

#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace snipaste::core {

class ScreenshotHistory final
{
public:
    // Keep history bounded by both item count and resident pixel memory.  A
    // full 4K frame is tens of megabytes in QImage's uncompressed storage;
    // count alone therefore permits an accidental multi-hundred-megabyte
    // resident set.
    explicit ScreenshotHistory(int maximumCount = 20,
                               qint64 memoryLimitBytes = 96LL * 1024 * 1024);

    void setMaximumCount(int maximumCount);
    void setMemoryLimitBytes(qint64 bytes);
    qint64 memoryUsageBytes() const;
    void add(const QImage &image, const QImage &background = {});
    void clear();
    void loadFromDirectory(const QString &directory);
    void saveToDirectory(const QString &directory) const;
    // After a save, lazy image/background paths follow the history indices
    // that were just written.  This keeps subsequent saves and navigation
    // from reading a file that belongs to a different item after a prepend.
    void rebaseBackgroundPaths(const QString &directory);
    // Full-screen backgrounds are only needed while navigating history.  Once
    // the asynchronous save has completed, keep their file paths and release
    // decoded pixels so an idle process does not retain one desktop frame per
    // history item.
    void releaseDecodedBackgrounds();
    // Selection images can also be full-screen captures.  Drop their decoded
    // buffers after a successful save while retaining the file paths for
    // history navigation and the history dialog.
    void releaseDecodedImages();
    quint64 revision() const { return m_revision; }
    // Backgrounds are loaded lazily because a full-screen background can
    // occupy several megabytes when decoded.  History navigation asks for a
    // single frame through this accessor instead of keeping every background
    // resident after startup.
    QImage backgroundAt(int index) const;
    // Historical selection images use the same on-demand policy.  New images
    // remain resident until their first asynchronous save completes.
    QImage imageAt(int index) const;
    int size() const;
    // Entries loaded from disk may be null placeholders; call imageAt() to
    // obtain one historical image without materializing the whole list.
    const QList<QImage> &images() const;
    const QList<QImage> &backgrounds() const;

private:
    void trimToLimits();

    int m_maximumCount;
    qint64 m_memoryLimitBytes;
    QList<QImage> m_images;
    QStringList m_imagePaths;
    QList<QImage> m_backgrounds;
    QStringList m_backgroundPaths;
    quint64 m_revision = 0;
};

} // namespace snipaste::core
