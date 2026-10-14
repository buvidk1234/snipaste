#include "core/history/screenshot_history.h"

#include <QColorSpace>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

#include <cstring>

namespace {

constexpr int kBackgroundTileSize = 256;
const QByteArray kBackgroundManifestMagic("SNIPASTE_BG2");

struct BackgroundManifest
{
    QSize size;
    int tileSize = kBackgroundTileSize;
    double devicePixelRatio = 1.0;
    QByteArray colorProfile;
    QStringList tileNames;
};

QString backgroundPathForKey(const QDir &directory, const QString &key)
{
    const QString path = directory.filePath(key + QStringLiteral(".background.tiles"));
    if (QFileInfo::exists(path))
        return path;
    const QString legacyPath = directory.filePath(key + QStringLiteral(".background.png"));
    return QFileInfo::exists(legacyPath) ? legacyPath : QString();
}

snipaste::core::CaptureGeometry loadCaptureGeometry(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4096)
        return {};
    const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonArray region = object.value(QStringLiteral("region")).toArray();
    const QJsonArray desktopSize = object.value(QStringLiteral("desktopSize")).toArray();
    if (object.value(QStringLiteral("version")).toInt() != 1
        || region.size() != 4 || desktopSize.size() != 2)
        return {};
    const snipaste::core::CaptureGeometry geometry{
        QRect(region.at(0).toInt(), region.at(1).toInt(),
              region.at(2).toInt(), region.at(3).toInt()),
        QSize(desktopSize.at(0).toInt(), desktopSize.at(1).toInt())};
    return geometry.isValid() ? geometry : snipaste::core::CaptureGeometry{};
}

bool saveCaptureGeometry(const QString &path, const snipaste::core::CaptureGeometry &geometry)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    const QJsonObject object{
        {QStringLiteral("version"), 1},
        {QStringLiteral("region"), QJsonArray{geometry.region.x(), geometry.region.y(),
                                             geometry.region.width(), geometry.region.height()}},
        {QStringLiteral("desktopSize"), QJsonArray{geometry.desktopSize.width(),
                                                  geometry.desktopSize.height()}}};
    const QByteArray data = QJsonDocument(object).toJson(QJsonDocument::Compact);
    return file.write(data) == data.size() && file.commit();
}

QString backgroundTileDirectory(const QString &historyDirectory)
{
    return QDir(historyDirectory).filePath(QStringLiteral("background-tiles"));
}

QString tileFileName(const QImage &tile)
{
    QByteArray dimensions;
    QDataStream stream(&dimensions, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << qint32(tile.width()) << qint32(tile.height());
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(dimensions);
    for (int y = 0; y < tile.height(); ++y)
        hash.addData(QByteArrayView(reinterpret_cast<const char *>(tile.constScanLine(y)),
                                   tile.width() * 4));
    return QString::fromLatin1(hash.result().toHex()) + QStringLiteral(".png");
}

bool saveBackgroundTiles(const QString &historyDirectory,
                         const QString &key,
                         const QImage &background)
{
    if (background.isNull())
        return false;

    QDir historyDir(historyDirectory);
    const QString tileDirectory = backgroundTileDirectory(historyDirectory);
    if (!QDir().mkpath(tileDirectory))
        return false;

    const int columns = (background.width() - 1) / kBackgroundTileSize + 1;
    const int rows = (background.height() - 1) / kBackgroundTileSize + 1;
    QStringList tileNames;
    tileNames.reserve(columns * rows);
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const QRect rect(column * kBackgroundTileSize,
                             row * kBackgroundTileSize,
                             qMin(kBackgroundTileSize, background.width() - column * kBackgroundTileSize),
                             qMin(kBackgroundTileSize, background.height() - row * kBackgroundTileSize));
            // Convert one block at a time; the worker must not allocate a
            // second full desktop buffer just to hash or encode it.
            QImage tile = background.copy(rect).convertToFormat(QImage::Format_RGBA8888);
            tile.setDevicePixelRatio(1.0);
            tile.setColorSpace({}); // The desktop profile is stored once in its manifest.
            const QString fileName = tileFileName(tile);
            const QString tilePath = QDir(tileDirectory).filePath(fileName);
            if (!QFileInfo::exists(tilePath)) {
                QSaveFile tileFile(tilePath);
                if (!tileFile.open(QIODevice::WriteOnly) || !tile.save(&tileFile, "PNG")
                    || !tileFile.commit())
                    return false;
            }
            tileNames.append(fileName);
        }
    }

    QSaveFile manifest(historyDir.filePath(key + QStringLiteral(".background.tiles")));
    if (!manifest.open(QIODevice::WriteOnly))
        return false;
    QDataStream stream(&manifest);
    stream.setVersion(QDataStream::Qt_6_5);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData(kBackgroundManifestMagic.constData(), kBackgroundManifestMagic.size());
    QByteArray tileHashes;
    tileHashes.reserve(tileNames.size() * 32);
    for (const QString &name : tileNames)
        tileHashes.append(QByteArray::fromHex(name.left(64).toLatin1()));
    stream << quint32(1) << qint32(background.width()) << qint32(background.height())
           << qint32(kBackgroundTileSize) << background.devicePixelRatio()
           << background.colorSpace().iccProfile() << tileHashes;
    if (stream.status() != QDataStream::Ok)
        return false;
    return manifest.commit();
}

