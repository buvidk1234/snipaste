#pragma once

#include <QImage>
#include <QList>
#include <QString>

namespace snipaste::core {

class ScreenshotHistory final
{
public:
    explicit ScreenshotHistory(int maximumCount = 20);

    void setMaximumCount(int maximumCount);
    void add(const QImage &image, const QImage &background = {});
    void clear();
    void loadFromDirectory(const QString &directory);
    void saveToDirectory(const QString &directory) const;
    int size() const;
    const QList<QImage> &images() const;
    const QList<QImage> &backgrounds() const;

private:
    int m_maximumCount;
    QList<QImage> m_images;
    QList<QImage> m_backgrounds;
};

} // namespace snipaste::core
