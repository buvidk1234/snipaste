#include "ui/annotation/annotation_dialog.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QColorDialog>
#include <QCursor>
#include <QFileDialog>
#include <QFocusEvent>
#include <QHBoxLayout>
#include <QInputMethod>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QMenu>
#include <QActionGroup>
#include <QPixmap>
#include <QSpinBox>
#include <QTextDocument>
#include <QToolButton>
#include <QTransform>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <cmath>
#include <limits>
#include <utility>

namespace snipaste::ui {

namespace {

bool isNonMovableBrush(AnnotationCanvas::Tool tool)
{
    return tool == AnnotationCanvas::Tool::Pen
        || tool == AnnotationCanvas::Tool::Marker
        || tool == AnnotationCanvas::Tool::Mosaic
        || tool == AnnotationCanvas::Tool::Blur;
}

} // namespace

AnnotationCanvas::AnnotationCanvas(QImage image, QWidget *parent)
    : QWidget(parent)
    , m_image(std::move(image))
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
    m_textEditor->setToolTip(QStringLiteral("Enter 换行 · Ctrl+Enter 完成 · Esc 取消"));
    m_textEditor->installEventFilter(this);
    m_textEditor->hide();
    connect(m_textEditor, &QPlainTextEdit::textChanged, this, [this] {
        updateTextEditorGeometry();
        update();
    });
    updateToolCursor();
}

AnnotationCanvas::~AnnotationCanvas()
{
    // Child focus changes during QWidget destruction must not commit into
    // annotation containers whose C++ members have already been destroyed.
    m_textEditor->removeEventFilter(this);
    m_textEditor->disconnect(this);
}

void AnnotationCanvas::setTool(Tool tool)
{
    finishTextInput();
    m_selectedShape = -1;
    m_editingShape = false;
    m_resizingHandle = -1;
    m_editingRotation = false;
    m_toolStyles.insert(int(m_tool), ToolStyle{m_penColor, m_penWidth, m_penStyle});
    m_tool = tool;
    if (m_toolStyles.contains(int(tool))) {
        const ToolStyle style = m_toolStyles.value(int(tool));
        const QColor oldColor = m_penColor;
        const qreal oldWidth = m_penWidth;
        const Qt::PenStyle oldStyle = m_penStyle;
        m_penColor = style.color.isValid() ? style.color : oldColor;
        if (style.width > 0)
            m_penWidth = style.width;
        if (m_penColor != oldColor)
            emit penColorChanged(m_penColor);
        if (!qFuzzyCompare(m_penWidth, oldWidth))
            emit penWidthChanged(m_penWidth);
        if (m_penStyle != oldStyle)
            emit penStyleChanged(m_penStyle);
    }
    updateToolCursor();
}

void AnnotationCanvas::setPenColor(const QColor &color)
{
    if (color.isValid() && m_penColor != color) {
        m_penColor = color;
        m_toolStyles[int(m_tool)].color = color;
        updateToolCursor();
        emit penColorChanged(m_penColor);
        update();
    }
}

void AnnotationCanvas::setPenWidth(qreal width)
{
    const qreal clamped = qBound<qreal>(1.0, width, 64.0);
    if (!qFuzzyCompare(m_penWidth, clamped)) {
        m_penWidth = clamped;
        m_toolStyles[int(m_tool)].width = clamped;
        updateToolCursor();
        emit penWidthChanged(m_penWidth);
        update();
    }
}

void AnnotationCanvas::setPenStyle(Qt::PenStyle style)
{
    if (m_penStyle != style) {
        m_penStyle = style;
        updateToolCursor();
        emit penStyleChanged(m_penStyle);
        update();
    }
}

void AnnotationCanvas::setTextBackground(bool enabled)
{
    m_textBackground = enabled;
    update();
}

void AnnotationCanvas::setTextBold(bool enabled)
{
    m_textBold = enabled;
    updateTextEditorGeometry();
    update();
}

void AnnotationCanvas::setTextItalic(bool enabled)
{
    m_textItalic = enabled;
    updateTextEditorGeometry();
    update();
}

void AnnotationCanvas::setTextFontFamily(const QString &family)
{
    if (!family.trimmed().isEmpty())
        m_textFontFamily = family.trimmed();
    updateTextEditorGeometry();
    update();
}

void AnnotationCanvas::setTextFontSize(qreal size)
{
    m_textFontSize = qBound<qreal>(6.0, size, 96.0);
    updateTextEditorGeometry();
    update();
}

void AnnotationCanvas::setCornerRadius(qreal radius)
{
    m_cornerRadius = qBound<qreal>(0.0, radius, 100.0);
    if (m_selectedShape >= 0 && m_selectedShape < m_shapes.size())
        m_shapes[m_selectedShape].cornerRadius = m_cornerRadius;
    update();
}

void AnnotationCanvas::setMagnification(qreal factor)
{
    m_magnification = qBound<qreal>(1.1, factor, 8.0);
    if (m_selectedShape >= 0 && m_selectedShape < m_shapes.size())
        m_shapes[m_selectedShape].magnification = m_magnification;
    update();
}

void AnnotationCanvas::setNumberStyle(int style)
{
    m_numberStyle = qBound(0, style, 2);
    if (m_selectedShape >= 0 && m_selectedShape < m_shapes.size())
        m_shapes[m_selectedShape].numberStyle = m_numberStyle;
    updateToolCursor();
    update();
}

void AnnotationCanvas::setNumberSize(qreal size)
{
    m_numberSize = qBound<qreal>(12.0, size, 96.0);
    if (m_selectedShape >= 0 && m_selectedShape < m_shapes.size())
        m_shapes[m_selectedShape].numberSize = m_numberSize;
    updateToolCursor();
    update();
}

void AnnotationCanvas::setShapeFilled(bool enabled)
{
    m_shapeFilled = enabled;
    if (m_selectedShape >= 0 && m_selectedShape < m_shapes.size())
        m_shapes[m_selectedShape].filled = enabled;
    update();
}

void AnnotationCanvas::setArrowStyle(ArrowStyle style)
{
    if (m_arrowStyle != style) {
        m_arrowStyle = style;
        emit arrowStyleChanged(m_arrowStyle);
        update();
    }
}

void AnnotationCanvas::setTextOutlineColor(const QColor &color)
{
    if (color.isValid())
        m_textOutlineColor = color;
    update();
}

void AnnotationCanvas::setTextOutlineWidth(qreal width)
{
    m_textOutlineWidth = qBound<qreal>(0.0, width, 16.0);
    update();
}

void AnnotationCanvas::setEffectSize(int size)
{
    m_effectSize = qBound(2, size, 40);
    updateToolCursor();
    update();
}

void AnnotationCanvas::setEffectShape(EffectShape shape)
{
    m_effectShape = shape;
    updateToolCursor();
    update();
}

