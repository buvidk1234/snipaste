#include "ui/annotation/annotation_canvas.h"
#include "ui/annotation/annotation_effect_cache.h"
#include "ui/annotation/annotation_stroke_preview.h"
#include "platform/keyboard.h"
#include <QPlainTextEdit>
#include <QTextDocument>
#include <QTimer>
#include <utility>

namespace snipaste::ui
{

using core::isNonMovableBrush;
using core::translateShape;

AnnotationCanvas::AnnotationCanvas(QImage image, QWidget *parent) : QWidget(parent), m_image(std::move(image))
{
    m_image.setDevicePixelRatio(1);
    m_logicalImageSize = m_image.size();
    // Treat the annotation layer as a small raster editor.  Brush tools keep
    // independent foreground/size state, just like Photoshop brush presets.
    // Effects use a neutral foreground because their cursor and renderer do
    // not paint the selected colour onto the screenshot.
    m_toolStyles.insert(int(Tool::Pen), ToolStyle{QColor(237, 28, 36), 2, Qt::SolidLine});
    m_toolStyles.insert(int(Tool::Marker), ToolStyle{QColor(255, 65, 65), 16, Qt::SolidLine});
    m_toolStyles.insert(int(Tool::Mosaic), ToolStyle{QColor(128, 128, 128), 8, Qt::SolidLine});
    m_toolStyles.insert(int(Tool::Blur), ToolStyle{QColor(128, 128, 128), 8, Qt::SolidLine});
    m_toolStyles.insert(int(Tool::Eraser), ToolStyle{QColor(255, 255, 255), 16, Qt::SolidLine});
    m_toolStyles.insert(int(Tool::Magnifier), ToolStyle{QColor(237, 28, 36), 2, Qt::SolidLine});
    setMinimumSize(640, 420);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    m_textEditor = new QPlainTextEdit(this);
    m_textEditor->setObjectName(QStringLiteral("annotationTextEditor"));
    m_textEditor->setFrameStyle(QFrame::NoFrame);
    m_textEditor->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_textEditor->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_textEditor->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_textEditor->document()->setDocumentMargin(0);
    m_textEditor->setPlaceholderText(QStringLiteral("输入文字"));
    m_textEditor->setToolTip(QStringLiteral("Enter 换行 · %1+Enter 完成 · Esc 取消")
                                 .arg(snipaste::platform::primaryModifierText()));
    m_textEditor->installEventFilter(this);
    m_textEditor->viewport()->installEventFilter(this);
    m_textEditor->setMouseTracking(true);
    m_textEditor->viewport()->setMouseTracking(true);
    m_textEditor->hide();
    connect(m_textEditor, &QPlainTextEdit::textChanged, this, [this] {
        updateTextEditorGeometry();
        updateTextEditorStyle();
        // QPlainTextEdit can scroll to the new cursor after textChanged.
        // Reconcile the viewport once that native key/IME operation ends.
        QTimer::singleShot(0, this, &AnnotationCanvas::updateTextEditorGeometry);
    });
    updateToolCursor();
}

AnnotationCanvas::~AnnotationCanvas()
{
    // Child focus changes during QWidget destruction must not commit into
    // annotation containers whose C++ members have already been destroyed.
    m_textEditor->removeEventFilter(this);
    m_textEditor->viewport()->removeEventFilter(this);
    m_textEditor->disconnect(this);
}

void AnnotationCanvas::setTool(Tool tool)
{
    finishTextInput();
    finishHistoryGesture();
    if (m_selectedShape >= 0 && m_selectedShape < m_document.shapes().size())
    {
        const auto family = [](Tool value) {
            if (value == Tool::Rectangle || value == Tool::RoundedRectangle || value == Tool::Ellipse)
                return 1;
            if (value == Tool::Line || value == Tool::Arrow)
                return 2;
            if (value == Tool::Mosaic || value == Tool::Blur)
                return 3;
            return 0;
        };
        const Tool selectedTool = m_document.shapes().at(m_selectedShape).tool;
        if (selectedTool == tool || (family(tool) != 0 && family(tool) == family(selectedTool)))
        {
            m_tool = tool;
            if (tool == Tool::Line)
                m_arrowStyle = ArrowStyle::None;
            else if (tool == Tool::Arrow && m_arrowStyle == ArrowStyle::None)
                m_arrowStyle = ArrowStyle::End;
            editSelectedStyle([&](Shape &shape) {
                shape.tool = tool;
                if (family(tool) == 2)
                    shape.arrowStyle = m_arrowStyle;
            });
            syncSelectedStyle();
            update();
            return;
        }
    }
    m_selectedShape = -1;
    m_hoveredText = -1;
    m_hoveredShape = -1;
    m_editingShape = false;
    m_resizingHandle = -1;
    m_editingRotation = false;
    if (m_rememberToolColors)
        m_toolStyles.insert(int(m_tool), ToolStyle{m_penColor, m_penWidth, m_penStyle});
    m_tool = tool;
    if (m_rememberToolColors && m_toolStyles.contains(int(tool)))
    {
        const ToolStyle style = m_toolStyles.value(int(tool));
        const QColor oldColor = m_penColor;
        const qreal oldWidth = m_penWidth;
        const Qt::PenStyle oldStyle = m_penStyle;
        m_penColor = style.color.isValid() ? style.color : oldColor;
        if (style.width > 0)
            m_penWidth = style.width;
        m_penStyle = style.penStyle;
        if (m_penColor != oldColor)
            emit penColorChanged(m_penColor);
        if (!qFuzzyCompare(m_penWidth, oldWidth))
            emit penWidthChanged(m_penWidth);
        if (m_penStyle != oldStyle)
            emit penStyleChanged(m_penStyle);
    }
    updateToolCursor();
    if (m_tool == Tool::Text)
        emit textStyleChanged();
    emit annotationStyleChanged();
}

void AnnotationCanvas::setPenColor(const QColor &color)
{
    if (color.isValid() && m_penColor != color)
    {
        m_penColor = color;
        m_toolStyles[int(m_tool)].color = color;
        updateToolCursor();
        emit penColorChanged(m_penColor);
        update();
    }
    if (color.isValid())
        editSelectedStyle([&](Shape &shape) { shape.color = color; });
}

void AnnotationCanvas::setPenWidth(qreal width)
{
    const qreal clamped = qBound<qreal>(1.0, width, 64.0);
    if (!qFuzzyCompare(m_penWidth, clamped))
    {
        m_penWidth = clamped;
        m_toolStyles[int(m_tool)].width = clamped;
        updateToolCursor();
        emit penWidthChanged(m_penWidth);
        update();
    }
    editSelectedStyle([&](Shape &shape) { shape.width = clamped; });
}

void AnnotationCanvas::setPenStyle(Qt::PenStyle style)
{
    if (m_penStyle != style)
    {
        m_penStyle = style;
        updateToolCursor();
        emit penStyleChanged(m_penStyle);
        update();
    }
    editSelectedStyle([&](Shape &shape) { shape.penStyle = style; });
}

void AnnotationCanvas::setTextBackground(bool enabled)
{
    m_textBackground = enabled;
    editTextStyle([&](Shape &shape) { shape.textBackground = enabled; });
    emit textStyleChanged();
    update();
}

void AnnotationCanvas::setTextBold(bool enabled)
{
    m_textBold = enabled;
    editTextStyle([&](Shape &shape) { shape.textBold = enabled; });
    emit textStyleChanged();
    update();
}

void AnnotationCanvas::setTextItalic(bool enabled)
{
    m_textItalic = enabled;
    editTextStyle([&](Shape &shape) { shape.textItalic = enabled; });
    emit textStyleChanged();
    update();
}

void AnnotationCanvas::setTextFontFamily(const QString &family)
{
    if (!family.trimmed().isEmpty())
        m_textFontFamily = family.trimmed();
    editTextStyle([&](Shape &shape) { shape.fontFamily = m_textFontFamily; });
    emit textStyleChanged();
    update();
}

void AnnotationCanvas::setTextFontSize(qreal size)
{
    m_textFontSize = qBound<qreal>(6.0, size, 96.0);
    editTextStyle([&](Shape &shape) { shape.fontSize = m_textFontSize; });
    emit textStyleChanged();
    update();
}

void AnnotationCanvas::setCornerRadius(qreal radius)
{
    m_cornerRadius = qBound<qreal>(0.0, radius, 100.0);
    editSelectedStyle([&](Shape &shape) {
        if (shape.tool == Tool::Rectangle || shape.tool == Tool::RoundedRectangle || shape.tool == Tool::Magnifier)
        {
            shape.cornerRadius = m_cornerRadius;
            if (shape.tool != Tool::Magnifier)
                shape.tool = m_tool = m_cornerRadius > 0 ? Tool::RoundedRectangle : Tool::Rectangle;
        }
    });
    update();
}

void AnnotationCanvas::setMagnification(qreal factor)
{
    m_magnification = qBound<qreal>(1.1, factor, 8.0);
    editSelectedStyle([&](Shape &shape) {
        if (shape.tool != Tool::Magnifier || qFuzzyCompare(shape.magnification, m_magnification))
            return;
        const QRectF target(shape.start, shape.end);
        const QSizeF size = target.size() * (m_magnification / shape.magnification);
        shape.start = target.center() - QPointF(size.width(), size.height()) / 2;
        shape.end = shape.start + QPointF(size.width(), size.height());
        shape.magnification = m_magnification;
    });
    update();
}

void AnnotationCanvas::setNumberStyle(int style)
{
    m_numberStyle = qBound(0, style, 2);
    editSelectedStyle([&](Shape &shape) {
        if (shape.tool == Tool::Number)
            shape.numberStyle = m_numberStyle;
    });
    updateToolCursor();
    update();
}

void AnnotationCanvas::setMagnifierCutout(bool enabled)
{
    m_magnifierCutout = enabled;
    editSelectedStyle([&](Shape &shape) {
        if (shape.tool == Tool::Magnifier)
            shape.magnifierCutout = enabled;
    });
    update();
}

void AnnotationCanvas::setMagnifierConnectorStyle(int style)
{
    m_magnifierConnectorStyle = qBound(0, style, 2);
    editSelectedStyle([&](Shape &shape) {
        if (shape.tool == Tool::Magnifier)
            shape.magnifierConnectorStyle = m_magnifierConnectorStyle;
    });
    update();
}

void AnnotationCanvas::setNumberSize(qreal size)
{
    m_numberSize = qBound<qreal>(12.0, size, 96.0);
    editSelectedStyle([&](Shape &shape) {
        if (shape.tool == Tool::Number)
            shape.numberSize = m_numberSize;
    });
    updateToolCursor();
    update();
}

void AnnotationCanvas::setShapeFilled(bool enabled)
{
    m_shapeFilled = enabled;
    editSelectedStyle([&](Shape &shape) {
        if (shape.tool == Tool::Rectangle || shape.tool == Tool::RoundedRectangle || shape.tool == Tool::Ellipse)
            shape.filled = enabled;
    });
    update();
}

void AnnotationCanvas::setArrowStyle(ArrowStyle style)
{
    m_startArrowHead = core::startArrowHead(style);
    m_endArrowHead = core::endArrowHead(style);
    if (m_arrowStyle != style)
    {
        m_arrowStyle = style;
        emit arrowStyleChanged(m_arrowStyle);
        update();
    }
    editSelectedStyle([&](Shape &shape) {
        if (shape.tool == Tool::Line || shape.tool == Tool::Arrow)
        {
            shape.startArrowHead = m_startArrowHead;
            shape.endArrowHead = m_endArrowHead;
            shape.arrowStyle = style;
            shape.tool = m_tool = style == ArrowStyle::None ? Tool::Line : Tool::Arrow;
        }
    });
}

void AnnotationCanvas::setStartArrowHead(core::ArrowHead head)
{
    m_startArrowHead = head;
    const auto style = core::combineArrowHeads(m_startArrowHead, m_endArrowHead);
    m_arrowStyle = style;
    m_tool = (m_startArrowHead != core::ArrowHead::None || m_endArrowHead != core::ArrowHead::None)
                 ? Tool::Arrow
                 : Tool::Line;
    editSelectedStyle([&](Shape &shape) {
        if (shape.tool == Tool::Line || shape.tool == Tool::Arrow)
        {
            shape.startArrowHead = m_startArrowHead;
            shape.endArrowHead = m_endArrowHead;
            shape.arrowStyle = m_arrowStyle;
            shape.tool = m_tool;
        }
    });
    emit arrowStyleChanged(m_arrowStyle);
    update();
}

void AnnotationCanvas::setEndArrowHead(core::ArrowHead head)
{
    m_endArrowHead = head;
    const auto style = core::combineArrowHeads(m_startArrowHead, m_endArrowHead);
    m_arrowStyle = style;
    m_tool = (m_startArrowHead != core::ArrowHead::None || m_endArrowHead != core::ArrowHead::None)
                 ? Tool::Arrow
                 : Tool::Line;
    editSelectedStyle([&](Shape &shape) {
        if (shape.tool == Tool::Line || shape.tool == Tool::Arrow)
        {
            shape.startArrowHead = m_startArrowHead;
            shape.endArrowHead = m_endArrowHead;
            shape.arrowStyle = m_arrowStyle;
            shape.tool = m_tool;
        }
    });
    emit arrowStyleChanged(m_arrowStyle);
    update();
}

void AnnotationCanvas::setTextOutlineColor(const QColor &color)
{
    if (color.isValid())
        m_textOutlineColor = color;
    editTextStyle([&](Shape &shape) { shape.textOutlineColor = m_textOutlineColor; });
    emit textStyleChanged();
    update();
}

void AnnotationCanvas::setTextOutlineWidth(qreal width)
{
    m_textOutlineWidth = qBound<qreal>(0.0, width, 16.0);
    if (m_textOutlineWidth > 0 && m_textOutlineColor.alpha() == 0)
        m_textOutlineColor = m_penColor.lightnessF() < 0.5 ? QColor(Qt::white) : QColor(Qt::black);
    editTextStyle([&](Shape &shape) {
        shape.textOutlineWidth = m_textOutlineWidth;
        shape.textOutlineColor = m_textOutlineColor;
    });
    emit textStyleChanged();
    update();
}

void AnnotationCanvas::setEffectSize(int size)
{
    m_effectSize = qBound(2, size, 40);
    editSelectedStyle([&](Shape &shape) {
        if (shape.tool == Tool::Mosaic || shape.tool == Tool::Blur)
            shape.effectSize = m_effectSize;
    });
    updateToolCursor();
    update();
}

void AnnotationCanvas::setEffectShape(EffectShape shape)
{
    m_effectShape = shape;
    editSelectedStyle([&](Shape &selected) {
        if (selected.tool == Tool::Mosaic || selected.tool == Tool::Blur || selected.tool == Tool::Magnifier)
            selected.effectShape = shape;
    });
    updateToolCursor();
    update();
}

void AnnotationCanvas::setCanvasFrame(QPoint origin, QSizeF logicalSize, QRect outputViewport)
{
    m_coordinateOrigin = origin;
    if (logicalSize.isValid() && logicalSize.width() > 0.0 && logicalSize.height() > 0.0)
    {
        m_logicalImageSize = logicalSize;
        m_hasFixedLogicalSize = true;
    }
    m_outputViewport = outputViewport;
    updateToolCursor();
    updateTextEditorGeometry();
    update();
}

void AnnotationCanvas::setCaptureImage(QImage image, QPointF annotationOffset, QSizeF logicalSize)
{
    finishTextInput();
    finishHistoryGesture();
    // Resizing a screenshot changes the crop, not the scale of existing marks.
    // Retain even clipped shapes so expanding the crop or redoing restores them.
    m_document.translate(annotationOffset);
    m_start += annotationOffset;
    m_current += annotationOffset;
    for (auto &point : m_currentPoints)
        point += annotationOffset;
    image.setDevicePixelRatio(1);
    m_image = std::move(image);
    m_effectCache.reset();
    invalidateRenderCache();
    if (logicalSize.isValid() && logicalSize.width() > 0.0 && logicalSize.height() > 0.0)
    {
        m_logicalImageSize = logicalSize;
        m_hasFixedLogicalSize = true;
    }
    else if (!m_hasFixedLogicalSize)
    {
        m_logicalImageSize = m_image.size();
    }
    update();
}

AnnotationCanvas::Shape AnnotationCanvas::currentShape() const
{
    Shape shape;
    shape.tool = m_tool;
    shape.start = m_start;
    shape.end = m_current;
    shape.points = m_currentPoints;
    shape.color = m_penColor;
    shape.width = m_penWidth;
    shape.penStyle = m_penStyle;
    shape.textBackground = m_textBackground;
    shape.arrowStyle = m_arrowStyle;
    shape.startArrowHead = m_startArrowHead;
    shape.endArrowHead = m_endArrowHead;
    shape.textOutlineColor = m_textOutlineColor;
    shape.textOutlineWidth = m_textOutlineWidth;
    shape.fontFamily = m_textFontFamily;
    shape.fontSize = m_textFontSize;
    shape.textBold = m_textBold;
    shape.textItalic = m_textItalic;
    shape.cornerRadius = m_cornerRadius;
    shape.magnification = m_magnification;
    shape.numberStyle = m_numberStyle;
    shape.numberSize = m_numberSize;
    shape.filled = m_shapeFilled;
    shape.effectSize = m_effectSize;
    shape.effectShape = m_effectShape;
    shape.magnifierCutout = m_magnifierCutout;
    shape.magnifierConnectorStyle = m_magnifierConnectorStyle;
    if (m_tool == Tool::Magnifier)
    {
        // The first drag selects the original pixels to magnify.  Keep that
        // rectangle as the inner/source frame and derive a larger display
        // frame around its centre.  This makes the dashed inner frame show
        // exactly the area the user selected instead of shrinking a second,
        // unrelated source rectangle inside it.
        const QRectF source = QRectF(m_start, m_current).normalized();
        shape.sourceRect = source;

        // Snipaste's first magnifier gesture expands the selected source to
        // Use the selected magnification while keeping the selected source
        // rectangle at the center of the display frame.
        QSizeF targetSize(source.size() * m_magnification);
        if (!m_image.isNull())
        {
            // Keep the generated display frame on the canvas when a large
            // source region is selected near an image edge.
            targetSize.setWidth(qMin(targetSize.width(), qreal(m_image.width())));
            targetSize.setHeight(qMin(targetSize.height(), qreal(m_image.height())));
        }
        targetSize.setWidth(qMax(targetSize.width(), source.width()));
        targetSize.setHeight(qMax(targetSize.height(), source.height()));

        QRectF target(QPointF(source.center().x() - targetSize.width() / 2.0,
                              source.center().y() - targetSize.height() / 2.0),
                      targetSize);
        if (!m_image.isNull())
        {
            const QRectF bounds(QPointF(0, 0), QSizeF(m_image.size()));
            if (target.left() < bounds.left())
                target.moveLeft(bounds.left());
            if (target.top() < bounds.top())
                target.moveTop(bounds.top());
            if (target.right() > bounds.right())
                target.moveRight(bounds.right());
            if (target.bottom() > bounds.bottom())
                target.moveBottom(bounds.bottom());
        }
        shape.start = target.topLeft();
        shape.end = target.bottomRight();
    }
    return shape;
}

void AnnotationCanvas::recordHistory()
{
    if (m_document.commit())
        emit historyChanged();
}

void AnnotationCanvas::documentRestored()
{
    m_selectedShape = -1;
    m_hoveredText = -1;
    m_hoveredShape = -1;
    invalidateRenderCache();
    updateToolCursor();
    emit historyChanged();
    update();
}

QRectF AnnotationCanvas::imageRect() const
{
    if (m_image.isNull())
        return {};
    if (m_hasFixedLogicalSize && !m_logicalImageSize.isEmpty())
    {
        // A capture canvas represents screen pixels at a fixed device scale.
        // Keep that scale while the selection frame changes so the old pixels
        // stay fixed and only the newly covered desktop area is revealed.
        return QRectF(QPointF(0, 0), m_logicalImageSize);
    }
    const QSizeF available = size();
    const qreal scale = qMin(available.width() / m_image.width(), available.height() / m_image.height());
    const QSizeF scaledSize = QSizeF(m_image.size()) * qMax<qreal>(0.01, scale);
    return QRectF((available.width() - scaledSize.width()) / 2.0,
                  (available.height() - scaledSize.height()) / 2.0, scaledSize.width(), scaledSize.height());
}

QRect AnnotationCanvas::outputPixelRect() const
{
    if (m_image.isNull() || m_outputViewport.isEmpty() || m_logicalImageSize.isEmpty())
        return {};
    const qreal sx = qreal(m_image.width()) / m_logicalImageSize.width();
    const qreal sy = qreal(m_image.height()) / m_logicalImageSize.height();
    const int left = qRound(m_outputViewport.left() * sx);
    const int top = qRound(m_outputViewport.top() * sy);
    const int right = qRound((m_outputViewport.left() + m_outputViewport.width()) * sx);
    const int bottom = qRound((m_outputViewport.top() + m_outputViewport.height()) * sy);
    return QRect(left, top, qMax(1, right - left), qMax(1, bottom - top)).intersected(m_image.rect());
}

QPointF AnnotationCanvas::toImagePoint(const QPointF &point) const
{
    const QRectF rect = imageRect();
    if (rect.isEmpty())
        return {};
    // Mouse positions are local to the selection child.  Add its origin so
    // all annotation coordinates stay in the full overlay image space.
    const QPointF framePoint = point + QPointF(m_coordinateOrigin);
    const qreal x = (framePoint.x() - rect.left()) * m_image.width() / rect.width();
    const qreal y = (framePoint.y() - rect.top()) * m_image.height() / rect.height();
    return QPointF(qBound<qreal>(0, x, m_image.width()), qBound<qreal>(0, y, m_image.height()));
}

void AnnotationCanvas::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateToolCursor();
    updateTextEditorGeometry();
}