bool readBackgroundManifest(const QString &manifestPath, BackgroundManifest &data)
{
    QFile manifest(manifestPath);
    if (!manifest.open(QIODevice::ReadOnly))
        return false;
    if (manifest.read(kBackgroundManifestMagic.size()) != kBackgroundManifestMagic)
        return false;

    QDataStream stream(&manifest);
    stream.setVersion(QDataStream::Qt_6_5);
    stream.setByteOrder(QDataStream::LittleEndian);
    quint32 version = 0;
    qint32 width = 0;
    qint32 height = 0;
    QByteArray tileHashes;
    stream >> version >> width >> height >> data.tileSize >> data.devicePixelRatio
           >> data.colorProfile >> tileHashes;
    if (stream.status() != QDataStream::Ok || version != 1 || width <= 0 || height <= 0
        || data.tileSize != kBackgroundTileSize)
        return false;
    const qint64 columns = (width - 1) / data.tileSize + 1;
    const qint64 rows = (height - 1) / data.tileSize + 1;
    if (tileHashes.size() != columns * rows * 32)
        return false;
    data.tileNames.reserve(columns * rows);
    for (qsizetype offset = 0; offset < tileHashes.size(); offset += 32)
        data.tileNames.append(QString::fromLatin1(tileHashes.mid(offset, 32).toHex())
                              + QStringLiteral(".png"));
    data.size = QSize(width, height);
    return true;
}

QImage loadBackgroundTiles(const QString &manifestPath)
{
    BackgroundManifest data;
    if (!readBackgroundManifest(manifestPath, data))
        return {};
    QImage result(data.size, QImage::Format_RGBA8888);
    if (result.isNull())
        return {};
    result.setDevicePixelRatio(data.devicePixelRatio > 0.0 ? data.devicePixelRatio : 1.0);
    if (!data.colorProfile.isEmpty())
        result.setColorSpace(QColorSpace::fromIccProfile(data.colorProfile));
    const int columns = (data.size.width() - 1) / data.tileSize + 1;
    const int rows = (data.size.height() - 1) / data.tileSize + 1;
    const QString tileDirectory = QDir(QFileInfo(manifestPath).absolutePath()).filePath(
        QStringLiteral("background-tiles"));
    int index = 0;
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column, ++index) {
            QImage tile;
            if (!tile.load(QDir(tileDirectory).filePath(data.tileNames.at(index))))
                return {};
            tile = tile.convertToFormat(QImage::Format_RGBA8888);
            const int left = column * data.tileSize;
            const int top = row * data.tileSize;
            const int copyWidth = qMin(data.tileSize, data.size.width() - left);
            const int copyHeight = qMin(data.tileSize, data.size.height() - top);
            if (tile.size() != QSize(copyWidth, copyHeight))
                return {};
            for (int y = 0; y < copyHeight; ++y) {
                std::memcpy(result.scanLine(top + y) + left * 4,
                            tile.constScanLine(y),
                            size_t(copyWidth) * 4);
            }
        }
    }
    return result;
}

void removeUnreferencedTiles(const QDir &historyDir, const QSet<QString> &expectedFiles)
{
    QSet<QString> referencedTiles;
    for (const QString &file : expectedFiles) {
        if (!file.endsWith(QStringLiteral(".background.tiles")))
            continue;
        BackgroundManifest data;
        // An unreadable manifest must not cause surviving backgrounds to
        // lose their blocks during cleanup.
        if (!readBackgroundManifest(historyDir.filePath(file), data))
            return;
        for (const QString &tile : data.tileNames)
            referencedTiles.insert(tile);
    }
    QDir tileDir(backgroundTileDirectory(historyDir.absolutePath()));
    for (const QString &file : tileDir.entryList({QStringLiteral("*.png")}, QDir::Files))
        if (!referencedTiles.contains(file))
            tileDir.remove(file);
}

} // namespace

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
        if (!m_storageKeys.isEmpty())
            m_storageKeys.removeLast();
        if (!m_imagePaths.isEmpty())
            m_imagePaths.removeLast();
        if (!m_backgrounds.isEmpty())
            m_backgrounds.removeLast();
        if (!m_backgroundPaths.isEmpty())
            m_backgroundPaths.removeLast();
        if (!m_geometries.isEmpty())
            m_geometries.removeLast();
    }
    trimToLimits();
}