void AnnotationCanvas::setCanvasFrame(QPoint origin, QSizeF logicalSize, QRect outputViewport)
{
    m_coordinateOrigin = origin;
    if (logicalSize.isValid() && logicalSize.width() > 0.0 && logicalSize.height() > 0.0) {
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
    // Resizing a screenshot changes the crop, not the scale of existing marks.
    // Retain even clipped shapes so expanding the crop or redoing restores them.
    const auto translate = [annotationOffset](Shape &shape) {
        shape.start += annotationOffset;
        shape.end += annotationOffset;
        shape.sourceRect.translate(annotationOffset);
        for (auto &point : shape.points)
            point += annotationOffset;
    };
    for (auto &shape : m_shapes) translate(shape);
    for (auto &shape : m_redoShapes) translate(shape);
    for (auto &shape : m_eraserUndoSnapshot) translate(shape);
    for (auto &shape : m_eraserRedoSnapshot) translate(shape);
    m_start += annotationOffset;
    m_current += annotationOffset;
    for (auto &point : m_currentPoints) point += annotationOffset;
    image.setDevicePixelRatio(1);
    m_image = std::move(image);
    if (logicalSize.isValid() && logicalSize.width() > 0.0 && logicalSize.height() > 0.0) {
        m_logicalImageSize = logicalSize;
        m_hasFixedLogicalSize = true;
    } else if (!m_hasFixedLogicalSize) {
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
    if (m_tool == Tool::Magnifier) {
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
        if (!m_image.isNull()) {
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
        if (!m_image.isNull()) {
            const QRectF bounds(QPointF(0, 0), QSizeF(m_image.size()));
            if (target.left() < bounds.left()) target.moveLeft(bounds.left());
            if (target.top() < bounds.top()) target.moveTop(bounds.top());
            if (target.right() > bounds.right()) target.moveRight(bounds.right());
            if (target.bottom() > bounds.bottom()) target.moveBottom(bounds.bottom());
        }
        shape.start = target.topLeft();
        shape.end = target.bottomRight();
    }
    return shape;
}

QRectF AnnotationCanvas::magnifierTargetRect(const Shape &shape) const
{
    if (shape.tool != Tool::Magnifier)
        return QRectF(shape.start, shape.end).normalized();
    // The generated display frame is stored by its two corners.  The source
    // rectangle is kept separately in sourceRect, so hit testing, painting,
    // and sampling all use the same outer/inner geometry after either frame
    // is moved independently.
    return QRectF(shape.start, shape.end).normalized();
}

QRectF AnnotationCanvas::shapeBounds(const Shape &shape) const
{
    QRectF bounds(shape.start, shape.end);
    if (!shape.sourceRect.isEmpty())
        bounds = bounds.united(shape.sourceRect);
    for (const QPointF &point : shape.points)
        bounds |= QRectF(point, QSizeF(0, 0));
    bounds = bounds.normalized();
    const qreal margin = qMax<qreal>(6.0, shape.width * 1.5);
    bounds = bounds.adjusted(-margin, -margin, margin, margin);
    if (qFuzzyIsNull(shape.rotation))
        return bounds;
    const QPointF center = bounds.center();
    const QTransform transform = QTransform().translate(center.x(), center.y())
        .rotate(shape.rotation).translate(-center.x(), -center.y());
    QRectF rotated;
    const QPolygonF corners({bounds.topLeft(), bounds.topRight(), bounds.bottomRight(), bounds.bottomLeft()});
    for (const QPointF &corner : corners)
        rotated |= QRectF(transform.map(corner), QSizeF(0, 0));
    return rotated;
}

int AnnotationCanvas::shapeAt(const QPointF &point) const
{
    for (int index = m_shapes.size() - 1; index >= 0; --index) {
        if (isNonMovableBrush(m_shapes.at(index).tool))
            continue;
        if (shapeBounds(m_shapes.at(index)).contains(point))
            return index;
    }
    return -1;
}

void AnnotationCanvas::invalidateEraserHistory()
{
    m_eraserUndoSnapshot.clear();
    m_eraserRedoSnapshot.clear();
    m_eraserUndoAvailable = false;
    m_eraserRedoAvailable = false;
    m_lastOperationWasEraser = false;
}

QVector<AnnotationCanvas::Shape> AnnotationCanvas::erasedShapeParts(const Shape &shape,
                                                                      const QPointF &from,
                                                                      const QPointF &to) const
{
    const qreal radius = qMax<qreal>(1.0, m_penWidth * 0.5);
    const auto distanceToSegment = [](const QPointF &point,
                                      const QPointF &a,
                                      const QPointF &b) {
        const QPointF delta = b - a;
        const qreal lengthSquared = QPointF::dotProduct(delta, delta);
        if (lengthSquared <= 0.0001)
            return QLineF(point, a).length();
        const qreal t = qBound<qreal>(0.0,
                                      QPointF::dotProduct(point - a, delta) / lengthSquared,
                                      1.0);
        return QLineF(point, a + delta * t).length();
    };
    const auto covered = [&](const QPointF &point) {
        return distanceToSegment(point, from, to) <= radius;
    };

    const bool brush = shape.tool == Tool::Pen || shape.tool == Tool::Marker
        || ((shape.tool == Tool::Mosaic || shape.tool == Tool::Blur)
            && shape.effectShape == EffectShape::Brush);
    if (!brush) {
        const QRectF eraseBounds(QPointF(qMin(from.x(), to.x()) - radius,
                                         qMin(from.y(), to.y()) - radius),
                                 QPointF(qMax(from.x(), to.x()) + radius,
                                         qMax(from.y(), to.y()) + radius));
        return shapeBounds(shape).intersects(eraseBounds)
            ? QVector<Shape>() : QVector<Shape>{shape};
    }

    QVector<QPointF> points = shape.points;
    if (points.isEmpty())
        points = {shape.start, shape.end};
    if (points.size() > 1) {
        QVector<QPointF> sampled;
        sampled.reserve(points.size() * 2);
        sampled.append(points.first());
        for (int index = 1; index < points.size(); ++index) {
            const QPointF a = points.at(index - 1);
            const QPointF b = points.at(index);
            const qreal length = QLineF(a, b).length();
            const int steps = qMax(1, qCeil(length / qMax<qreal>(1.0, radius * 0.45)));
            for (int step = 1; step <= steps; ++step)
                sampled.append(a + (b - a) * (qreal(step) / steps));
        }
        points = std::move(sampled);
    }

    QVector<Shape> result;
    QVector<QPointF> run;
    const auto flush = [&] {
        if (run.isEmpty())
            return;
        if (run.size() == 1 && points.size() > 1)
            run.clear();
        else {
            Shape part = shape;
            part.points = run;
            part.start = run.first();
            part.end = run.last();
            result.append(std::move(part));
            run.clear();
        }
    };
    for (const QPointF &point : points) {
        if (covered(point))
            flush();
        else
            run.append(point);
    }
    flush();
    return result;
}

void AnnotationCanvas::applyEraserSegment(const QPointF &from, const QPointF &to)
{
    QVector<Shape> next;
    next.reserve(m_shapes.size());
    bool changed = false;
    for (const Shape &shape : std::as_const(m_shapes)) {
        const QVector<Shape> parts = erasedShapeParts(shape, from, to);
        if (parts.size() != 1 || (parts.size() == 1 && parts.first().points != shape.points))
            changed = true;
        next += parts;
    }
    if (!changed)
        return;
    m_shapes = std::move(next);
    m_redoShapes.clear();
    emit historyChanged();
    update();
}

void AnnotationCanvas::translateShape(Shape &shape, const QPointF &delta) const
{
    shape.start += delta;
    shape.end += delta;
    shape.sourceRect.translate(delta);
    for (QPointF &point : shape.points)
        point += delta;
}

void AnnotationCanvas::translateMagnifierPart(Shape &shape,
                                              MagnifierPart part,
                                              const QPointF &delta) const
{
    if (shape.tool != Tool::Magnifier || part == MagnifierPart::None) {
        translateShape(shape, delta);
        return;
    }
    if (part == MagnifierPart::Source) {
        shape.sourceRect.translate(delta);
        return;
    }
    shape.start += delta;
    shape.end += delta;
}

void AnnotationCanvas::rotateSelected(qreal degrees)
{
    if (m_selectedShape < 0 || m_selectedShape >= m_shapes.size())
        return;
    Shape &shape = m_shapes[m_selectedShape];
    invalidateEraserHistory();
    shape.rotation = std::fmod(shape.rotation + degrees + 360.0, 360.0);
    if (shape.rotation > 180.0)
        shape.rotation -= 360.0;
    m_redoShapes.clear();
    emit historyChanged();
    update();
}

QRectF AnnotationCanvas::imageRect() const
{
    if (m_image.isNull())
        return {};
    if (m_hasFixedLogicalSize && !m_logicalImageSize.isEmpty()) {
        // A capture canvas represents screen pixels at a fixed device scale.
        // Keep that scale while the selection frame changes so the old pixels
        // stay fixed and only the newly covered desktop area is revealed.
        return QRectF(QPointF(0, 0), m_logicalImageSize);
    }
    const QSizeF available = size();
    const qreal scale = qMin(available.width() / m_image.width(),
                             available.height() / m_image.height());
    const QSizeF scaledSize = QSizeF(m_image.size()) * qMax<qreal>(0.01, scale);
    return QRectF((available.width() - scaledSize.width()) / 2.0,
                  (available.height() - scaledSize.height()) / 2.0,
                  scaledSize.width(),
                  scaledSize.height());
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
    return QRect(left, top, qMax(1, right - left), qMax(1, bottom - top))
        .intersected(m_image.rect());
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

void AnnotationCanvas::updateToolCursor()
{
    // Shape and text tools use the normal crosshair.  Brush-like tools get a
    // footprint cursor so the pointer communicates both the tool and its
    // current thickness before the user starts drawing.
    if (m_tool == Tool::Number) {
        const int cursorSize = qBound(24, qCeil(m_numberSize) + 8, 112);
        QPixmap pixmap(cursorSize, cursorSize);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QPointF center(cursorSize / 2.0, cursorSize / 2.0);
        const qreal radius = m_numberSize / 2.0;
        if (m_numberStyle == 0) {
            painter.setPen(QPen(Qt::white, 2.0));
            painter.setBrush(m_penColor);
            painter.drawEllipse(center, radius, radius);
        } else if (m_numberStyle == 1) {
            painter.setPen(QPen(m_penColor, 2.0));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(center, radius, radius);
        }
        QFont numberFont = font();
        numberFont.setFamily(QStringLiteral("Microsoft YaHei UI"));
        numberFont.setPixelSize(qMax(8, qRound(m_numberSize * 0.52)));
        numberFont.setBold(true);
        painter.setFont(numberFont);
        painter.setPen(m_numberStyle == 0 ? Qt::white : m_penColor);
        painter.drawText(QRectF(center.x() - radius, center.y() - radius,
                                radius * 2.0, radius * 2.0),
                         Qt::AlignCenter, QString::number(m_nextNumber));
        painter.end();
        setCursor(QCursor(pixmap, cursorSize / 2, cursorSize / 2));
        return;
    }
    if (m_tool != Tool::Pen && m_tool != Tool::Marker
        && m_tool != Tool::Mosaic && m_tool != Tool::Blur
        && m_tool != Tool::Eraser) {
        setCursor(Qt::CrossCursor);
        return;
    }

    const qreal scale = m_image.isNull() || m_image.width() <= 0
        ? 1.0
        : qBound<qreal>(0.25, imageRect().width() / m_image.width(), 4.0);
    // Keep the cursor footprint in the same coordinate space as the rendered
    // brush.  The previous 24px cap made a 64px mosaic stroke look two to
    // three times larger than its cursor after using the wheel to increase it.
    const qreal visibleWidth = qMax<qreal>(1.0, m_penWidth * scale);
    // Leave a small margin for the outline/cross and keep the cursor within a
    // practical Windows cursor size while still representing the full brush
    // range (the brush itself is capped at 64 logical pixels).
    const int canvasSize = qBound(32, qCeil(visibleWidth) + 8, 256);
    QPixmap pixmap(canvasSize, canvasSize);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QPointF center(canvasSize / 2.0, canvasSize / 2.0);

    if (m_tool == Tool::Eraser) {
        const qreal radius = qMax<qreal>(0.5, visibleWidth / 2.0);
        painter.setPen(QPen(Qt::white, 2.0));
        painter.setBrush(QColor(30, 30, 30, 80));
        painter.drawEllipse(center, radius, radius);
        painter.setPen(QPen(QColor(20, 20, 20, 220), 1.0, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(center, radius, radius);
    } else if (m_tool == Tool::Pen) {
        // A fine cross and a coloured centre point stay recognisable on both
        // light and dark screenshots.
        QPen outline(QColor(0, 0, 0, 190), 3.0, Qt::SolidLine,
                     Qt::SquareCap, Qt::MiterJoin);
        painter.setPen(outline);
        painter.drawLine(QPointF(3, center.y()), QPointF(canvasSize - 3, center.y()));
        painter.drawLine(QPointF(center.x(), 3), QPointF(center.x(), canvasSize - 3));
        QPen cross(m_penColor, 1.0, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
        painter.setPen(cross);
        painter.drawLine(QPointF(3, center.y()), QPointF(canvasSize - 3, center.y()));
        painter.drawLine(QPointF(center.x(), 3), QPointF(center.x(), canvasSize - 3));
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_penColor);
        painter.drawEllipse(center, qMax<qreal>(2.0, visibleWidth / 2.0),
                            qMax<qreal>(2.0, visibleWidth / 2.0));
    } else if (m_tool == Tool::Marker) {
        // The marker is a wide, red, opaque footprint.  Keep it visibly
        // different from the pencil's fine cross and from the effect tools.
        const QColor markerColor(235, 55, 55, 235);
        const qreal radius = qMax<qreal>(0.5, visibleWidth / 2.0);
        painter.setPen(QPen(QColor(255, 255, 255, 230), 1.0));
        painter.setBrush(markerColor);
        painter.drawEllipse(center, radius, radius);
        painter.setPen(QPen(QColor(125, 20, 20, 220), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(center, radius, radius);
    } else if ((m_tool == Tool::Mosaic || m_tool == Tool::Blur)
               && m_effectShape != EffectShape::Brush) {
        // Rectangle/ellipse effects are drag-to-select modes.  Use the same
        // crosshair as the other geometric annotation tools instead of a
        // brush footprint cursor, which suggests painting before the drag.
        painter.end();
        setCursor(Qt::CrossCursor);
        return;
    } else if (m_tool == Tool::Mosaic) {
        // Mosaic uses a neutral grey, blocky footprint.  The small checker
        // marks make it distinguishable from blur even at a glance.
        const qreal radius = qMax<qreal>(0.5, visibleWidth / 2.0);
        painter.save();
        QPainterPath clip;
        clip.addEllipse(center, radius, radius);
        painter.setClipPath(clip);
        painter.fillRect(QRectF(center.x() - radius, center.y() - radius,
                                radius * 2.0, radius * 2.0), QColor(145, 145, 145, 235));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(95, 95, 95, 210));
        const qreal cell = qMax<qreal>(3.0, radius / 2.4);
        for (qreal y = center.y() - radius; y < center.y() + radius; y += cell) {
            for (qreal x = center.x() - radius; x < center.x() + radius; x += cell) {
                if ((qRound((x - center.x()) / cell) + qRound((y - center.y()) / cell)) % 2 == 0)
                    painter.drawRect(QRectF(x, y, cell, cell));
            }
        }
        painter.restore();
        painter.setPen(QPen(QColor(45, 45, 45, 210), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(center, radius, radius);
    } else {
        // Blur keeps a grey footprint but has a soft inner halo instead of
        // the mosaic checker pattern.
        const qreal radius = qMax<qreal>(0.5, visibleWidth / 2.0);
        painter.setPen(QPen(QColor(255, 255, 255, 210), 1.0));
        painter.setBrush(QColor(155, 155, 155, 185));
        painter.drawEllipse(center, radius, radius);
        painter.setPen(QPen(QColor(75, 75, 75, 180), 1.0, Qt::DashLine));
        painter.setBrush(QColor(205, 205, 205, 90));
        painter.drawEllipse(center, radius * qBound<qreal>(0.35,
                                                            1.0 - m_effectSize / 60.0,
                                                            0.75),
                            radius * qBound<qreal>(0.35,
                                                   1.0 - m_effectSize / 60.0,
                                                   0.75));
    }
    painter.end();
    setCursor(QCursor(pixmap, canvasSize / 2, canvasSize / 2));
}

void AnnotationCanvas::drawShape(QPainter &painter, const Shape &shape, qreal scale) const
{
    if (!qFuzzyIsNull(shape.rotation)) {
        Shape unrotated = shape;
        unrotated.rotation = 0.0;
        const QPointF center = shapeBounds(unrotated).center();
        painter.save();
        painter.translate(center);
        painter.rotate(shape.rotation);
        painter.translate(-center);
        drawShape(painter, unrotated, scale);
        painter.restore();
        return;
    }
    QColor color = shape.color;
    if (shape.tool == Tool::Marker)
        color.setAlpha(190);
    QPen pen(color);
    pen.setWidthF(qMax<qreal>(1.0, shape.width * scale));
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    pen.setStyle(shape.penStyle);
    painter.setPen(pen);

    if (shape.tool == Tool::Mosaic || shape.tool == Tool::Blur) {
        const bool mosaic = shape.tool == Tool::Mosaic;
        pen.setStyle(mosaic ? Qt::DashLine : Qt::DotLine);
        pen.setColor(mosaic ? QColor(90, 90, 90, 220) : QColor(120, 120, 120, 190));
        // This is only the selection hint.  Its thickness must not follow
        // the mosaic brush width; doing so paints a wide grey halo around a
        // correctly sized effect and makes the result look oversized.
        pen.setWidthF(qMax<qreal>(1.0, scale));
        painter.setPen(pen);
        // The effect itself has already been rendered underneath.  Keep this
        // layer as a border/selection hint only; filling the shape here makes
        // a rectangle or ellipse look like a large grey overlay and hides the
        // live mosaic preview.
        painter.setBrush(Qt::NoBrush);
        if (shape.effectShape == EffectShape::Rectangle) {
            painter.drawRect(QRectF(shape.start, shape.end).normalized());
        } else if (shape.effectShape == EffectShape::Ellipse) {
            painter.drawEllipse(QRectF(shape.start, shape.end).normalized());
        } else {
            // Brush width controls the footprint. Effect size controls only
            // the pixel block/blur kernel, so the controls stay independent.
            const qreal radius = qMax<qreal>(2.0, shape.width * 0.5 * scale);
            const QVector<QPointF> points = shape.points.isEmpty()
                ? QVector<QPointF>{shape.start, shape.end} : shape.points;
            QPainterPath centerline;
            if (!points.isEmpty()) {
                centerline.moveTo(points.first());
                for (qsizetype index = 1; index < points.size(); ++index)
                    centerline.lineTo(points.at(index));
            }
            QPainterPathStroker stroker;
            stroker.setWidth(radius * 2.0);
            stroker.setCapStyle(Qt::RoundCap);
            stroker.setJoinStyle(Qt::RoundJoin);
            const QPainterPath footprint = centerline.isEmpty()
                ? QPainterPath() : stroker.createStroke(centerline);
            if (!footprint.isEmpty())
                painter.drawPath(footprint);
            else if (!points.isEmpty())
                painter.drawEllipse(points.first(), radius, radius);
        }
        return;
    }

    if (shape.tool == Tool::Eraser)
        return;

    if (shape.tool == Tool::Rectangle) {
        if (shape.filled)
            painter.setBrush(color);
        painter.drawRect(QRectF(shape.start, shape.end).normalized());
        return;
    }
    if (shape.tool == Tool::RoundedRectangle) {
        if (shape.filled)
            painter.setBrush(color);
        const qreal r = shape.cornerRadius > 0 ? shape.cornerRadius * scale : 12.0 * scale;
        painter.drawRoundedRect(QRectF(shape.start, shape.end).normalized(), r, r);
        return;
    }
    if (shape.tool == Tool::Magnifier) {
        const QRectF area = magnifierTargetRect(shape);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(pen);
        if (shape.effectShape == EffectShape::Ellipse) {
            painter.drawEllipse(area);
        } else if (shape.cornerRadius > 0.0) {
            const qreal r = shape.cornerRadius * scale;
            painter.drawRoundedRect(area, r, r);
        } else {
            painter.drawRect(area);
        }
        if (shape.magnifierCutout && !shape.sourceRect.isEmpty()) {
            const QRectF source = shape.sourceRect.normalized();
            QPen sourcePen = pen;
            // The source frame becomes a white dashed frame while it overlaps
            // the target.  When detached, keep it solid and stop the
            // connector exactly at the target border.
            const bool overlaps = source.intersects(area);
            sourcePen.setStyle(overlaps ? Qt::DashLine : Qt::SolidLine);
            sourcePen.setColor(Qt::white);
            sourcePen.setWidthF(qMax<qreal>(1.0, scale));
            painter.setPen(sourcePen);
            if (shape.effectShape == EffectShape::Ellipse) {
                painter.drawEllipse(source);
            } else if (shape.cornerRadius > 0.0) {
                const qreal r = shape.cornerRadius * scale;
                painter.drawRoundedRect(source, r, r);
            } else {
                painter.drawRect(source);
            }
            if (!overlaps && shape.magnifierConnectorStyle != 2) {
                QPen connectorPen = sourcePen;
                connectorPen.setStyle(Qt::SolidLine);
                connectorPen.setWidthF(qMax<qreal>(1.0, shape.width * scale * 0.85));
                painter.setPen(connectorPen);
                const QPointF sourceCenter = source.center();
                const QPointF targetCenter = area.center();
                const QPointF delta = targetCenter - sourceCenter;
                const qreal distance = std::hypot(delta.x(), delta.y());
                if (distance > 0.001) {
                    const QPointF unit = delta / distance;
                    const auto boundaryAlong = [](const QRectF &rect,
                                                  const QPointF &center,
                                                  const QPointF &direction) {
                        qreal t = std::numeric_limits<qreal>::max();
                        if (!qFuzzyIsNull(direction.x())) {
                            const qreal x = direction.x() > 0.0 ? rect.right() : rect.left();
                            t = qMin(t, (x - center.x()) / direction.x());
                        }
                        if (!qFuzzyIsNull(direction.y())) {
                            const qreal y = direction.y() > 0.0 ? rect.bottom() : rect.top();
                            t = qMin(t, (y - center.y()) / direction.y());
                        }
                        return t > 0.0 && std::isfinite(t) ? center + direction * t : center;
                    };
                    QPointF from = boundaryAlong(source, sourceCenter, unit);
                    QPointF to = boundaryAlong(area, targetCenter, -unit);
                    // Inset both endpoints by half the stroke width so
                    // antialiasing cannot paint into either rectangle.
                    const qreal inset = connectorPen.widthF() * 0.5 + 0.5;
                    from += unit * inset;
                    to -= unit * inset;
                    if (QLineF(from, to).length() > 0.001) {
                        if (shape.magnifierConnectorStyle == 1) {
                            const QPointF mid(from.x(), to.y());
                            const QVector<QPointF> pts{from, mid, to};
                            painter.drawPolyline(pts.constData(), pts.size());
                        } else {
                            painter.drawLine(from, to);
                        }
                    }
                }
            }
        }
        return;
    }
    if (shape.tool == Tool::Ellipse) {
        if (shape.filled)
            painter.setBrush(color);
        painter.drawEllipse(QRectF(shape.start, shape.end).normalized());
        return;
    }
    if (shape.tool == Tool::Line) {
        painter.drawLine(shape.start, shape.end);
        return;
    }
    if (shape.tool == Tool::Pen || shape.tool == Tool::Marker) {
        if (shape.points.size() > 1)
            painter.drawPolyline(shape.points.constData(), shape.points.size());
        else if (shape.points.size() == 1) {
            const qreal radius = qMax<qreal>(0.5, shape.width * scale / 2.0);
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            painter.drawEllipse(shape.points.constFirst(), radius, radius);
        }
        return;
    }
    if (shape.tool == Tool::Arrow) {
        const QPointF delta = shape.end - shape.start;
        const qreal length = std::hypot(delta.x(), delta.y());
        painter.drawLine(shape.start, shape.end);
        if (length > 1.0) {
            const QPointF unit = delta / length;
            const QPointF normal(-unit.y(), unit.x());
            const qreal head = 14.0 * scale;
            const auto drawHead = [&](const QPointF &tip, const QPointF &direction, bool hollow) {
                const QPointF side(-direction.y(), direction.x());
                const QPointF left = tip - direction * head + side * head * 0.45;
                const QPointF right = tip - direction * head - side * head * 0.45;
                if (hollow) {
                    painter.setBrush(Qt::NoBrush);
                    painter.drawPolygon(QPolygonF{tip, left, right});
                } else {
                    painter.setBrush(color);
                    painter.drawPolygon(QPolygonF{tip, left, right});
                    painter.setBrush(Qt::NoBrush);
                }
            };
            const bool startHead = shape.arrowStyle == ArrowStyle::Start
                || shape.arrowStyle == ArrowStyle::Both
                || shape.arrowStyle == ArrowStyle::HollowBoth
                || shape.arrowStyle == ArrowStyle::HollowStart;
            const bool endHead = shape.arrowStyle == ArrowStyle::End
                || shape.arrowStyle == ArrowStyle::Both
                || shape.arrowStyle == ArrowStyle::Flat
                || shape.arrowStyle == ArrowStyle::HollowEnd
                || shape.arrowStyle == ArrowStyle::HollowBoth;
            const bool hollowStart = shape.arrowStyle == ArrowStyle::HollowStart
                || shape.arrowStyle == ArrowStyle::HollowBoth;
            const bool hollowEnd = shape.arrowStyle == ArrowStyle::HollowEnd
                || shape.arrowStyle == ArrowStyle::HollowBoth;
            if (startHead)
                drawHead(shape.start, -unit, hollowStart);
            if (endHead)
                drawHead(shape.end, unit, hollowEnd);
        }
        return;
    }
    QFont font = painter.font();
    font.setFamily(shape.fontFamily.isEmpty() ? QStringLiteral("Microsoft YaHei UI")
                                             : shape.fontFamily);
    font.setPixelSize(qMax(6, qRound(shape.fontSize * scale)));
    font.setBold(shape.textBold);
    font.setItalic(shape.textItalic);
    painter.setFont(font);
    if (shape.tool == Tool::Number) {
        font.setPixelSize(qMax(8, qRound(shape.numberSize * 0.52 * scale)));
        font.setBold(true);
        painter.setFont(font);
        const qreal diameter = shape.numberSize * scale;
        const QRectF badge(shape.start.x() - diameter / 2.0,
                           shape.start.y() - diameter / 2.0,
                           diameter, diameter);
        if (shape.numberStyle == 0) {
            painter.setBrush(shape.color);
            painter.setPen(Qt::NoPen);
            painter.drawEllipse(badge);
            painter.setPen(Qt::white);
        } else if (shape.numberStyle == 1) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(shape.color, qMax<qreal>(1.5, 1.5 * scale)));
            painter.drawEllipse(badge);
            painter.drawEllipse(badge.adjusted(2.5 * scale, 2.5 * scale, -2.5 * scale, -2.5 * scale));
            painter.setPen(shape.color);
        } else {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(shape.color, qMax<qreal>(1.5, 1.5 * scale)));
            painter.drawEllipse(badge);
            painter.setPen(shape.color);
        }
        painter.drawText(badge, Qt::AlignCenter, shape.text);
        return;
    }
    const QFontMetricsF metrics(font);
    const QStringList lines = shape.text.split(QChar::LineFeed);
    for (qsizetype index = 0; index < lines.size(); ++index) {
        const QPointF top = shape.start + QPointF(0, index * metrics.lineSpacing());
        if (shape.textBackground) {
            painter.fillRect(QRectF(top, QSizeF(metrics.horizontalAdvance(lines.at(index)) + 4,
                                               metrics.lineSpacing())),
                             QColor(color.red(), color.green(), color.blue(), 80));
        }
        const QPointF baseline = top + QPointF(0, metrics.ascent());
        if (shape.textOutlineWidth > 0.0 && shape.textOutlineColor.alpha() > 0) {
            QPainterPath textPath;
            textPath.addText(baseline, font, lines.at(index));
            painter.fillPath(textPath, color);
            QPen outline(shape.textOutlineColor);
            outline.setWidthF(shape.textOutlineWidth * scale);
            outline.setJoinStyle(Qt::RoundJoin);
            painter.strokePath(textPath, outline);
        } else {
            painter.drawText(baseline, lines.at(index));
        }
    }
}

QPainterPath AnnotationCanvas::effectMask(const Shape &shape) const
{
    QPainterPath mask;
    mask.setFillRule(Qt::WindingFill);
    if (shape.effectShape == EffectShape::Rectangle) {
        mask.addRect(QRectF(shape.start, shape.end).normalized());
        return mask;
    }
    if (shape.effectShape == EffectShape::Ellipse) {
        mask.addEllipse(QRectF(shape.start, shape.end).normalized());
        return mask;
    }

    const QVector<QPointF> points = shape.points.isEmpty()
        ? QVector<QPointF>{shape.start, shape.end} : shape.points;
    const qreal radius = qMax<qreal>(0.5, shape.width * 0.5);
    if (points.isEmpty())
        return mask;

    // Use one stroked path for the freehand mode.  A union of independent
    // circles leaves visible gaps whenever Windows delivers sparse mouse
    // move events, and those gaps make the mosaic look like a chain of large
    // grey dots.  A round-capped stroke keeps the footprint equal to the
    // configured brush width and remains continuous between events.
    QPainterPath stroke;
    stroke.moveTo(points.first());
    bool hasSegment = false;
    for (qsizetype index = 1; index < points.size(); ++index) {
        const QPointF point = points.at(index);
        if (QLineF(stroke.currentPosition(), point).length() <= 0.001)
            continue;
        stroke.lineTo(point);
        hasSegment = true;
    }
    if (!hasSegment) {
        mask.addEllipse(QRectF(points.first() - QPointF(radius, radius),
                               QSizeF(radius * 2.0, radius * 2.0)));
        return mask;
    }

    QPainterPathStroker stroker;
    stroker.setWidth(radius * 2.0);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    return stroker.createStroke(stroke);
}

void AnnotationCanvas::drawEffect(QPainter &painter,
                                  const QImage &sourceFrame,
                                  const Shape &shape) const
{
    if ((shape.tool != Tool::Mosaic && shape.tool != Tool::Blur)
        || sourceFrame.isNull())
        return;

    const QPainterPath mask = effectMask(shape);
    const QRect effectRect = mask.boundingRect().toAlignedRect()
        .intersected(sourceFrame.rect());
    if (effectRect.isEmpty())
        return;

    // Both effects use the same selected pixels and the same shape mask.  The
    // only difference is the filter applied to the cropped source image.
    const QImage source = sourceFrame.copy(effectRect);
    const int blockSize = qBound(2, shape.effectSize, 40);
    const QSize reducedSize(qMax(1, qCeil(qreal(effectRect.width()) / blockSize)),
                            qMax(1, qCeil(qreal(effectRect.height()) / blockSize)));

    QImage filtered;
    if (shape.tool == Tool::Mosaic) {
        // Pixelate on a stable grid.  Each cell receives the mean source
        // colour, which gives a predictable block instead of a post-rendered
        // blur and works identically for brush, rectangle, and ellipse masks.
        filtered = QImage(effectRect.size(), QImage::Format_ARGB32_Premultiplied);
        filtered.fill(Qt::transparent);
        QPainter mosaicPainter(&filtered);
        mosaicPainter.setRenderHint(QPainter::Antialiasing, false);

        // Build summed-area tables once.  A block average then needs only
        // four table reads per channel instead of walking every source pixel
        // for every block, which keeps click/drag rendering responsive.
        const int sourceWidth = source.width();
        const int sourceHeight = source.height();
        const int stride = sourceWidth + 1;
        const qsizetype integralSize = qsizetype(stride) * (sourceHeight + 1);
        QVector<quint64> sumRed(integralSize, 0);
        QVector<quint64> sumGreen(integralSize, 0);
        QVector<quint64> sumBlue(integralSize, 0);
        QVector<quint64> sumAlpha(integralSize, 0);
        for (int y = 0; y < sourceHeight; ++y) {
            quint64 rowRed = 0;
            quint64 rowGreen = 0;
            quint64 rowBlue = 0;
            quint64 rowAlpha = 0;
            const auto *scanLine = reinterpret_cast<const QRgb *>(source.constScanLine(y));
            for (int x = 0; x < sourceWidth; ++x) {
                const QRgb pixel = scanLine[x];
                rowRed += qRed(pixel);
                rowGreen += qGreen(pixel);
                rowBlue += qBlue(pixel);
                rowAlpha += qAlpha(pixel);
                const qsizetype index = qsizetype(y + 1) * stride + x + 1;
                const qsizetype previousRow = qsizetype(y) * stride + x + 1;
                sumRed[index] = sumRed[previousRow] + rowRed;
                sumGreen[index] = sumGreen[previousRow] + rowGreen;
                sumBlue[index] = sumBlue[previousRow] + rowBlue;
                sumAlpha[index] = sumAlpha[previousRow] + rowAlpha;
            }
        }
        const auto blockSum = [stride](const QVector<quint64> &table,
                                       int left, int top, int right, int bottom) {
            const qsizetype br = qsizetype(bottom) * stride + right;
            const qsizetype tr = qsizetype(top) * stride + right;
            const qsizetype bl = qsizetype(bottom) * stride + left;
            const qsizetype tl = qsizetype(top) * stride + left;
            return table[br] - table[tr] - table[bl] + table[tl];
        };
        for (int y = 0; y < source.height(); y += blockSize) {
            for (int x = 0; x < source.width(); x += blockSize) {
                const int right = qMin(x + blockSize, sourceWidth);
                const int bottom = qMin(y + blockSize, sourceHeight);
                const quint64 count = quint64(right - x) * quint64(bottom - y);
                const int red = int(blockSum(sumRed, x, y, right, bottom) / count);
                const int green = int(blockSum(sumGreen, x, y, right, bottom) / count);
                const int blue = int(blockSum(sumBlue, x, y, right, bottom) / count);
                const int alpha = int(blockSum(sumAlpha, x, y, right, bottom) / count);
                const QRect cell(x, y, right - x, bottom - y);
                mosaicPainter.fillRect(cell,
                                       QColor(red, green, blue, alpha));
            }
        }
        mosaicPainter.end();
    } else {
        const QImage reduced = source.scaled(reducedSize,
                                             Qt::IgnoreAspectRatio,
                                             Qt::SmoothTransformation);
        filtered = reduced.scaled(effectRect.size(),
                                  Qt::IgnoreAspectRatio,
                                  Qt::SmoothTransformation);
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setClipPath(mask);
    painter.drawImage(effectRect.topLeft(), filtered);
    painter.restore();
}

void AnnotationCanvas::paintEvent(QPaintEvent *)
{
    QImage display = fullRenderedImage(false);
    if (m_dragging && (m_tool == Tool::Mosaic || m_tool == Tool::Blur)) {
        // Effects are rasterized on every move so the screenshot shows the
        // same result the user will commit on release.
        QPainter effectPainter(&display);
        const Shape preview = currentShape();
        drawEffect(effectPainter, display, preview);
        effectPainter.end();
    }

    QPainter painter(this);
    if (!testAttribute(Qt::WA_TranslucentBackground)) {
        painter.fillRect(rect(), QColor(30, 30, 30));
    }
    const QRectF frame = imageRect();
    const QRectF localFrame = frame.translated(-QPointF(m_coordinateOrigin));
    painter.drawImage(localFrame, display);
    const qreal scale = frame.width() / qMax(1, m_image.width());
    painter.save();
    painter.translate(localFrame.topLeft());
    painter.scale(scale, scale);
    if (m_dragging) {
        Shape preview = currentShape();
        // During the first magnifier gesture the UI is a plain rectangular
        // selection of the source pixels.  The larger target frame and
        // enlarged pixels appear after the drag is committed.
        if (m_tool == Tool::Magnifier) {
            const QRectF source = preview.sourceRect.normalized();
            preview.start = source.topLeft();
            preview.end = source.bottomRight();
            preview.sourceRect = {};
        }
        drawShape(painter, preview, 1.0);
    }
    if (m_selectedShape >= 0 && m_selectedShape < m_shapes.size()) {
        const Shape &sel = m_shapes.at(m_selectedShape);
        QRectF bounds = shapeBounds(sel);
        painter.save();
        if (!qFuzzyIsNull(sel.rotation)) {
            painter.translate(bounds.center());
            painter.rotate(sel.rotation);
            painter.translate(-bounds.center());
        }
        QPen dashedPen(QColor(255, 255, 255, 240), 1.0, Qt::DashLine);
        painter.setPen(dashedPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(bounds);

        // 8 square resize handles
        const qreal hs = 6.0;
        const QList<QPointF> handles = {
            bounds.topLeft(),
            QPointF(bounds.center().x(), bounds.top()),
            bounds.topRight(),
            QPointF(bounds.right(), bounds.center().y()),
            bounds.bottomRight(),
            QPointF(bounds.center().x(), bounds.bottom()),
            bounds.bottomLeft(),
            QPointF(bounds.left(), bounds.center().y())
        };
        painter.setPen(QPen(QColor(40, 137, 237, 230), 1.0));
        painter.setBrush(Qt::white);
        for (const QPointF &pt : handles) {
            painter.drawRect(QRectF(pt.x() - hs / 2.0, pt.y() - hs / 2.0, hs, hs));
        }

        // Top rotation handle
        const QPointF topCenter(bounds.center().x(), bounds.top());
        const QPointF rotHandleCenter(bounds.center().x(), bounds.top() - 20.0);
        painter.setPen(QPen(QColor(40, 137, 237, 220), 1.5));
        painter.drawLine(topCenter, rotHandleCenter);
        painter.setPen(QPen(QColor(40, 137, 237), 1.5));
        painter.setBrush(Qt::white);
        painter.drawEllipse(rotHandleCenter, 7.0, 7.0);
        painter.setPen(QPen(QColor(40, 137, 237), 1.5));
        painter.drawArc(QRectF(rotHandleCenter.x() - 4.5, rotHandleCenter.y() - 4.5, 9.0, 9.0), 45 * 16, 270 * 16);
        painter.drawLine(rotHandleCenter + QPointF(2, -4), rotHandleCenter + QPointF(4, -2));
        painter.restore();
    }
    painter.restore();

    // The parent overlay dims everything outside the active selection.  The
    // canvas itself is kept at the selection geometry for compatibility with
    // resize hit testing, so only paint the part of the full frame that falls
    // inside this child widget.
}

void AnnotationCanvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton) {
        if (m_textEditing)
            finishTextInput();
        if (m_editingShape) {
            m_editingShape = false;
            m_editingMagnifierPart = MagnifierPart::None;
            emit historyChanged();
        }
        if (m_dragging) {
            m_dragging = false;
            m_currentPoints.clear();
        }
        setFocus();
        event->accept();
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;
    finishTextInput();
    setFocus();
    const QPointF imagePoint = toImagePoint(event->position());
    if (m_selectedShape >= 0 && m_selectedShape < m_shapes.size()) {
        const Shape &sel = m_shapes.at(m_selectedShape);
        QRectF bounds = shapeBounds(sel);
        const QPointF rotHandleCenter(bounds.center().x(), bounds.top() - 20.0);
        if (QLineF(imagePoint, rotHandleCenter).length() <= 10.0) {
            m_editingRotation = true;
            m_start = imagePoint;
            event->accept();
            return;
        }
        const QList<QPointF> handles = {
            bounds.topLeft(),
            QPointF(bounds.center().x(), bounds.top()),
            bounds.topRight(),
            QPointF(bounds.right(), bounds.center().y()),
            bounds.bottomRight(),
            QPointF(bounds.center().x(), bounds.bottom()),
            bounds.bottomLeft(),
            QPointF(bounds.left(), bounds.center().y())
        };
        for (int i = 0; i < handles.size(); ++i) {
            if (QLineF(imagePoint, handles[i]).length() <= 7.0) {
                m_resizingHandle = i;
                m_start = imagePoint;
                event->accept();
                return;
            }
        }
    }
    // Freehand tools must always start a new stroke, even over an existing
    // stroke or effect.  Their wide bounds are not selection handles.
    const int hit = isNonMovableBrush(m_tool) ? -1 : shapeAt(imagePoint);
    if (hit >= 0 && event->modifiers().testFlag(Qt::ControlModifier)
        && m_tool != Tool::Eraser && m_tool != Tool::Text && m_tool != Tool::Number) {
        // Ctrl-drag duplicates an existing annotation.  The small initial
        // offset makes the duplicate visible even when the pointer does not
        // move before release.
        Shape duplicate = m_shapes.at(hit);
        const Tool duplicatedTool = duplicate.tool;
        translateShape(duplicate, QPointF(12.0, 12.0));
        m_shapes.append(std::move(duplicate));
        invalidateEraserHistory();
        m_selectedShape = m_shapes.size() - 1;
        m_editingShape = true;
        m_editingMagnifierPart = duplicatedTool == Tool::Magnifier
            ? MagnifierPart::Target : MagnifierPart::None;
        m_start = imagePoint;
        m_current = imagePoint;
        emit historyChanged();
        update();
        event->accept();
        return;
    }
    if (hit >= 0 && (event->modifiers().testFlag(Qt::ControlModifier)
                     || m_shapes.at(hit).tool == m_tool)
        && m_tool != Tool::Eraser && m_tool != Tool::Text && m_tool != Tool::Number) {
        m_selectedShape = hit;
        m_editingShape = true;
        m_editingMagnifierPart = MagnifierPart::None;
        if (m_shapes.at(hit).tool == Tool::Magnifier && m_tool == Tool::Magnifier) {
            const Shape &magnifier = m_shapes.at(hit);
            const QRectF target = magnifierTargetRect(magnifier);
            const QRectF source = magnifier.sourceRect.normalized();
            const bool inSource = !source.isEmpty() && source.contains(imagePoint);
            const bool inTarget = target.contains(imagePoint);
            // The source frame is smaller and sits inside the target by
            // default.  Clicking in that small frame selects the sampling
            // range; clicking the surrounding part of the target moves the
            // large display frame.
            if (inSource)
                m_editingMagnifierPart = MagnifierPart::Source;
            else if (inTarget)
                m_editingMagnifierPart = MagnifierPart::Target;
        }
        m_start = imagePoint;
        m_current = imagePoint;
        update();
        event->accept();
        return;
    }
    m_selectedShape = -1;
    m_editingShape = false;
    m_editingMagnifierPart = MagnifierPart::None;
    m_start = imagePoint;
    m_current = m_start;
    const bool effectBrush = (m_tool == Tool::Mosaic || m_tool == Tool::Blur)
        && m_effectShape == EffectShape::Brush;
    m_currentPoints = effectBrush || m_tool == Tool::Pen || m_tool == Tool::Marker
        || m_tool == Tool::Eraser ? QVector<QPointF>{m_start} : QVector<QPointF>{};
    m_dragging = true;
    if (m_tool == Tool::Text) {
        m_dragging = false;
        beginTextInput(event->position());
        event->accept();
        return;
    }
    if (m_tool == Tool::Number) {
        m_dragging = false;
        invalidateEraserHistory();
        Shape shape = currentShape();
        shape.text = QString::number(m_nextNumber++);
        m_shapes.append(shape);
        if (m_autoSelectOnCreation)
            m_selectedShape = m_shapes.size() - 1;
        m_redoShapes.clear();
        emit historyChanged();
        updateToolCursor();
        update();
    }
    if (m_tool == Tool::Eraser) {
        m_eraserUndoSnapshot = m_shapes;
        m_eraserRedoSnapshot.clear();
        m_eraserUndoAvailable = true;
        m_eraserRedoAvailable = false;
        m_lastOperationWasEraser = true;
        applyEraserSegment(imagePoint, imagePoint);
        return;
    }
}

void AnnotationCanvas::beginTextInput(const QPointF &position)
{
    m_textShape = currentShape();
    m_textShape.start = toImagePoint(position);
    m_textEditor->clear();
    m_textEditor->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { color: %1; background: rgba(255,255,255,12); "
        "border: 1px solid rgba(40,137,237,150); border-radius: 2px; "
        "padding: 2px; selection-color: white; "
        "selection-background-color: rgba(40,137,237,190); }").arg(m_textShape.color.name()));
    m_textEditing = true;
    updateTextEditorGeometry();
    m_textEditor->show();
    m_textEditor->raise();
    m_textEditor->setFocus(Qt::MouseFocusReason);
}

void AnnotationCanvas::updateTextEditorGeometry()
{
    if (!m_textEditing || m_image.isNull())
        return;
    const QRectF target = imageRect();
    const qreal scale = target.width() / m_image.width();
    QFont textFont = font();
    textFont.setFamily(QStringLiteral("Microsoft YaHei UI"));
    textFont.setPixelSize(qMax(1, qRound(20 * scale)));
    textFont.setBold(false);
    if (m_textEditor->font() != textFont)
        m_textEditor->setFont(textFont);
    const QFontMetrics metrics(textFont);
    // Snipaste starts text editing with a one-line, one-character box and
    // grows it as the user types.  A fixed 80 px box makes an empty editor
    // look like a large selection rectangle and hides the underlying image.
    int contentWidth = qMax(18, metrics.horizontalAdvance(QStringLiteral("M")) + 8);
    const QStringList lines = m_textEditor->toPlainText().split(QChar::LineFeed);
    for (const QString &line : lines)
        contentWidth = qMax(contentWidth, metrics.horizontalAdvance(line) + 8);
    const QRectF localTarget = target.translated(-QPointF(m_coordinateOrigin));
    const QRect area = localTarget.toAlignedRect().intersected(rect());
    if (area.isEmpty())
        return;
    const QPoint anchor = (localTarget.topLeft() + m_textShape.start * scale).toPoint();
    const int editorWidth = qMin(contentWidth, area.width());
    const int editorHeight = qMin(qMax(metrics.lineSpacing() + 6,
                                       int(lines.size()) * metrics.lineSpacing() + 6),
                                  area.height());
    m_textEditor->setGeometry(qBound(area.left(), anchor.x(), area.right() - editorWidth + 1),
                             qBound(area.top(), anchor.y(), area.bottom() - editorHeight + 1),
                             editorWidth, editorHeight);
    m_textShape.start = toImagePoint(m_textEditor->pos());
}

void AnnotationCanvas::finishTextInput(bool commit)
{
    if (!m_textEditing)
        return;
    m_textEditing = false; // Guard the focus-out event caused by hiding the editor.
    if (commit)
        QGuiApplication::inputMethod()->commit();
    else
        QGuiApplication::inputMethod()->reset();
    m_textShape.text = m_textEditor->toPlainText();
    m_textEditor->hide();
    if (commit && !m_textShape.text.isEmpty()) {
        invalidateEraserHistory();
        m_shapes.append(m_textShape);
        if (m_autoSelectOnCreation)
            m_selectedShape = m_shapes.size() - 1;
        m_redoShapes.clear();
        emit historyChanged();
    }
    m_textEditor->clear();
    update();
}

bool AnnotationCanvas::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_textEditor && m_textEditing) {
        if (event->type() == QEvent::KeyPress) {
            auto *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_Escape
                || ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
                    && key->modifiers().testFlag(Qt::ControlModifier))) {
                finishTextInput(key->key() != Qt::Key_Escape);
                setFocus();
                event->accept();
                return true;
            }
        } else if (event->type() == QEvent::FocusOut
                   && static_cast<QFocusEvent *>(event)->reason() != Qt::PopupFocusReason) {
            finishTextInput();
        }
    }
    return QWidget::eventFilter(watched, event);
}

void AnnotationCanvas::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateToolCursor();
    updateTextEditorGeometry();
}

void AnnotationCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (m_editingRotation && m_selectedShape >= 0 && m_selectedShape < m_shapes.size()) {
        const QPointF point = toImagePoint(event->position());
        const QRectF bounds = shapeBounds(m_shapes.at(m_selectedShape));
        const QPointF center = bounds.center();
        const qreal angle1 = std::atan2(m_start.y() - center.y(), m_start.x() - center.x()) * 180.0 / 3.14159265358979323846;
        const qreal angle2 = std::atan2(point.y() - center.y(), point.x() - center.x()) * 180.0 / 3.14159265358979323846;
        rotateSelected(angle2 - angle1);
        m_start = point;
        emit historyChanged();
        update();
        event->accept();
        return;
    }
    if (m_resizingHandle >= 0 && m_selectedShape >= 0 && m_selectedShape < m_shapes.size()) {
        const QPointF point = toImagePoint(event->position());
        const QPointF delta = point - m_start;
        Shape &shape = m_shapes[m_selectedShape];
        if (m_resizingHandle == 0 || m_resizingHandle == 6 || m_resizingHandle == 7)
            shape.start.rx() += delta.x();
        if (m_resizingHandle == 0 || m_resizingHandle == 1 || m_resizingHandle == 2)
            shape.start.ry() += delta.y();
        if (m_resizingHandle == 2 || m_resizingHandle == 3 || m_resizingHandle == 4)
            shape.end.rx() += delta.x();
        if (m_resizingHandle == 4 || m_resizingHandle == 5 || m_resizingHandle == 6)
            shape.end.ry() += delta.y();
        m_start = point;
        emit historyChanged();
        update();
        event->accept();
        return;
    }
    if (m_editingShape && m_selectedShape >= 0 && m_selectedShape < m_shapes.size()) {
        const QPointF point = toImagePoint(event->position());
        const QPointF delta = point - m_start;
        if (!delta.isNull()) {
            if (m_shapes.at(m_selectedShape).tool == Tool::Magnifier)
                translateMagnifierPart(m_shapes[m_selectedShape],
                                        m_editingMagnifierPart == MagnifierPart::None
                                            ? MagnifierPart::Target : m_editingMagnifierPart,
                                        delta);
            else
                translateShape(m_shapes[m_selectedShape], delta);
            m_start = point;
            emit historyChanged();
            update();
        }
        event->accept();
        return;
    }
    if (!m_dragging)
        return;
    const QPointF raw = toImagePoint(event->position());
    if (m_tool == Tool::Eraser) {
        const QPointF previous = m_currentPoints.isEmpty() ? m_current : m_currentPoints.last();
        m_current = raw;
        m_currentPoints.append(raw);
        applyEraserSegment(previous, raw);
        update();
        event->accept();
        return;
    }
    m_current = raw;
    if (event->modifiers().testFlag(Qt::ShiftModifier)) {
        if (m_tool == Tool::Rectangle || m_tool == Tool::RoundedRectangle
            || m_tool == Tool::Ellipse
            || ((m_tool == Tool::Mosaic || m_tool == Tool::Blur)
                && m_effectShape != EffectShape::Brush)) {
            const qreal side = qMin(std::abs(raw.x() - m_start.x()),
                                    std::abs(raw.y() - m_start.y()));
            m_current = QPointF(m_start.x() + (raw.x() < m_start.x() ? -side : side),
                                m_start.y() + (raw.y() < m_start.y() ? -side : side));
        } else if (m_tool == Tool::Line || m_tool == Tool::Arrow
                    || m_tool == Tool::Pen || m_tool == Tool::Marker
                    || ((m_tool == Tool::Mosaic || m_tool == Tool::Blur)
                        && m_effectShape == EffectShape::Brush)) {
            const QPointF delta = raw - m_start;
            const qreal length = std::hypot(delta.x(), delta.y());
            if (length > 0.0) {
                constexpr qreal pi = 3.14159265358979323846;
                const qreal angle = std::atan2(delta.y(), delta.x());
                const qreal snapped = std::round(angle / (pi / 4.0)) * (pi / 4.0);
                m_current = m_start + QPointF(std::cos(snapped) * length,
                                              std::sin(snapped) * length);
            }
        }
    }
    if (m_tool == Tool::Pen || m_tool == Tool::Marker
        || ((m_tool == Tool::Mosaic || m_tool == Tool::Blur)
            && m_effectShape == EffectShape::Brush))
        if (event->modifiers().testFlag(Qt::ShiftModifier))
            m_currentPoints = {m_start, m_current};
        else
            m_currentPoints.append(m_current);
    update();
}

void AnnotationCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if ((m_editingShape || m_editingRotation || m_resizingHandle >= 0) && event->button() == Qt::LeftButton) {
        m_editingShape = false;
        m_editingRotation = false;
        m_resizingHandle = -1;
        m_editingMagnifierPart = MagnifierPart::None;
        emit historyChanged();
        update();
        event->accept();
        return;
    }
    if (!m_dragging || event->button() != Qt::LeftButton)
        return;
    const QPointF raw = toImagePoint(event->position());
    m_current = raw;
    if (event->modifiers().testFlag(Qt::ShiftModifier)) {
        if (m_tool == Tool::Rectangle || m_tool == Tool::RoundedRectangle
            || m_tool == Tool::Ellipse
            || ((m_tool == Tool::Mosaic || m_tool == Tool::Blur)
                && m_effectShape != EffectShape::Brush)) {
            const qreal side = qMin(std::abs(raw.x() - m_start.x()),
                                    std::abs(raw.y() - m_start.y()));
            m_current = QPointF(m_start.x() + (raw.x() < m_start.x() ? -side : side),
                                m_start.y() + (raw.y() < m_start.y() ? -side : side));
        } else if (m_tool == Tool::Line || m_tool == Tool::Arrow
                    || m_tool == Tool::Pen || m_tool == Tool::Marker
                    || ((m_tool == Tool::Mosaic || m_tool == Tool::Blur)
                        && m_effectShape == EffectShape::Brush)) {
            const QPointF delta = raw - m_start;
            const qreal length = std::hypot(delta.x(), delta.y());
            if (length > 0.0) {
                constexpr qreal pi = 3.14159265358979323846;
                const qreal angle = std::atan2(delta.y(), delta.x());
                const qreal snapped = std::round(angle / (pi / 4.0)) * (pi / 4.0);
                m_current = m_start + QPointF(std::cos(snapped) * length,
                                              std::sin(snapped) * length);
            }
        }
    }
    const int shapeCountBefore = m_shapes.size();
    if (m_tool == Tool::Eraser) {
        const QPointF previous = m_currentPoints.isEmpty() ? m_current : m_currentPoints.last();
        applyEraserSegment(previous, m_current);
        m_dragging = false;
        m_currentPoints.clear();
        emit historyChanged();
        update();
        event->accept();
        return;
    }
    invalidateEraserHistory();
    const bool effectBrush = (m_tool == Tool::Mosaic || m_tool == Tool::Blur)
        && m_effectShape == EffectShape::Brush;
    if (effectBrush)
        m_currentPoints.append(m_current);
    if (m_tool == Tool::Pen || m_tool == Tool::Marker
        || effectBrush) {
        // A click is a valid brush stamp in the same way it is in Photoshop;
        // it must leave a dot even when the pointer did not move.
        if (!m_currentPoints.isEmpty())
            m_shapes.append(currentShape());
    } else if (QLineF(m_start, m_current).length() > 2.0) {
        m_shapes.append(currentShape());
    }
    if (m_autoSelectOnCreation && !isNonMovableBrush(m_tool)
        && m_shapes.size() > shapeCountBefore)
        m_selectedShape = m_shapes.size() - 1;
    m_redoShapes.clear();
    m_dragging = false;
    m_currentPoints.clear();
    emit historyChanged();
    update();
}

