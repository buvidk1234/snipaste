#pragma once

#include <QImage>
#include <QList>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace snipaste::core {

// Coordinates within the desktop shown during this capture, before DPI scaling.
// This belongs to a screenshot, independently of the deduplicated recent regions.
struct CaptureGeometry
{
    QRect region;
    QSize desktopSize;

    bool isValid() const
    {
        return !region.isEmpty() && !desktopSize.isEmpty()
            && QRect(QPoint(), desktopSize).contains(region);
    }
};

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
    void add(const QImage &image, const QImage &background = {},
             CaptureGeometry geometry = {});
    void clear();
    void loadFromDirectory(const QString &directory);
    // Write new entries only. Backgrounds use lossless content-addressed
    // blocks shared by historical desktops; unchanged blocks are not written.
    bool saveToDirectory(const QString &directory) const;
    // Attach stable storage paths after a successful asynchronous save.
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
    QSize imageSizeAt(int index) const;
    QImage thumbnailAt(int index) const;
    int size() const;
    // Entries loaded from disk may be null placeholders; call imageAt() to
    // obtain one historical image without materializing the whole list.
    const QList<QImage> &images() const;
    const QList<QImage> &backgrounds() const;
    const QList<CaptureGeometry> &geometries() const { return m_geometries; }

private:
    void trimToLimits();

    int m_maximumCount;
    qint64 m_memoryLimitBytes;
    QList<QImage> m_images;
    // Stable per-entry names prevent a prepend from renaming every older PNG.
    // The entry order is stored separately in a small index file.
    QStringList m_storageKeys;
    QStringList m_imagePaths;
    QList<QImage> m_backgrounds;
    QStringList m_backgroundPaths;
    QList<CaptureGeometry> m_geometries;
    quint64 m_revision = 0;
};

} // namespace snipaste::core