void ScreenshotHistory::setMemoryLimitBytes(qint64 bytes)
{
    m_memoryLimitBytes = qMax<qint64>(1, bytes);
    trimToLimits();
}

void ScreenshotHistory::add(const QImage &image, const QImage &background, CaptureGeometry geometry)
{
    if (image.isNull())
        return;
    ++m_revision;
    m_images.prepend(image);
    m_storageKeys.prepend(QStringLiteral("capture-%1").arg(
        QUuid::createUuid().toString(QUuid::Id128)));
    m_imagePaths.prepend(QString());
    m_backgrounds.prepend(background);
    m_backgroundPaths.prepend(QString());
    // Direct full-screen commands pass the very same frame as image/background.
    if (!geometry.isValid() && !background.isNull() && image.cacheKey() == background.cacheKey())
        geometry = {background.rect(), background.size()};
    m_geometries.prepend(geometry.isValid() ? geometry : CaptureGeometry{});
    while (m_images.size() > m_maximumCount) {
        m_images.removeLast();
        m_storageKeys.removeLast();
        m_imagePaths.removeLast();
        m_backgrounds.removeLast();
        m_backgroundPaths.removeLast();
        m_geometries.removeLast();
    }
    trimToLimits();
}

void ScreenshotHistory::clear()
{
    ++m_revision;
    m_images.clear();
    m_storageKeys.clear();
    m_imagePaths.clear();
    m_backgrounds.clear();
    m_backgroundPaths.clear();
    m_geometries.clear();
}

void ScreenshotHistory::loadFromDirectory(const QString &directory)
{
    ++m_revision;
    m_images.clear();
    m_storageKeys.clear();
    m_imagePaths.clear();
    m_backgrounds.clear();
    m_backgroundPaths.clear();
    m_geometries.clear();
    const QDir dir(directory);
    const QString orderPath = dir.filePath(QStringLiteral(".snipaste-order"));
    QStringList keys;
    QFile orderFile(orderPath);
    if (orderFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!orderFile.atEnd()) {
            const QString key = QString::fromUtf8(orderFile.readLine()).trimmed();
            if (!key.isEmpty() && QFileInfo(key).fileName() == key)
                keys.append(key);
        }
    } else if (!QFileInfo::exists(orderPath)) {
        // Versions before the stable-key index used 000.png, 001.png, etc.
        // Only import that known layout; an empty committed index stays empty.
        const QRegularExpression legacyName(QStringLiteral("^[0-9]+\\.png$"));
        for (const QString &file : dir.entryList({QStringLiteral("*.png")}, QDir::Files, QDir::Name))
            if (legacyName.match(file).hasMatch())
                keys.append(QFileInfo(file).completeBaseName());
    }
    for (const QString &key : keys) {
        const QString imagePath = dir.filePath(key + QStringLiteral(".png"));
        QImageReader reader(imagePath);
        if (reader.canRead()) {
            // Keep the compressed selection on disk until history navigation
            // or the history dialog asks for it.  This avoids decoding all
            // historical captures when merely opening a new snip session.
            m_images.append(QImage());
            m_storageKeys.append(key);
            m_imagePaths.append(imagePath);
            const QString backgroundPath = backgroundPathForKey(dir, key);
            // Do not decode the full-screen background during startup.  A
            // 1920x1200 RGBA frame is about 9 MB in memory, and loading 20 of
            // them just to release them during trimming leaves a large heap
            // high-water mark on Windows.  Keep only the path until history
            // navigation or persistence actually needs the frame.
            m_backgrounds.append(QImage());
            m_backgroundPaths.append(backgroundPath);
            m_geometries.append(loadCaptureGeometry(dir.filePath(key + QStringLiteral(".geometry.json"))));
        }
        if (m_images.size() >= m_maximumCount)
            break;
    }
    trimToLimits();
}

