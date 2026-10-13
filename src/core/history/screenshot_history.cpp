#include "core/history/screenshot_history.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QUuid>

namespace snipaste::core {

ScreenshotHistory::ScreenshotHistory(int maximumCount, qint64 memoryLimitBytes)
    : m_maximumCount(qMax(1, maximumCount))
    , m_memoryLimitBytes(qMax<qint64>(1, memoryLimitBytes))
{
}

void ScreenshotHistory::setMaximumCount(int maximumCount)
{
    m_maximumCount = qMax(1, maximumCount);
    while (m_images.size() > m_maximumCount) {
        m_images.removeLast();
        if (!m_imagePaths.isEmpty())
            m_imagePaths.removeLast();
        if (!m_backgrounds.isEmpty())
            m_backgrounds.removeLast();
        if (!m_backgroundPaths.isEmpty())
            m_backgroundPaths.removeLast();
    }
    trimToLimits();
}

void ScreenshotHistory::setMemoryLimitBytes(qint64 bytes)
{
    m_memoryLimitBytes = qMax<qint64>(1, bytes);
    trimToLimits();
}

void ScreenshotHistory::add(const QImage &image, const QImage &background)
{
    if (image.isNull())
        return;
    ++m_revision;
    m_images.prepend(image);
    m_imagePaths.prepend(QString());
    m_backgrounds.prepend(background);
    m_backgroundPaths.prepend(QString());
    while (m_images.size() > m_maximumCount) {
        m_images.removeLast();
        m_imagePaths.removeLast();
        m_backgrounds.removeLast();
        m_backgroundPaths.removeLast();
    }
    trimToLimits();
}

void ScreenshotHistory::clear()
{
    ++m_revision;
    m_images.clear();
    m_imagePaths.clear();
    m_backgrounds.clear();
    m_backgroundPaths.clear();
}

void ScreenshotHistory::loadFromDirectory(const QString &directory)
{
    ++m_revision;
    m_images.clear();
    m_imagePaths.clear();
    m_backgrounds.clear();
    m_backgroundPaths.clear();
    const QDir dir(directory);
    const QStringList files = dir.entryList({QStringLiteral("*.png")}, QDir::Files, QDir::Name);
    for (const QString &file : files) {
        if (file.endsWith(QStringLiteral(".background.png")))
            continue;
        const QString imagePath = dir.filePath(file);
        QImageReader reader(imagePath);
        if (reader.canRead()) {
            // Keep the compressed selection on disk until history navigation
            // or the history dialog asks for it.  This avoids decoding all
            // historical captures when merely opening a new snip session.
            m_images.append(QImage());
            m_imagePaths.append(imagePath);
            const QString backgroundPath = dir.filePath(
                file.left(file.size() - QStringLiteral(".png").size())
                + QStringLiteral(".background.png"));
            // Do not decode the full-screen background during startup.  A
            // 1920x1200 RGBA frame is about 9 MB in memory, and loading 20 of
            // them just to release them during trimming leaves a large heap
            // high-water mark on Windows.  Keep only the path until history
            // navigation or persistence actually needs the frame.
            m_backgrounds.append(QImage());
            m_backgroundPaths.append(QFileInfo::exists(backgroundPath)
                                         ? backgroundPath
                                         : QString());
        }
        if (m_images.size() >= m_maximumCount)
            break;
    }
    trimToLimits();
}

void ScreenshotHistory::saveToDirectory(const QString &directory) const
{
    QDir dir;
    if (!dir.mkpath(directory))
        return;
    QDir historyDir(directory);
    // Keep lazy backgrounds compressed while the old files are replaced.
    // Stage them as files instead of reading every PNG into a QByteArray: on
    // a large history this keeps the save worker's peak memory bounded too.
    const QString cacheToken = QUuid::createUuid().toString(QUuid::Id128);
    QList<QString> lazyImagePaths;
    lazyImagePaths.resize(m_images.size());
    QList<QString> lazyBackgroundPaths;
    lazyBackgroundPaths.resize(m_images.size());
    for (int index = 0; index < m_images.size(); ++index) {
        if (m_images.at(index).isNull()
            && index < m_imagePaths.size()
            && !m_imagePaths.at(index).isEmpty()) {
            const QString stagedPath = historyDir.filePath(
                QStringLiteral(".snipaste-image-%1-%2")
                    .arg(cacheToken)
                    .arg(index, 3, 10, QChar('0')));
            QFile::remove(stagedPath);
            if (QFile::copy(m_imagePaths.at(index), stagedPath))
                lazyImagePaths[index] = stagedPath;
        }
        if (index >= m_backgrounds.size() || !m_backgrounds.at(index).isNull()
            || index >= m_backgroundPaths.size()
            || m_backgroundPaths.at(index).isEmpty())
            continue;
        const QString stagedPath = historyDir.filePath(
            QStringLiteral(".snipaste-background-%1-%2")
                .arg(cacheToken)
                .arg(index, 3, 10, QChar('0')));
        QFile::remove(stagedPath);
        if (QFile::copy(m_backgroundPaths.at(index), stagedPath))
            lazyBackgroundPaths[index] = stagedPath;
    }
    const QStringList oldFiles = historyDir.entryList({QStringLiteral("*.png")}, QDir::Files);
    for (const QString &file : oldFiles)
        historyDir.remove(file);
    for (int index = 0; index < m_images.size(); ++index) {
        const QString path = historyDir.filePath(
            QStringLiteral("%1.png").arg(index, 3, 10, QChar('0')));
        if (!m_images.at(index).isNull()) {
            m_images.at(index).save(path, "PNG");
        } else if (index < lazyImagePaths.size()
                   && !lazyImagePaths.at(index).isEmpty()) {
            QFile::remove(path);
            QFile::copy(lazyImagePaths.at(index), path);
        }
        if (index < m_backgrounds.size() && !m_backgrounds.at(index).isNull()) {
            const QString backgroundPath = historyDir.filePath(
                QStringLiteral("%1.background.png").arg(index, 3, 10, QChar('0')));
            m_backgrounds.at(index).save(backgroundPath, "PNG");
        } else if (index < lazyBackgroundPaths.size()
                   && !lazyBackgroundPaths.at(index).isEmpty()) {
            const QString backgroundPath = historyDir.filePath(
                QStringLiteral("%1.background.png").arg(index, 3, 10, QChar('0')));
            QFile::remove(backgroundPath);
            QFile::copy(lazyBackgroundPaths.at(index), backgroundPath);
        }
    }
    for (const QString &stagedPath : lazyImagePaths)
        if (!stagedPath.isEmpty())
            QFile::remove(stagedPath);
    for (const QString &stagedPath : lazyBackgroundPaths)
        if (!stagedPath.isEmpty())
            QFile::remove(stagedPath);
}

void ScreenshotHistory::rebaseBackgroundPaths(const QString &directory)
{
    const QDir historyDir(directory);
    for (int index = 0; index < m_imagePaths.size(); ++index) {
        const QString path = historyDir.filePath(
            QStringLiteral("%1.png").arg(index, 3, 10, QChar('0')));
        m_imagePaths[index] = QFileInfo::exists(path) ? path : QString();
    }
    for (int index = 0; index < m_backgroundPaths.size(); ++index) {
        const QString path = historyDir.filePath(
            QStringLiteral("%1.background.png").arg(index, 3, 10, QChar('0')));
        // Newly-added entries start with an empty path.  They need to be
        // rebased too; otherwise releasing the in-memory frame would make
        // that history item impossible to restore until the next restart.
        m_backgroundPaths[index] = QFileInfo::exists(path) ? path : QString();
    }
}

void ScreenshotHistory::releaseDecodedBackgrounds()
{
    for (qsizetype index = 0; index < m_backgrounds.size(); ++index) {
        if (index < m_backgroundPaths.size() && !m_backgroundPaths.at(index).isEmpty())
            m_backgrounds[index] = {};
    }
}

void ScreenshotHistory::releaseDecodedImages()
{
    for (qsizetype index = 0; index < m_images.size(); ++index) {
        if (index < m_imagePaths.size() && !m_imagePaths.at(index).isEmpty())
            m_images[index] = {};
    }
}

QImage ScreenshotHistory::backgroundAt(int index) const
{
    if (index < 0 || index >= m_images.size())
        return {};
    if (index < m_backgrounds.size() && !m_backgrounds.at(index).isNull())
        return m_backgrounds.at(index);
    if (index >= m_backgroundPaths.size() || m_backgroundPaths.at(index).isEmpty())
        return {};
    QImage background;
    if (!background.load(m_backgroundPaths.at(index)))
        return {};
    return background;
}

int ScreenshotHistory::size() const
{
    return m_images.size();
}

const QList<QImage> &ScreenshotHistory::images() const
{
    return m_images;
}

QImage ScreenshotHistory::imageAt(int index) const
{
    if (index < 0 || index >= m_images.size())
        return {};
    if (!m_images.at(index).isNull())
        return m_images.at(index);
    if (index >= m_imagePaths.size() || m_imagePaths.at(index).isEmpty())
        return {};
    QImage image;
    if (!image.load(m_imagePaths.at(index)))
        return {};
    return image;
}

const QList<QImage> &ScreenshotHistory::backgrounds() const
{
    return m_backgrounds;
}

qint64 ScreenshotHistory::memoryUsageBytes() const
{
    qint64 bytes = 0;
    for (const QImage &image : m_images)
        bytes += image.sizeInBytes();
    for (const QImage &image : m_backgrounds)
        bytes += image.sizeInBytes();
    return bytes;
}

void ScreenshotHistory::trimToLimits()
{
    while (m_images.size() > m_maximumCount) {
        m_images.removeLast();
        if (!m_imagePaths.isEmpty())
            m_imagePaths.removeLast();
        if (!m_backgrounds.isEmpty())
            m_backgrounds.removeLast();
        if (!m_backgroundPaths.isEmpty())
            m_backgroundPaths.removeLast();
    }

    // A background is only needed to restore the desktop around a historical
    // selection.  Prefer releasing those optional frames before evicting the
    // actual screenshots; this keeps the history useful on 4K/multi-monitor
    // setups where one background can be larger than the whole budget.
    qint64 usage = memoryUsageBytes();
    while (usage > m_memoryLimitBytes) {
        bool releasedBackground = false;
        for (qsizetype index = m_backgrounds.size() - 1;
             index >= 0 && usage > m_memoryLimitBytes; --index) {
            // An in-memory background with no file path belongs to a capture
            // that has not been persisted yet.  Keep it until the async save
            // can write it; otherwise a memory trim would silently make that
            // history entry unable to restore its desktop context.
            if (!m_backgrounds.at(index).isNull()
                && index < m_backgroundPaths.size()
                && !m_backgroundPaths.at(index).isEmpty()) {
                usage -= m_backgrounds.at(index).sizeInBytes();
                m_backgrounds[index] = QImage();
                releasedBackground = true;
            }
        }
        if (releasedBackground)
            continue;

        // Preserve the newest screenshot even if it alone is larger than the
        // limit; otherwise a single full-screen capture could never be
        // recalled.
        if (m_images.size() <= 1)
            break;
        usage -= m_images.constLast().sizeInBytes();
        m_images.removeLast();
        if (!m_imagePaths.isEmpty())
            m_imagePaths.removeLast();
        if (!m_backgrounds.isEmpty()) {
            usage -= m_backgrounds.constLast().sizeInBytes();
            m_backgrounds.removeLast();
        }
        if (!m_backgroundPaths.isEmpty())
            m_backgroundPaths.removeLast();
    }
}

} // namespace snipaste::core