void AnnotationCanvas::wheelEvent(QWheelEvent *event)
{
    int delta = event->angleDelta().y() != 0
        ? event->angleDelta().y() : event->angleDelta().x();
    if (delta == 0)
        delta = event->pixelDelta().y() != 0 ? event->pixelDelta().y()
                                             : event->pixelDelta().x();
    if (delta == 0) {
        event->ignore();
        return;
    }
    const int steps = qMax(1, qAbs(delta) / 120);
    const int sign = delta > 0 ? 1 : -1;
    if (m_tool == Tool::Number) {
        setNumberSize(m_numberSize + sign * steps * 2);
    } else if (m_tool == Tool::Text) {
        setTextFontSize(m_textFontSize + sign * steps * 2);
    } else {
        setPenWidth(m_penWidth + sign * steps);
    }
    event->accept();
}

void AnnotationCanvas::keyPressEvent(QKeyEvent *event)
{
    if (m_selectedShape >= 0 && event->modifiers().testFlag(Qt::AltModifier)) {
        if (event->key() == Qt::Key_Left) {
            rotateSelected(-15.0);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Right) {
            rotateSelected(15.0);
            event->accept();
            return;
        }
    }
    if (event->modifiers() == Qt::NoModifier
        && (event->key() == Qt::Key_BracketLeft || event->key() == Qt::Key_BracketRight)) {
        setPenWidth(m_penWidth + (event->key() == Qt::Key_BracketRight ? 1 : -1));
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && m_selectedShape >= 0) {
        m_selectedShape = -1;
        m_editingShape = false;
        update();
        return;
    }
    if (event->modifiers().testFlag(Qt::ControlModifier)
        && event->modifiers().testFlag(Qt::ShiftModifier)
        && event->key() == Qt::Key_C) {
        copySelectedShape();
        event->accept();
        return;
    }
    if (event->modifiers().testFlag(Qt::ControlModifier)
        && event->modifiers().testFlag(Qt::ShiftModifier)
        && event->key() == Qt::Key_V) {
        pasteShape();
        event->accept();
        return;
    }
    if (event->modifiers().testFlag(Qt::ControlModifier)
        && event->modifiers().testFlag(Qt::ShiftModifier)
        && event->key() == Qt::Key_Z) {
        clearAnnotations();
        event->accept();
        return;
    }
    if (event->modifiers().testFlag(Qt::ControlModifier)
        && event->key() == Qt::Key_Z) {
        undo();
        return;
    }
    if (event->modifiers().testFlag(Qt::ControlModifier)
        && (event->key() == Qt::Key_Y || event->key() == Qt::Key_R)) {
        redo();
        return;
    }
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        if (m_selectedShape >= 0 && m_selectedShape < m_shapes.size()) {
            invalidateEraserHistory();
            m_shapes.removeAt(m_selectedShape);
            m_selectedShape = -1;
            m_redoShapes.clear();
            emit historyChanged();
            update();
        } else {
            clearAnnotations();
        }
        return;
    }
    QWidget::keyPressEvent(event);
}

