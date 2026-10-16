#include "core/paste/paste_state_store.h"
#include "ui/paste/paste_window.h"
#include "ui/annotation/annotation_canvas.h"
#include <QDataStream>
#include <QIODevice>
#include <QJsonObject>
namespace snipaste::ui
{
namespace
{
QByteArray saveAnnotations(const core::AnnotationDocument &document)
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << quint32(1) << qint32(document.nextNumber()) << qint32(document.shapes().size());
    for (const auto &s : document.shapes())
        stream << s.tool << s.start << s.end << s.sourceRect << s.text << s.points << s.color << s.width
               << s.penStyle << s.textBackground << s.arrowStyle << s.startArrowHead << s.endArrowHead
               << s.textOutlineColor << s.textOutlineWidth << s.fontFamily << s.fontSize << s.textBold
               << s.textItalic << s.cornerRadius << s.magnification << s.numberStyle << s.numberSize
               << s.filled << s.rotation << s.effectSize << s.effectShape << s.magnifierCutout
               << s.magnifierConnectorStyle << s.erasedPath;
    return bytes.toBase64();
}

core::AnnotationDocument loadAnnotations(const QByteArray &bytes)
{
    QDataStream stream(QByteArray::fromBase64(bytes));
    stream.setVersion(QDataStream::Qt_6_0);
    quint32 version = 0;
    qint32 nextNumber = 1, count = 0;
    stream >> version >> nextNumber >> count;
    core::AnnotationDocument document;
    if (version != 1 || count < 0 || count > 100000 || nextNumber < 1 || nextNumber > 100000)
        return document;
    for (int index = 0; index < count; ++index)
    {
        core::AnnotationShape s;
        stream >> s.tool >> s.start >> s.end >> s.sourceRect >> s.text >> s.points >> s.color >> s.width
               >> s.penStyle >> s.textBackground >> s.arrowStyle >> s.startArrowHead >> s.endArrowHead
               >> s.textOutlineColor >> s.textOutlineWidth >> s.fontFamily >> s.fontSize >> s.textBold
               >> s.textItalic >> s.cornerRadius >> s.magnification >> s.numberStyle >> s.numberSize
               >> s.filled >> s.rotation >> s.effectSize >> s.effectShape >> s.magnifierCutout
               >> s.magnifierConnectorStyle >> s.erasedPath;
        if (stream.status() != QDataStream::Ok)
            return {};
        document.append(std::move(s));
    }
    while (document.nextNumber() < nextNumber)
        document.takeNumber();
    document.commit();
    return document;
}
}

core::PasteWindowState PasteWindow::snapshot() const
{
    if (m_canvas)
        m_canvas->finishTextInput();
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
    object.insert(QStringLiteral("showAnnotations"), m_showAnnotations);
    if (m_canvas && !m_canvas->document().shapes().isEmpty())
        object.insert(QStringLiteral("annotations"), QString::fromLatin1(saveAnnotations(m_canvas->document())));
    object.insert(QStringLiteral("shadowSize"), m_shadowSize);
    object.insert(QStringLiteral("shadowColor"), m_shadowColor.name(QColor::HexArgb));
    object.insert(QStringLiteral("border"), m_borderEnabled);
    object.insert(QStringLiteral("borderWidth"), m_borderWidth);
    object.insert(QStringLiteral("borderColor"), m_borderColor.name(QColor::HexArgb));
    object.insert(QStringLiteral("decorationVersion"), 2);
    object.insert(QStringLiteral("customBackgroundColor"), m_customBackgroundColor.name(QColor::HexArgb));
    object.insert(QStringLiteral("virtualDesktop"), this->virtualDesktopPinned());
    object.insert(QStringLiteral("thumbnailMode"), this->thumbnailMode());
    return {sourceImage(), object, m_savedImageKey, m_savedImagePath};
}
void PasteWindow::restore(const core::PasteWindowState &state)
{
    const auto &windowObject = state.options;
    m_showAnnotations = windowObject.value(QStringLiteral("showAnnotations")).toBool(true);
    m_shadowSize = qBound(0, windowObject.value(QStringLiteral("shadowSize")).toInt(m_shadowSize), 100);
    m_shadowColor = QColor(windowObject.value(QStringLiteral("shadowColor")).toString(m_shadowColor.name(QColor::HexArgb)));
    m_borderEnabled = windowObject.value(QStringLiteral("border")).toBool(m_borderEnabled);
    m_borderWidth = qBound(0, windowObject.value(QStringLiteral("borderWidth")).toInt(m_borderWidth), 100);
    m_borderColor = QColor(windowObject.value(QStringLiteral("borderColor")).toString(m_borderColor.name(QColor::HexArgb)));
    if (windowObject.value(QStringLiteral("decorationVersion")).toInt() < 2)
    {
        if (m_borderColor == QColor(255, 205, 60, 255))
            m_borderColor = QColor(220, 225, 232, 220);
        if (m_shadowColor == QColor(0, 0, 0, 150))
            m_shadowColor = QColor(60, 68, 80, 100);
    }
    m_customBackgroundColor = QColor(windowObject.value(QStringLiteral("customBackgroundColor")).toString(m_customBackgroundColor.name(QColor::HexArgb)));
    if (windowObject.contains(QStringLiteral("annotations")))
        setAnnotations(loadAnnotations(windowObject.value(QStringLiteral("annotations")).toString().toLatin1()));
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
