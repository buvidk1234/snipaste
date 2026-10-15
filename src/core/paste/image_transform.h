#pragma once

#include <QImage>

namespace snipaste::core
{

struct ImageTransform
{
    int quarterTurns = 0;
    bool flipHorizontal = false;
    bool flipVertical = false;
    bool grayscale = false;
    bool inverted = false;
};

// Pixel transforms are independent of window scale, screen DPI and menus.
QImage transformImage(const QImage &source, const ImageTransform &transform);

} // namespace snipaste::core