void AnnotationCanvas::copySelectedShape()
{
    if (m_selectedShape < 0 || m_selectedShape >= m_shapes.size())
        return;
    m_shapeClipboard = {m_shapes.at(m_selectedShape)};
}

void AnnotationCanvas::pasteShape()
{
    if (m_shapeClipboard.isEmpty())
        return;
    Shape pasted = m_shapeClipboard.constLast();
    invalidateEraserHistory();
    translateShape(pasted, QPointF(12.0, 12.0));
    m_shapes.append(std::move(pasted));
    m_selectedShape = m_shapes.size() - 1;
    m_redoShapes.clear();
    emit historyChanged();
    update();
}

QImage AnnotationCanvas::fullRenderedImage(bool includePendingText) const
{
    QImage result = m_image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (const Shape &shape : m_shapes) {
        if (shape.tool == Tool::Magnifier) {
            const QRectF target = magnifierTargetRect(shape);
            if (target.width() < 2.0 || target.height() < 2.0)
                continue;
            QRectF source = shape.sourceRect;
            if (source.isEmpty()) {
                source = QRectF(target.center().x() - target.width() / 4.0,
                                target.center().y() - target.height() / 4.0,
                                target.width() / 2.0,
                                target.height() / 2.0);
            }
            const QRectF unclippedSource = source;
            source = source.intersected(QRectF(result.rect()));
            if (source.isEmpty())
                continue;
            const QImage sourceFrame = result;
            painter.save();
            if (shape.effectShape == EffectShape::Ellipse) {
                QPainterPath clip;
                clip.addEllipse(target);
                painter.setClipPath(clip);
            } else if (shape.cornerRadius > 0.0) {
                QPainterPath clip;
                clip.addRoundedRect(target, shape.cornerRadius, shape.cornerRadius);
                painter.setClipPath(clip);
            } else {
                painter.setClipRect(target);
            }
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            // Map the source centre onto the target centre.  Drawing the
            // clipped source directly through this transform avoids the
            // top-left anchoring and stretching that occurred when a source
            // rectangle touched the image boundary.
            QTransform zoomTransform;
            zoomTransform.translate(target.center().x(), target.center().y());
            zoomTransform.scale(target.width() / qMax<qreal>(1.0, unclippedSource.width()),
                                target.height() / qMax<qreal>(1.0, unclippedSource.height()));
            zoomTransform.translate(-unclippedSource.center().x(), -unclippedSource.center().y());
            painter.setWorldTransform(zoomTransform, true);
            painter.drawImage(QPointF(0, 0), sourceFrame);
            painter.restore();
            Shape outline = shape;
            outline.sourceRect = source;
            drawShape(painter, outline, 1.0);
            continue;
        }
        if (shape.tool == Tool::Mosaic || shape.tool == Tool::Blur) {
            const QImage sourceFrame = result;
            drawEffect(painter, sourceFrame, shape);
            continue;
        }
        drawShape(painter, shape, 1.0);
    }
    if (includePendingText && m_textEditing) {
        Shape pending = m_textShape;
        pending.text = m_textEditor->toPlainText();
        drawShape(painter, pending, 1.0);
    }
    return result;
}

