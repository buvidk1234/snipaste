#include "core/paste/image_transform.h"
#include <QTransform>

namespace snipaste::core
{

QImage transformImage(const QImage &source, const ImageTransform &transform)
{
    QImage image = source;
    if (image.isNull())
        return image;
    if (transform.quarterTurns != 0)
        image = image.transformed(QTransform().rotate(90 * transform.quarterTurns), Qt::SmoothTransformation);
    if (transform.flipHorizontal || transform.flipVertical)
    {
        Qt::Orientations axes;
        if (transform.flipHorizontal)
            axes |= Qt::Horizontal;
        if (transform.flipVertical)
            axes |= Qt::Vertical;
        image = image.flipped(axes);
    }
    if (transform.grayscale)
        image = image.convertToFormat(QImage::Format_Grayscale8);
    if (transform.inverted)
        image.invertPixels();
    image.setDevicePixelRatio(1);
    return image;
}

} // namespace snipaste::core