bool ScreenshotHistory::saveToDirectory(const QString &directory) const
{
    QDir dir;
    if (!dir.mkpath(directory))
        return false;
    QDir historyDir(directory);
    QStringList persistedKeys;
    QSet<QString> expectedFiles;
    for (int index = 0; index < m_images.size(); ++index) {
        QString key = index < m_storageKeys.size() ? m_storageKeys.at(index) : QString();
        if (key.isEmpty())
            key = QStringLiteral("capture-%1").arg(QUuid::createUuid().toString(QUuid::Id128));
        const QString imageFileName = key + QStringLiteral(".png");
        const QString imagePath = historyDir.filePath(imageFileName);
        // The previous worker can have written this entry before the GUI
        // thread rebased its paths. Check its stable file, not just the path
        // list, to avoid writing it again during rapid consecutive captures.
        if (!QFileInfo::exists(imagePath)) {
            const QImage image = imageAt(index);
            QSaveFile imageFile(imagePath);
            if (image.isNull() || !imageFile.open(QIODevice::WriteOnly)
                || !image.save(&imageFile, "PNG") || !imageFile.commit())
                return false;
        }
        persistedKeys.append(key);
        expectedFiles.insert(imageFileName);

        QString backgroundPath = backgroundPathForKey(historyDir, key);
        if (backgroundPath.isEmpty()) {
            const QImage background = backgroundAt(index);
            if (!background.isNull()) {
                if (!saveBackgroundTiles(directory, key, background))
                    return false;
                backgroundPath = backgroundPathForKey(historyDir, key);
            } else if (index < m_backgroundPaths.size() && !m_backgroundPaths.at(index).isEmpty()) {
                // A missing/corrupt historical background must not be
                // silently committed as an entry with no desktop context.
                return false;
            }
        }
        if (!backgroundPath.isEmpty())
            expectedFiles.insert(QFileInfo(backgroundPath).fileName());

        if (index < m_geometries.size() && m_geometries.at(index).isValid()) {
            const QString geometryFileName = key + QStringLiteral(".geometry.json");
            const QString geometryPath = historyDir.filePath(geometryFileName);
            if (!QFileInfo::exists(geometryPath)
                && !saveCaptureGeometry(geometryPath, m_geometries.at(index)))
                return false;
            expectedFiles.insert(geometryFileName);
        }
    }

    // The order file is tiny; unlike the old scheme it does not require
    // rewriting any existing screenshot when a new item is prepended.
    QSaveFile orderFile(historyDir.filePath(QStringLiteral(".snipaste-order")));
    if (!orderFile.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    for (const QString &key : persistedKeys) {
        if (orderFile.write(key.toUtf8() + '\n') < 0)
            return false;
    }
    if (!orderFile.commit())
        return false;

    // Delete evicted entries and their unshared blocks only after the new
    // order is committed, so a failed save leaves the old history intact.
    const QStringList oldFiles = historyDir.entryList(
        {QStringLiteral("*.png"), QStringLiteral("*.background.tiles"),
         QStringLiteral("*.geometry.json")}, QDir::Files);
    for (const QString &file : oldFiles)
        if (!expectedFiles.contains(file))
            historyDir.remove(file);
    removeUnreferencedTiles(historyDir, expectedFiles);
    return true;
}

void ScreenshotHistory::rebaseBackgroundPaths(const QString &directory)
{
    const QDir historyDir(directory);
    for (int index = 0; index < m_imagePaths.size(); ++index) {
        const QString key = index < m_storageKeys.size()
            ? m_storageKeys.at(index)
            : QStringLiteral("%1").arg(index, 3, 10, QChar('0'));
        const QString path = historyDir.filePath(key + QStringLiteral(".png"));
        m_imagePaths[index] = QFileInfo::exists(path) ? path : QString();
    }
    for (int index = 0; index < m_backgroundPaths.size(); ++index) {
        const QString key = index < m_storageKeys.size()
            ? m_storageKeys.at(index)
            : QStringLiteral("%1").arg(index, 3, 10, QChar('0'));
        const QString path = backgroundPathForKey(historyDir, key);
        // Newly-added entries start with an empty path.  They need to be
        // rebased too; otherwise releasing the in-memory frame would make
        // that history item impossible to restore until the next restart.
        m_backgroundPaths[index] = path;
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
    const QString path = m_backgroundPaths.at(index);
    return path.endsWith(QStringLiteral(".background.png")) ? QImage(path) : loadBackgroundTiles(path);
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
        if (!m_storageKeys.isEmpty())
            m_storageKeys.removeLast();
        if (!m_imagePaths.isEmpty())
            m_imagePaths.removeLast();
        if (!m_backgrounds.isEmpty())
            m_backgrounds.removeLast();
        if (!m_backgroundPaths.isEmpty())
            m_backgroundPaths.removeLast();
        if (!m_geometries.isEmpty())
            m_geometries.removeLast();
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
        if (!m_storageKeys.isEmpty())
            m_storageKeys.removeLast();
        if (!m_imagePaths.isEmpty())
            m_imagePaths.removeLast();
        if (!m_backgrounds.isEmpty()) {
            usage -= m_backgrounds.constLast().sizeInBytes();
            m_backgrounds.removeLast();
        }
        if (!m_backgroundPaths.isEmpty())
            m_backgroundPaths.removeLast();
        if (!m_geometries.isEmpty())
            m_geometries.removeLast();
    }
}

} // namespace snipaste::core