QImage AnnotationCanvas::renderedImage(bool includePendingText) const
{
    const QImage full = fullRenderedImage(includePendingText);
    const QRect crop = outputPixelRect();
    return crop.isEmpty() ? full : full.copy(crop);
}

void AnnotationCanvas::undo()
{
    finishTextInput();
    if (m_lastOperationWasEraser && m_eraserUndoAvailable) {
        m_eraserRedoSnapshot = m_shapes;
        m_shapes = m_eraserUndoSnapshot;
        m_eraserRedoAvailable = true;
        m_eraserUndoAvailable = false;
        m_lastOperationWasEraser = false;
        emit historyChanged();
        update();
        return;
    }
    if (m_shapes.isEmpty())
        return;
    m_redoShapes.append(m_shapes.takeLast());
    emit historyChanged();
    update();
}

void AnnotationCanvas::redo()
{
    finishTextInput();
    if (m_eraserRedoAvailable) {
        m_eraserUndoSnapshot = m_shapes;
        m_shapes = m_eraserRedoSnapshot;
        m_eraserUndoAvailable = true;
        m_eraserRedoAvailable = false;
        m_lastOperationWasEraser = true;
        emit historyChanged();
        update();
        return;
    }
    if (m_redoShapes.isEmpty())
        return;
    m_shapes.append(m_redoShapes.takeLast());
    emit historyChanged();
    update();
}

