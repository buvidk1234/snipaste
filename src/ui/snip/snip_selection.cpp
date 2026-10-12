#include "ui/snip/snip_overlay.h"
#include "ui/annotation/annotation_dialog.h"

#include <QCursor>
#include <QFrame>
#include <QMouseEvent>
#include <QRegion>

namespace snipate::ui {

Qt::Edges SnipOverlay::resizeEdgesAt(QPoint position) const
{
    constexpr int hitWidth = 6;
    if (!m_selectionConfirmed || !m_selection.adjusted(-hitWidth, -hitWidth, hitWidth, hitWidth).contains(position))
        return {};
    Qt::Edges edges;
    if (qAbs(position.x() - m_selection.left()) <= hitWidth)
        edges |= Qt::LeftEdge;
    else if (qAbs(position.x() - m_selection.right()) <= hitWidth)
        edges |= Qt::RightEdge;
    if (qAbs(position.y() - m_selection.top()) <= hitWidth)
        edges |= Qt::TopEdge;
    else if (qAbs(position.y() - m_selection.bottom()) <= hitWidth)
        edges |= Qt::BottomEdge;
    return edges;
}

void SnipOverlay::updateSelectionCursor(QPoint position)
{
    // Once a stroke has started, the brush owns the pointer even when the
    // stroke happens to cross the selection border.  Otherwise the resize
    // hit-test briefly replaces the brush footprint with a resize arrow.
    const bool canvasActive = m_canvas
        && !m_canvas->testAttribute(Qt::WA_TransparentForMouseEvents);
    const auto brushTool = canvasActive && (m_canvas->tool() == AnnotationCanvas::Tool::Pen
                                            || m_canvas->tool() == AnnotationCanvas::Tool::Marker
                                            || m_canvas->tool() == AnnotationCanvas::Tool::Mosaic
                                            || m_canvas->tool() == AnnotationCanvas::Tool::Blur
                                            || m_canvas->tool() == AnnotationCanvas::Tool::Eraser);
    const bool canvasDrawing = canvasActive && m_canvas->isDrawing();
    const Qt::Edges edges = m_transformingSelection
        ? m_resizeEdges
        : ((brushTool || canvasDrawing) ? Qt::Edges() : resizeEdgesAt(position));
    // Once an annotation tool owns the canvas, let AnnotationCanvas display
    // its tool-specific brush cursor.  Selection resize cursors still win at
    // the border, where the canvas is temporarily forwarded to the overlay.
    if (canvasActive
        && edges == Qt::Edges() && !m_transformingSelection) {
        // The child canvas owns the mouse events while an annotation tool is
        // active, but Windows may still use the parent overlay cursor when
        // the pointer crosses a masked child boundary.  Keep both cursors in
        // sync so brush tools cannot fall back to a stale resize arrow.
        // Rebuild the footprint on hover as well as on tool changes.  Windows
        // can deliver the first hover event before the child cursor change
        // reaches the native window, leaving the previous resize cursor on
        // screen until another input event (for example the mouse wheel).
        m_canvas->refreshToolCursor();
        const QCursor toolCursor = m_canvas->cursor();
        setCursor(toolCursor);
        return;
    }
    Qt::CursorShape cursor = Qt::CrossCursor;
    if (edges == (Qt::TopEdge | Qt::LeftEdge) || edges == (Qt::BottomEdge | Qt::RightEdge))
        cursor = Qt::SizeFDiagCursor;
    else if (edges == (Qt::TopEdge | Qt::RightEdge) || edges == (Qt::BottomEdge | Qt::LeftEdge))
        cursor = Qt::SizeBDiagCursor;
    else if (edges.testFlag(Qt::LeftEdge) || edges.testFlag(Qt::RightEdge))
        cursor = Qt::SizeHorCursor;
    else if (edges.testFlag(Qt::TopEdge) || edges.testFlag(Qt::BottomEdge))
        cursor = Qt::SizeVerCursor;
    else if (m_movingSelection || (m_selectionConfirmed && m_selection.contains(position)
             && m_canvas && m_canvas->testAttribute(Qt::WA_TransparentForMouseEvents)))
        cursor = Qt::SizeAllCursor;
    setCursor(cursor);
    if (m_canvas)
        m_canvas->setCursor(cursor);
}

void SnipOverlay::refreshAnnotationCursor()
{
    if (!m_canvas || m_canvas->testAttribute(Qt::WA_TransparentForMouseEvents))
        return;
    m_canvas->refreshToolCursor();
    updateSelectionCursor(mapFromGlobal(QCursor::pos()));
}

bool SnipOverlay::beginSelectionTransform(QPoint position)
{
    m_resizeEdges = resizeEdgesAt(position);
    m_movingSelection = m_resizeEdges == Qt::Edges() && m_selection.contains(position)
        && m_canvas && m_canvas->testAttribute(Qt::WA_TransparentForMouseEvents);
    if (m_resizeEdges == Qt::Edges() && !m_movingSelection)
        return false;
    m_transformingSelection = true;
    m_transformOrigin = position;
    m_transformOriginSelection = m_selection;
    if (m_toolbar) m_toolbar->hide();
    if (m_sizeToolbar) m_sizeToolbar->hide();
    updateSelectionCursor(position);
    return true;
}

void SnipOverlay::changeSelection(QRect selection, bool moveAnnotations)
{
    selection = selection.intersected(rect());
    if (selection.width() < 2 || selection.height() < 2 || selection == m_selection)
        return;
    m_selectionImageOverride = {};
    m_selection = selection;
    if (m_canvas) {
        Q_UNUSED(moveAnnotations);
        // Annotation coordinates live in the full overlay image.  Resizing or
        // moving the selection only changes the output viewport; it must not
        // scale or translate existing strokes.
        m_canvas->setCanvasFrame(m_selection.topLeft(), QSizeF(size()), m_selection);
        m_canvas->setGeometry(m_selection);
    }
    update();
}

void SnipOverlay::updateSelectionTransform(QPoint position)
{
    const QPoint delta = position - m_transformOrigin;
    QRect next = m_transformOriginSelection;
    if (m_movingSelection) {
        next.translate(delta);
        next.moveLeft(qBound(0, next.left(), width() - next.width()));
        next.moveTop(qBound(0, next.top(), height() - next.height()));
    } else {
        if (m_resizeEdges.testFlag(Qt::LeftEdge))
            next.setLeft(qBound(0, next.left() + delta.x(), next.right() - 1));
        if (m_resizeEdges.testFlag(Qt::RightEdge))
            next.setRight(qBound(next.left() + 1, next.right() + delta.x(), width() - 1));
        if (m_resizeEdges.testFlag(Qt::TopEdge))
            next.setTop(qBound(0, next.top() + delta.y(), next.bottom() - 1));
        if (m_resizeEdges.testFlag(Qt::BottomEdge))
            next.setBottom(qBound(next.top() + 1, next.bottom() + delta.y(), height() - 1));
    }
    changeSelection(next, m_movingSelection);
}

bool SnipOverlay::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Wheel) {
        auto *w = qobject_cast<QWidget *>(watched);
        if (w && (w == m_toolbar || w == m_toolOptions || (m_toolbar && m_toolbar->isAncestorOf(w)))) {
            if (m_canvas && !m_canvas->testAttribute(Qt::WA_TransparentForMouseEvents)) {
                auto *we = static_cast<QWheelEvent *>(event);
                int delta = we->angleDelta().y() != 0 ? we->angleDelta().y() : we->angleDelta().x();
                if (delta == 0)
                    delta = we->pixelDelta().y() != 0 ? we->pixelDelta().y() : we->pixelDelta().x();
                if (delta != 0) {
                    const int steps = qMax(1, qAbs(delta) / 120);
                    const int sign = delta > 0 ? 1 : -1;
                    m_canvas->setPenWidth(m_canvas->penWidth() + sign * steps);
                    return true;
                }
            }
        }
    }
    if (watched == m_canvas && (event->type() == QEvent::MouseMove
        || event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease)) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        const QPoint position = m_canvas->mapTo(this, mouse->position().toPoint());
        const bool canvasActive = m_canvas
            && !m_canvas->testAttribute(Qt::WA_TransparentForMouseEvents);
        const bool brushTool = canvasActive
            && (m_canvas->tool() == AnnotationCanvas::Tool::Pen
                || m_canvas->tool() == AnnotationCanvas::Tool::Marker
                || m_canvas->tool() == AnnotationCanvas::Tool::Mosaic
                || m_canvas->tool() == AnnotationCanvas::Tool::Blur
                || m_canvas->tool() == AnnotationCanvas::Tool::Eraser);
        const bool overResizeEdge = resizeEdgesAt(position) != Qt::Edges();
        if (m_transformingSelection || (!brushTool && !m_canvas->isDrawing() && overResizeEdge)) {
            QMouseEvent forwarded(mouse->type(), QPointF(position), mouse->globalPosition(),
                                   mouse->button(), mouse->buttons(), mouse->modifiers());
            if (mouse->type() == QEvent::MouseButtonPress) mousePressEvent(&forwarded);
            else if (mouse->type() == QEvent::MouseButtonRelease) mouseReleaseEvent(&forwarded);
            else mouseMoveEvent(&forwarded);
            return true;
        }
        // Keep the parent and child cursors synchronized even while a brush
        // stroke is in progress.  During a stroke the old selection-resize
        // cursor could otherwise remain visible until the next hover event,
        // which made marker, mosaic and blur look like horizontal arrows.
        if (mouse->type() == QEvent::MouseMove)
            updateSelectionCursor(position);
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace snipate::ui
