#include "ui/annotation/annotation_canvas.h"

namespace snipaste::ui
{
bool AnnotationCanvas::editSelectedStyle(const std::function<void(Shape &)> &edit)
{
    if (m_textEditing)
    {
        edit(m_textShape);
        updateTextEditorStyle();
        updateTextEditorGeometry();
        emit annotationStyleChanged();
        emit textStyleChanged();
        return true;
    }
    if (m_selectedShape < 0 || m_selectedShape >= m_document.shapes().size())
        return false;
    Shape changed = m_document.shapes().at(m_selectedShape);
    edit(changed);
    if (changed == m_document.shapes().at(m_selectedShape))
        return false;
    m_document.editShape(m_selectedShape, [&](Shape &shape) { shape = changed; });
    invalidateRenderCache();
    recordHistory();
    emit annotationStyleChanged();
    update();
    return true;
}

void AnnotationCanvas::syncSelectedStyle()
{
    if (m_selectedShape < 0 || m_selectedShape >= m_document.shapes().size() || m_textEditing)
        return;
    const Shape &shape = m_document.shapes().at(m_selectedShape);
    if (m_tool != shape.tool && m_rememberToolColors)
        m_toolStyles.insert(int(m_tool), ToolStyle{m_penColor, m_penWidth, m_penStyle});
    m_tool = shape.tool;
    m_penColor = shape.color;
    m_penWidth = shape.width;
    m_penStyle = shape.penStyle;
    m_shapeFilled = shape.filled;
    m_cornerRadius = shape.cornerRadius;
    m_arrowStyle = shape.arrowStyle;
    m_startArrowHead = (shape.startArrowHead != core::ArrowHead::None || shape.endArrowHead != core::ArrowHead::None)
                           ? shape.startArrowHead
                           : core::startArrowHead(shape.arrowStyle);
    m_endArrowHead = (shape.startArrowHead != core::ArrowHead::None || shape.endArrowHead != core::ArrowHead::None)
                         ? shape.endArrowHead
                         : core::endArrowHead(shape.arrowStyle);
    m_textFontFamily = shape.fontFamily;
    m_textFontSize = shape.fontSize;
    m_textBold = shape.textBold;
    m_textItalic = shape.textItalic;
    m_textBackground = shape.textBackground;
    m_textOutlineWidth = shape.textOutlineWidth;
    m_textOutlineColor = shape.textOutlineColor;
    m_effectSize = shape.effectSize;
    m_effectShape = shape.effectShape;
    m_numberSize = shape.numberSize;
    m_numberStyle = shape.numberStyle;
    m_magnification = shape.magnification;
    m_magnifierCutout = shape.magnifierCutout;
    m_magnifierConnectorStyle = shape.magnifierConnectorStyle;
    updateToolCursor();
    emit penColorChanged(m_penColor);
    emit penWidthChanged(m_penWidth);
    emit penStyleChanged(m_penStyle);
    emit arrowStyleChanged(m_arrowStyle);
    emit annotationStyleChanged();
    if (m_tool == Tool::Text)
        emit textStyleChanged();
    emit selectionChanged();
}
} // namespace snipaste::ui