void AnnotationCanvas::undo()
{
    finishTextInput();
    finishHistoryGesture();
    if (m_document.undo())
        documentRestored();
}

void AnnotationCanvas::redo()
{
    finishTextInput();
    finishHistoryGesture();
    if (m_document.redo())
        documentRestored();
}

void AnnotationCanvas::clearAnnotations()
{
    finishTextInput(false);
    finishHistoryGesture();
    if (m_document.shapes().isEmpty())
        return;
    m_document.clear();
    m_selectedShape = -1;
    invalidateRenderCache();
    recordHistory();
    update();
}

void AnnotationCanvas::resetCapture()
{
    finishTextInput(false);
    m_textEditor->clear();
    m_textEditor->document()->clearUndoRedoStacks();
    m_textShape = {};
    m_textShapeIndex = m_hoveredText = m_hoveredShape = -1;
    m_image = {};
    m_renderCache = {};
    m_effectCache.reset();
    m_strokePreview.reset();
    m_renderCacheValid = false;
    m_previewBackground = {};
    m_previewSourceKey = 0;
    m_logicalImageSize = {};
    m_hasFixedLogicalSize = false;
    m_coordinateOrigin = {};
    m_outputViewport = {};
    m_document.reset();
    m_shapeClipboard.clear();
    m_dragging = m_editingShape = m_editingRotation = false;
    m_resizingHandle = m_selectedShape = -1;
    m_editingMagnifierPart = MagnifierPart::None;
    m_start = m_current = {};
    m_currentPoints.clear();
    setTool(Tool::Rectangle);
}

} // namespace snipaste::ui