void AnnotationCanvas::clearAnnotations()
{
    finishTextInput(false);
    if (m_shapes.isEmpty())
        return;
    invalidateEraserHistory();
    m_redoShapes += m_shapes;
    m_shapes.clear();
    emit historyChanged();
    update();
}

AnnotationDialog::AnnotationDialog(const QImage &image, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("编辑截图"));
    resize(1000, 720);
    m_canvas = new AnnotationCanvas(image, this);

    auto *rectangle = new QToolButton(this);
    rectangle->setText(QStringLiteral("矩形"));
    auto *ellipse = new QToolButton(this);
    ellipse->setText(QStringLiteral("椭圆"));
    auto *roundedRectangle = new QToolButton(this);
    roundedRectangle->setText(QStringLiteral("圆角矩形"));
    auto *line = new QToolButton(this);
    line->setText(QStringLiteral("直线"));
    auto *arrow = new QToolButton(this);
    arrow->setText(QStringLiteral("箭头"));
    auto *pen = new QToolButton(this);
    pen->setText(QStringLiteral("画笔"));
    auto *text = new QToolButton(this);
    text->setText(QStringLiteral("文字"));
    auto *number = new QToolButton(this);
    number->setText(QStringLiteral("序号"));
    auto *magnifier = new QToolButton(this);
    magnifier->setText(QStringLiteral("放大镜标注"));
    auto *mosaic = new QToolButton(this);
    mosaic->setText(QStringLiteral("马赛克"));
    auto *blur = new QToolButton(this);
    blur->setText(QStringLiteral("模糊"));
    auto *eraser = new QToolButton(this);
    eraser->setText(QStringLiteral("橡皮擦"));
    auto *color = new QPushButton(QStringLiteral("颜色"), this);
    auto *width = new QSpinBox(this);
    width->setRange(1, 32);
    width->setValue(3);
    width->setSuffix(QStringLiteral(" px"));
    auto *lineStyle = new QComboBox(this);
    lineStyle->addItems({QStringLiteral("实线"), QStringLiteral("虚线"), QStringLiteral("点线")});
    auto *textBackground = new QCheckBox(QStringLiteral("文字底色"), this);
    auto *undo = new QPushButton(QStringLiteral("撤销"), this);
    auto *redo = new QPushButton(QStringLiteral("重做"), this);
    auto *clear = new QPushButton(QStringLiteral("清空标注"), this);
    auto *copy = new QPushButton(QStringLiteral("复制"), this);
    auto *save = new QPushButton(QStringLiteral("保存…"), this);

    // The professional build keeps the compact toolbar.  Less frequently
    // used arrow and text-outline variants are available from the tool's
    // context menu, matching Snipaste's secondary tool settings without
    // widening the toolbar.
    arrow->setContextMenuPolicy(Qt::CustomContextMenu);
    text->setContextMenuPolicy(Qt::CustomContextMenu);

    auto *toolbar = new QHBoxLayout;
    toolbar->addWidget(rectangle);
    toolbar->addWidget(ellipse);
    toolbar->addWidget(roundedRectangle);
    toolbar->addWidget(line);
    toolbar->addWidget(arrow);
    toolbar->addWidget(pen);
    toolbar->addWidget(text);
    toolbar->addWidget(number);
    toolbar->addWidget(magnifier);
    toolbar->addWidget(mosaic);
    toolbar->addWidget(blur);
    toolbar->addWidget(eraser);
    toolbar->addWidget(color);
    toolbar->addWidget(width);
    toolbar->addWidget(lineStyle);
    toolbar->addWidget(textBackground);
    toolbar->addSpacing(12);
    toolbar->addWidget(undo);
    toolbar->addWidget(redo);
    toolbar->addWidget(clear);
    toolbar->addStretch();
    toolbar->addWidget(copy);
    toolbar->addWidget(save);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("完成"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(toolbar);
    layout->addWidget(m_canvas, 1);
    layout->addWidget(buttons);

    connect(rectangle, &QToolButton::clicked, this, [this] {
        m_canvas->setTool(AnnotationCanvas::Tool::Rectangle);
    });
    connect(ellipse, &QToolButton::clicked, this, [this] {
        m_canvas->setTool(AnnotationCanvas::Tool::Ellipse);
    });
    connect(roundedRectangle, &QToolButton::clicked, this, [this] {
        m_canvas->setTool(AnnotationCanvas::Tool::RoundedRectangle);
    });
    connect(line, &QToolButton::clicked, this, [this] {
        m_canvas->setTool(AnnotationCanvas::Tool::Line);
    });
    connect(arrow, &QToolButton::clicked, this, [this] {
        m_canvas->setTool(AnnotationCanvas::Tool::Arrow);
    });
    connect(arrow, &QToolButton::customContextMenuRequested, this, [this, arrow](const QPoint &position) {
        QMenu menu;
        QActionGroup group(&menu);
        group.setExclusive(true);
        const auto addStyle = [&](const QString &label, AnnotationCanvas::ArrowStyle style) {
            auto *action = menu.addAction(label);
            action->setCheckable(true);
            action->setChecked(m_canvas->arrowStyle() == style);
            action->setData(static_cast<int>(style));
            group.addAction(action);
        };
        addStyle(QStringLiteral("末端箭头"), AnnotationCanvas::ArrowStyle::End);
        addStyle(QStringLiteral("起点箭头"), AnnotationCanvas::ArrowStyle::Start);
        addStyle(QStringLiteral("双向箭头"), AnnotationCanvas::ArrowStyle::Both);
        addStyle(QStringLiteral("空心末端"), AnnotationCanvas::ArrowStyle::HollowEnd);
        addStyle(QStringLiteral("空心双向"), AnnotationCanvas::ArrowStyle::HollowBoth);
        if (QAction *chosen = menu.exec(arrow->mapToGlobal(position)))
            m_canvas->setArrowStyle(static_cast<AnnotationCanvas::ArrowStyle>(chosen->data().toInt()));
    });
    connect(pen, &QToolButton::clicked, this, [this] {
        m_canvas->setTool(AnnotationCanvas::Tool::Pen);
    });
    connect(text, &QToolButton::clicked, this, [this] {
        m_canvas->setTool(AnnotationCanvas::Tool::Text);
    });
    connect(text, &QToolButton::customContextMenuRequested, this, [this, text](const QPoint &position) {
        QMenu menu;
        QAction *disable = menu.addAction(QStringLiteral("无描边"));
        QAction *colorAction = menu.addAction(QStringLiteral("描边颜色…"));
        QAction *widthAction = menu.addAction(QStringLiteral("描边宽度…"));
        QAction *chosen = menu.exec(text->mapToGlobal(position));
        if (chosen == disable) {
            m_canvas->setTextOutlineWidth(0.0);
        } else if (chosen == colorAction) {
            QColor color = QColorDialog::getColor(m_canvas->textOutlineColor(), this,
                                                  QStringLiteral("文字描边颜色"),
                                                  QColorDialog::ShowAlphaChannel);
            if (color.isValid()) {
                m_canvas->setTextOutlineColor(color);
                if (m_canvas->textOutlineWidth() <= 0.0)
                    m_canvas->setTextOutlineWidth(1.0);
            }
        } else if (chosen == widthAction) {
            bool ok = false;
            const double width = QInputDialog::getDouble(this, QStringLiteral("文字描边宽度"),
                                                         QStringLiteral("宽度 (px):"),
                                                         m_canvas->textOutlineWidth(), 0.0, 16.0,
                                                         1, &ok);
            if (ok)
                m_canvas->setTextOutlineWidth(width);
        }
    });
    connect(number, &QToolButton::clicked, this, [this] {
        m_canvas->setTool(AnnotationCanvas::Tool::Number);
    });
    connect(magnifier, &QToolButton::clicked, this, [this] {
        m_canvas->setTool(AnnotationCanvas::Tool::Magnifier);
    });
    connect(mosaic, &QToolButton::clicked, this, [this] {
        m_canvas->setTool(AnnotationCanvas::Tool::Mosaic);
    });
    connect(blur, &QToolButton::clicked, this, [this] {
        m_canvas->setTool(AnnotationCanvas::Tool::Blur);
    });
    connect(eraser, &QToolButton::clicked, this, [this] {
        m_canvas->setTool(AnnotationCanvas::Tool::Eraser);
    });
    connect(color, &QPushButton::clicked, this, [this, color] {
        const QColor selected = QColorDialog::getColor(Qt::red, this, QStringLiteral("选择标注颜色"));
        if (selected.isValid()) {
            m_canvas->setPenColor(selected);
            color->setStyleSheet(QStringLiteral("background-color: %1;").arg(selected.name()));
        }
    });
    connect(width, &QSpinBox::valueChanged, this, [this](int value) {
        m_canvas->setPenWidth(value);
    });
    connect(lineStyle, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        m_canvas->setPenStyle(index == 1 ? Qt::DashLine : index == 2 ? Qt::DotLine : Qt::SolidLine);
    });
    connect(textBackground, &QCheckBox::toggled, m_canvas, &AnnotationCanvas::setTextBackground);
    connect(undo, &QPushButton::clicked, m_canvas, &AnnotationCanvas::undo);
    connect(redo, &QPushButton::clicked, m_canvas, &AnnotationCanvas::redo);
    connect(clear, &QPushButton::clicked, m_canvas, &AnnotationCanvas::clearAnnotations);
    connect(copy, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setImage(resultImage());
    });
    connect(save, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getSaveFileName(
            this,
            QStringLiteral("保存标注截图"),
            QStringLiteral("snipaste-annotated.png"),
            QStringLiteral("PNG 图片 (*.png);;JPEG 图片 (*.jpg *.jpeg)"));
        if (!path.isEmpty())
            resultImage().save(path);
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QImage AnnotationDialog::resultImage() const
{
    m_canvas->finishTextInput();
    return m_canvas->renderedImage();
}

} // namespace snipaste::ui
