#include "core/history/screenshot_history.h"

#include <QDir>

namespace snipaste::core {

ScreenshotHistory::ScreenshotHistory(int maximumCount)
    : m_maximumCount(qMax(1, maximumCount))
{
}

void ScreenshotHistory::setMaximumCount(int maximumCount)
{
    m_maximumCount = qMax(1, maximumCount);
    while (m_images.size() > m_maximumCount) {
        m_images.removeLast();
        if (!m_backgrounds.isEmpty())
            m_backgrounds.removeLast();
    }
}

void ScreenshotHistory::add(const QImage &image, const QImage &background)
{
    if (image.isNull())
        return;
    m_images.prepend(image);
    m_backgrounds.prepend(background);
    while (m_images.size() > m_maximumCount) {
        m_images.removeLast();
        m_backgrounds.removeLast();
    }
}

void ScreenshotHistory::clear()
{
    m_images.clear();
    m_backgrounds.clear();
}

void ScreenshotHistory::loadFromDirectory(const QString &directory)
{
    m_images.clear();
    m_backgrounds.clear();
    const QDir dir(directory);
    const QStringList files = dir.entryList({QStringLiteral("*.png")}, QDir::Files, QDir::Name);
    for (const QString &file : files) {
        if (file.endsWith(QStringLiteral(".background.png")))
            continue;
        QImage image;
        if (image.load(dir.filePath(file))) {
            m_images.append(std::move(image));
            const QString backgroundPath = dir.filePath(
                file.left(file.size() - QStringLiteral(".png").size())
                + QStringLiteral(".background.png"));
            QImage background;
            if (background.load(backgroundPath))
                m_backgrounds.append(std::move(background));
            else
                m_backgrounds.append(QImage());
        }
        if (m_images.size() >= m_maximumCount)
            break;
    }
}

void ScreenshotHistory::saveToDirectory(const QString &directory) const
{
    QDir dir;
    if (!dir.mkpath(directory))
        return;
    QDir historyDir(directory);
    const QStringList oldFiles = historyDir.entryList({QStringLiteral("*.png")}, QDir::Files);
    for (const QString &file : oldFiles)
        historyDir.remove(file);
    for (int index = 0; index < m_images.size(); ++index) {
        const QString path = historyDir.filePath(
            QStringLiteral("%1.png").arg(index, 3, 10, QChar('0')));
        m_images.at(index).save(path, "PNG");
        if (index < m_backgrounds.size() && !m_backgrounds.at(index).isNull()) {
            const QString backgroundPath = historyDir.filePath(
                QStringLiteral("%1.background.png").arg(index, 3, 10, QChar('0')));
            m_backgrounds.at(index).save(backgroundPath, "PNG");
        }
    }
}

int ScreenshotHistory::size() const
{
    return m_images.size();
}

const QList<QImage> &ScreenshotHistory::images() const
{
    return m_images;
}

const QList<QImage> &ScreenshotHistory::backgrounds() const
{
    return m_backgrounds;
}

} // namespace snipaste::core
