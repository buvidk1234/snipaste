#include "core/paste/paste_state_store.h"
#include "ui/paste/paste_window.h"
#include <QJsonObject>
namespace snipaste::ui
{
core::PasteWindowState PasteWindow::snapshot() const
{
    QJsonObject object;

    object.insert(QStringLiteral("visible"), this->isVisible());
    object.insert(QStringLiteral("x"), this->x());
    object.insert(QStringLiteral("y"), this->y());
    object.insert(QStringLiteral("opacity"), this->windowOpacity());
    object.insert(QStringLiteral("rotation"), this->rotationQuarterTurns());
    object.insert(QStringLiteral("flipHorizontal"), this->flipHorizontal());
    object.insert(QStringLiteral("flipVertical"), this->flipVertical());
    object.insert(QStringLiteral("grayscale"), this->grayscale());
    object.insert(QStringLiteral("inverted"), this->inverted());
    object.insert(QStringLiteral("scale"), this->scale());
    object.insert(QStringLiteral("transparentBackground"), this->transparentBackground());
    object.insert(QStringLiteral("backgroundMode"), this->backgroundMode());
    object.insert(QStringLiteral("alwaysOnTop"), this->alwaysOnTop());
    object.insert(QStringLiteral("clickThrough"), this->clickThrough());
    object.insert(QStringLiteral("shadow"), this->shadowEnabled());
    object.insert(QStringLiteral("virtualDesktop"), this->virtualDesktopPinned());
    object.insert(QStringLiteral("thumbnailMode"), this->thumbnailMode());
    return {sourceImage(), object, m_savedImageKey, m_savedImagePath};
}
void PasteWindow::restore(const core::PasteWindowState &state)
{
    const auto &windowObject = state.options;
    this->move(windowObject.value(QStringLiteral("x")).toInt(),
               windowObject.value(QStringLiteral("y")).toInt());
    this->setWindowOpacity(windowObject.value(QStringLiteral("opacity")).toDouble(1.0));
    this->restoreTransform(windowObject.value(QStringLiteral("rotation")).toInt(),
                           windowObject.value(QStringLiteral("flipHorizontal")).toBool(),
                           windowObject.value(QStringLiteral("flipVertical")).toBool(),
                           windowObject.value(QStringLiteral("grayscale")).toBool(),
                           windowObject.value(QStringLiteral("inverted")).toBool(),
                           windowObject.value(QStringLiteral("scale")).toDouble(1.0),
                           windowObject.value(QStringLiteral("transparentBackground")).toBool(),
                           windowObject.value(QStringLiteral("alwaysOnTop")).toBool(true));
    this->setThumbnailMode(windowObject.value(QStringLiteral("thumbnailMode")).toBool(false));
    if (windowObject.contains(QStringLiteral("backgroundMode")))
        this->setBackgroundMode(windowObject.value(QStringLiteral("backgroundMode")).toString());
    if (windowObject.contains(QStringLiteral("virtualDesktop")))
        this->setVirtualDesktopPinned(windowObject.value(QStringLiteral("virtualDesktop")).toBool());
    this->setClickThrough(windowObject.value(QStringLiteral("clickThrough")).toBool());
    this->setShadowEnabled(windowObject.contains(QStringLiteral("shadow"))
                               ? windowObject.value(QStringLiteral("shadow")).toBool()
                               : shadowEnabled());
    m_savedImageKey = sourceImage().cacheKey();
    m_savedImagePath = state.savedImagePath;
}
} // namespace snipaste::ui
