#include "ui/clipboard_output.h"
#include "ui/paste/paste_manager.h"
#include "ui/paste/paste_window.h"

#include "platform/keyboard.h"
#include "ui/annotation/annotation_canvas.h"
#include "ui/annotation/annotation_options.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QGraphicsView>
#include <QFrame>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QScreen>
#include <QStandardPaths>
#include <QToolButton>
#include <QWheelEvent>

namespace snipaste::ui
{
QString PasteWindow::mouseAction(int index, const QString &fallback) const
{
    if (index >= 0 && index < m_mouseActions.size() && !m_mouseActions.at(index).trimmed().isEmpty())
        return m_mouseActions.at(index).trimmed();
    return fallback;
}
bool PasteWindow::mouseModifierMatches(const QString &value, Qt::KeyboardModifiers modifiers) const
{
    const QString expected = value.trimmed();
    const Qt::KeyboardModifiers actual =
        modifiers & (Qt::ControlModifier | Qt::MetaModifier | Qt::ShiftModifier | Qt::AltModifier);
    if (expected.isEmpty() || expected == QStringLiteral("无"))
        return actual == Qt::NoModifier;
    if (expected == QStringLiteral("Ctrl"))
        return actual == Qt::ControlModifier;
    if (expected == QStringLiteral("Command") || expected == QStringLiteral("Cmd"))
        return actual == snipaste::platform::primaryModifier();
    if (expected == QStringLiteral("Shift"))
        return actual == Qt::ShiftModifier;
    if (expected == QStringLiteral("Alt"))
        return actual == Qt::AltModifier;
    return false;
}
bool PasteWindow::mouseButtonMatches(const QString &value, Qt::MouseButton button, bool doubleClick) const
{
    const QString expected = value.trimmed();
    if (doubleClick)
        return (button == Qt::LeftButton && expected == QStringLiteral("左键双击"));
    if (button == Qt::MiddleButton)
        return expected == QStringLiteral("中键单击");
    if (button == Qt::RightButton)
        return expected == QStringLiteral("右键单击") || expected == QStringLiteral("鼠标右键拖拽");
    return false;
}
void PasteWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton)
        m_suppressContextMenu = false;
    if (mouseModifierMatches(mouseAction(10, QStringLiteral("无")), event->modifiers()) &&
        mouseButtonMatches(mouseAction(11, QStringLiteral("中键单击")), event->button(), false))
    {
        // The default middle-click action resets a pin's transform and
        // opacity while preserving its position and background mode.
        if (!m_resetTransform)
        {
            emit activated(this, event->modifiers());
            event->accept();
            return;
        }
        m_rotationQuarterTurns = 0;
        m_flipHorizontal = false;
        m_flipVertical = false;
        m_grayscale = false;
        m_inverted = false;
        m_thumbnailMode = false;
        m_scale = 1.0;
        rebuildImage();
        setWindowOpacity(1.0);
        emit activated(this, event->modifiers());
        event->accept();
        return;
    }
    if (mouseModifierMatches(mouseAction(14, QStringLiteral("无")), event->modifiers()) &&
        mouseAction(15, QStringLiteral("鼠标右键拖拽")) == QStringLiteral("鼠标右键拖拽") &&
        event->button() == Qt::RightButton)
    {
        m_selectionThumbnailCandidate = true;
        m_selectionThumbnailDragging = false;
        m_selectionThumbnailMoved = false;
        m_selectionThumbnailOrigin = event->position().toPoint();
        m_selectionThumbnailCurrent = m_selectionThumbnailOrigin;
        updateEditorGeometry();
        update();
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton)
    {
        emit activated(this, event->modifiers());
        m_pressed = true;
        m_dragging = false;
        m_pressPosition = event->globalPosition().toPoint();
        m_resizeEdges = m_resizeEnabled ? resizeEdgesAt(event->position().toPoint()) : Qt::Edges{};
        // An empty edge set means a normal drag.  The previous boolean
        // conversion inverted that meaning, so clicks in the middle of a
        // paste window were treated as resize gestures and never moved it.
        m_resizing = m_resizeEdges != Qt::Edges{};
        m_resizeStartGeometry = frameGeometry();
        m_resizeStartGlobal = m_pressPosition;
        m_dragOffset = m_pressPosition - frameGeometry().topLeft();
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}
void PasteWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (m_selectionThumbnailCandidate && event->buttons().testFlag(Qt::RightButton))
    {
        m_selectionThumbnailCurrent = event->position().toPoint();
        if ((m_selectionThumbnailCurrent - m_selectionThumbnailOrigin).manhattanLength() >=
            QApplication::startDragDistance())
        {
            m_selectionThumbnailDragging = true;
            m_selectionThumbnailMoved = true;
            m_suppressContextMenu = true;
        }
        if (m_selectionThumbnailDragging)
        {
            update();
            event->accept();
            return;
        }
    }
    if (!m_pressed)
    {
        updateResizeCursor(event->position().toPoint());
        QWidget::mouseMoveEvent(event);
        return;
    }
    if (m_resizing && event->buttons().testFlag(Qt::LeftButton))
    {
        resizeFromPointer(event->globalPosition().toPoint());
        event->accept();
        return;
    }
    if (m_pressed && event->buttons().testFlag(Qt::LeftButton))
    {
        const QPoint globalPosition = event->globalPosition().toPoint();
        if (!m_dragging &&
            (globalPosition - m_pressPosition).manhattanLength() >= QApplication::startDragDistance())
        {
            m_dragging = true;
        }
        if (m_dragging)
            move(globalPosition - m_dragOffset);
        event->accept();
        return;
    }

    QWidget::mouseMoveEvent(event);
}
void PasteWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (mouseModifierMatches(mouseAction(12, QStringLiteral("Shift")), event->modifiers()) &&
        mouseButtonMatches(mouseAction(13, QStringLiteral("左键双击")), event->button(), true))
    {
        toggleThumbnailMode();
        event->accept();
        return;
    }
    if (mouseModifierMatches(mouseAction(8, QStringLiteral("无")), event->modifiers()) &&
        mouseButtonMatches(mouseAction(9, QStringLiteral("左键双击")), event->button(), true))
    {
        hidePaste();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}
void PasteWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton && m_selectionThumbnailCandidate)
    {
        const bool dragged = m_selectionThumbnailDragging && m_selectionThumbnailMoved;
        const QRect selection = QRect(m_selectionThumbnailOrigin, m_selectionThumbnailCurrent).normalized();
        m_selectionThumbnailCandidate = false;
        m_selectionThumbnailDragging = false;
        m_selectionThumbnailMoved = false;
        m_suppressContextMenu = dragged;
        if (dragged)
        {
            applySelectionThumbnail(selection);
            update();
            event->accept();
            return;
        }
        update();
        updateEditorGeometry();
        // A right click without movement remains a normal context-menu click.
    }
    if (event->button() == Qt::LeftButton && m_pressed)
    {
        const bool wasDragging = m_dragging;
        m_pressed = false;
        m_dragging = false;
        m_resizing = false;
        m_resizeEdges = {};
        setCursor(Qt::ArrowCursor);
        QScreen *screen = QGuiApplication::screenAt(frameGeometry().center());
        // A click near the screen edge must not move the window between the
        // first and second clicks of a double-click. Snap only after a drag.
        if (wasDragging && screen)
        {
            const QRect area = screen->availableGeometry();
            QPoint snapped = pos();
            constexpr int threshold = 12;
            if (qAbs(snapped.x() - area.left()) <= threshold)
                snapped.setX(area.left());
            if (qAbs(snapped.y() - area.top()) <= threshold)
                snapped.setY(area.top());
            if (qAbs(frameGeometry().right() - area.right()) <= threshold)
                snapped.setX(area.right() - width() + 1);
            if (qAbs(frameGeometry().bottom() - area.bottom()) <= threshold)
                snapped.setY(area.bottom() - height() + 1);
            move(snapped);
        }
        if (wasDragging)
            emit positionChanged(this);
    }
    QWidget::mouseReleaseEvent(event);
}
Qt::Edges PasteWindow::resizeEdgesAt(const QPoint &position) const
{
    if (!m_resizeEnabled || m_image.isNull())
        return {};
    constexpr int margin = 7;
    Qt::Edges edges;
    if (position.x() <= margin)
        edges |= Qt::LeftEdge;
    else if (position.x() >= width() - margin - 1)
        edges |= Qt::RightEdge;
    if (position.y() <= margin)
        edges |= Qt::TopEdge;
    else if (position.y() >= height() - margin - 1)
        edges |= Qt::BottomEdge;
    return edges;
}
void PasteWindow::updateResizeCursor(const QPoint &position)
{
    const Qt::Edges edges = resizeEdgesAt(position);
    if ((edges & (Qt::LeftEdge | Qt::RightEdge)) && (edges & (Qt::TopEdge | Qt::BottomEdge)))
    {
        const bool nwse = (edges.testFlag(Qt::LeftEdge) == edges.testFlag(Qt::TopEdge));
        setCursor(nwse ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor);
    }
    else if (edges & (Qt::LeftEdge | Qt::RightEdge))
    {
        setCursor(Qt::SizeHorCursor);
    }
    else if (edges & (Qt::TopEdge | Qt::BottomEdge))
    {
        setCursor(Qt::SizeVerCursor);
    }
    else
    {
        setCursor(Qt::ArrowCursor);
    }
}
void PasteWindow::resizeFromPointer(const QPoint &globalPosition)
{
    if (m_resizeStartGeometry.isEmpty() || m_image.isNull())
        return;
    const QPoint delta = globalPosition - m_resizeStartGlobal;
    QRect geometry = m_resizeStartGeometry;
    if (m_resizeEdges.testFlag(Qt::LeftEdge))
        geometry.setLeft(geometry.left() + delta.x());
    if (m_resizeEdges.testFlag(Qt::RightEdge))
        geometry.setRight(geometry.right() + delta.x());
    if (m_resizeEdges.testFlag(Qt::TopEdge))
        geometry.setTop(geometry.top() + delta.y());
    if (m_resizeEdges.testFlag(Qt::BottomEdge))
        geometry.setBottom(geometry.bottom() + delta.y());

    constexpr int minimum = 24;
    if (geometry.width() < minimum)
    {
        if (m_resizeEdges.testFlag(Qt::LeftEdge))
            geometry.setLeft(geometry.right() - minimum + 1);
        else
            geometry.setRight(geometry.left() + minimum - 1);
    }
    if (geometry.height() < minimum)
    {
        if (m_resizeEdges.testFlag(Qt::TopEdge))
            geometry.setTop(geometry.bottom() - minimum + 1);
        else
            geometry.setBottom(geometry.top() + minimum - 1);
    }

    const qreal ratio = m_disableDpiScaling ? 1.0 : devicePixelRatioF();
    const bool horizontal = m_resizeEdges.testFlag(Qt::LeftEdge) || m_resizeEdges.testFlag(Qt::RightEdge);
    const bool vertical = m_resizeEdges.testFlag(Qt::TopEdge) || m_resizeEdges.testFlag(Qt::BottomEdge);
    qreal scale = m_scale;
    if (horizontal)
        scale = qMax<qreal>(0.1, qreal(geometry.width()) * ratio / m_image.width());
    if (vertical)
        scale = qMax<qreal>(scale, qreal(geometry.height()) * ratio / m_image.height());
    scale = qBound<qreal>(0.1, scale, 5.0);
    const QSize target(qMax(1, qRound(m_image.width() * scale / ratio)),
                       qMax(1, qRound(m_image.height() * scale / ratio)));
    if (m_resizeEdges.testFlag(Qt::LeftEdge))
        geometry.setLeft(geometry.right() - target.width() + 1);
    else
        geometry.setWidth(target.width());
    if (m_resizeEdges.testFlag(Qt::TopEdge))
        geometry.setTop(geometry.bottom() - target.height() + 1);
    else
        geometry.setHeight(target.height());
    m_scale = scale;
    setGeometry(geometry);
}
void PasteWindow::keyPressEvent(QKeyEvent *event)
{
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    const bool plain = modifiers == Qt::NoModifier;
    const bool primary = modifiers == snipaste::platform::primaryModifier();
    if (plain && event->key() == Qt::Key_Space)
    {
        if (!event->isAutoRepeat())
            setToolbarVisible(!m_showToolbar);
        event->accept();
        return;
    }
    if (m_showToolbar && plain && (event->key() == Qt::Key_Escape ||
        event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter))
    {
        setToolbarVisible(false);
        event->accept();
        return;
    }
    if ((primary && event->key() >= Qt::Key_1 && event->key() <= Qt::Key_9) ||
        (plain && (event->key() == Qt::Key_B || event->key() == Qt::Key_T ||
                   event->key() == Qt::Key_N || event->key() == Qt::Key_E)))
    {
        setToolbarVisible(true);
        for (auto *button : m_toolbar->findChildren<QToolButton *>())
        {
            if ((primary && button->property("toolNumber").toInt() == event->key() - Qt::Key_0) ||
                (plain && !button->property("toolKey").toString().isEmpty() &&
                 button->property("toolKey").toString() == QKeySequence(event->key()).toString()))
            {
                if (!button->isChecked())
                    button->click();
                event->accept();
                return;
            }
        }
    }
    if (m_showToolbar && m_canvas)
    {
        if (plain && event->key() == Qt::Key_Tab &&
            (m_canvas->tool() == AnnotationCanvas::Tool::Line || m_canvas->tool() == AnnotationCanvas::Tool::Arrow))
        {
            if (auto *toggle = m_annotationOptions->findChild<QToolButton *>(QStringLiteral("annotationToggleArrow")))
                toggle->click();
            event->accept();
            return;
        }
        const bool history = modifiers.testFlag(snipaste::platform::primaryModifier()) &&
                             (event->key() == Qt::Key_Z || event->key() == Qt::Key_Y);
        const bool editing = !m_editor->testAttribute(Qt::WA_TransparentForMouseEvents);
        if (history || (editing && plain && (event->key() == Qt::Key_Delete ||
            event->key() == Qt::Key_Backspace || event->key() == Qt::Key_BracketLeft ||
            event->key() == Qt::Key_BracketRight || event->key() == Qt::Key_1 || event->key() == Qt::Key_2)))
        {
            if (plain && (event->key() == Qt::Key_1 || event->key() == Qt::Key_2))
                m_canvas->adjustToolSize(event->key() == Qt::Key_1 ? 1 : -1);
            else
                QApplication::sendEvent(m_canvas, event);
            event->accept();
            return;
        }
    }
    if (event->key() == Qt::Key_Escape && modifiers.testFlag(Qt::ShiftModifier))
    {
        if (m_shiftEscapeAction == QStringLiteral("关闭贴图"))
            hidePaste();
        else if (m_shiftEscapeAction == QStringLiteral("销毁贴图"))
            close();
        return;
    }

    if (event->key() == Qt::Key_Escape ||
        (event->key() == Qt::Key_W && modifiers.testFlag(snipaste::platform::primaryModifier())))
    {
        if (m_escapeAction == QStringLiteral("关闭贴图"))
            hidePaste();
        else if (m_escapeAction == QStringLiteral("销毁贴图"))
            close();
        return;
    }

    if (modifiers.testFlag(snipaste::platform::primaryModifier()))
    {
        if (event->key() == Qt::Key_S && modifiers.testFlag(Qt::AltModifier) && m_copyAsFileShortcutEnabled)
        {
            if (m_canvas)
                m_canvas->finishTextInput();
            rebuildImage();
            emit copyAsFileRequested(this);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_A)
        {
            emit selectAllRequested(this);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_C && modifiers.testFlag(Qt::ShiftModifier))
        {
            if (!m_sourceText.isEmpty())
                QApplication::clipboard()->setText(m_sourceText);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_C || event->key() == Qt::Key_Insert)
        {
            copyImage();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_S)
        {
            const bool quick = modifiers.testFlag(Qt::ShiftModifier);
            QString path;
            if (quick)
            {
                const QString directory = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
                path = QDir(directory).filePath(
                    QStringLiteral("snipaste-%1.png")
                        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))));
            }
            else
            {
                path = QFileDialog::getSaveFileName(
                    this, QStringLiteral("保存贴图"), QStringLiteral("snipaste.png"),
                    QStringLiteral("PNG 图片 (*.png);;JPEG 图片 (*.jpg *.jpeg);;所有文件 (*.*)"));
            }
            if (!path.isEmpty())
            {
                if (QFileInfo(path).suffix().isEmpty())
                    path += QStringLiteral(".png");
                if (m_canvas)
                    m_canvas->finishTextInput();
                rebuildImage();
                m_image.save(path);
            }
            event->accept();
            return;
        }
    }

    const bool plus = event->key() == Qt::Key_Plus ||
                      (event->key() == Qt::Key_Equal && modifiers.testFlag(Qt::ShiftModifier));
    const bool minus = event->key() == Qt::Key_Minus || event->key() == Qt::Key_Underscore;
    if (plus || minus)
    {
        if (modifiers.testFlag(snipaste::platform::primaryModifier()))
            adjustOpacity(plus ? 0.05 : -0.05);
        else
            changeScale(plus ? 1.1 : 1.0 / 1.1);
        event->accept();
        return;
    }

    QPoint movement;
    switch (event->key())
    {
    case Qt::Key_Left:
        movement = QPoint(-1, 0);
        break;
    case Qt::Key_Right:
        movement = QPoint(1, 0);
        break;
    case Qt::Key_Up:
        movement = QPoint(0, -1);
        break;
    case Qt::Key_Down:
        movement = QPoint(0, 1);
        break;
    case Qt::Key_W:
        movement = QPoint(0, -1);
        break;
    case Qt::Key_S:
        movement = QPoint(0, 1);
        break;
    case Qt::Key_A:
        movement = QPoint(-1, 0);
        break;
    case Qt::Key_D:
        movement = QPoint(1, 0);
        break;
    default:
        break;
    }
    if (!movement.isNull() && modifiers == Qt::NoModifier)
    {
        if (m_exchangeNavigationKeys)
        {
            const bool arrow = event->key() == Qt::Key_Left || event->key() == Qt::Key_Right ||
                               event->key() == Qt::Key_Up || event->key() == Qt::Key_Down;
            if (arrow)
                emit moveRequested(this, movement);
            else
                move(pos() + movement);
        }
        else
        {
            move(pos() + movement);
        }
        event->accept();
        return;
    }

    switch (event->key())
    {
    case Qt::Key_1:
        m_rotationQuarterTurns = (m_rotationQuarterTurns + 1) % 4;
        rebuildImage();
        return;
    case Qt::Key_2:
        m_rotationQuarterTurns = (m_rotationQuarterTurns + 3) % 4;
        rebuildImage();
        return;
    case Qt::Key_3:
        m_flipHorizontal = !m_flipHorizontal;
        rebuildImage();
        return;
    case Qt::Key_4:
        m_flipVertical = !m_flipVertical;
        rebuildImage();
        return;
    case Qt::Key_5:
        m_grayscale = !m_grayscale;
        rebuildImage();
        return;
    case Qt::Key_6:
        m_inverted = !m_inverted;
        rebuildImage();
        return;
    case Qt::Key_V:
        if (modifiers.testFlag(snipaste::platform::primaryModifier()))
        {
            const QImage image = QApplication::clipboard()->image();
            if (!image.isNull())
                setSourceImage(image);
            return;
        }
        break;
    default:
        break;
    }

    QWidget::keyPressEvent(event);
}
void PasteWindow::wheelEvent(QWheelEvent *event)
{
    const bool configured = !m_mouseActions.isEmpty();
    const bool scaleAction = mouseModifierMatches(mouseAction(0, QStringLiteral("无")), event->modifiers()) &&
                             mouseAction(2, QStringLiteral("鼠标滚轮")) == QStringLiteral("鼠标滚轮");
    // Alt+wheel remains a compatibility shortcut when no custom mouse table
    // has been configured.
    if (scaleAction || (!configured && event->modifiers().testFlag(Qt::AltModifier)))
    {
        const qreal factor = event->angleDelta().y() > 0 ? 1.1 : 1.0 / 1.1;
        changeScale(factor);
        emit wheelTransformRequested(this, factor, 0.0);
        event->accept();
        return;
    }
    const bool opacityAction =
        mouseModifierMatches(mouseAction(4, QStringLiteral("Ctrl")), event->modifiers()) &&
        mouseAction(6, QStringLiteral("鼠标滚轮")) == QStringLiteral("鼠标滚轮");
    if (opacityAction)
    {
        const qreal delta = event->angleDelta().y() > 0 ? 0.05 : -0.05;
        setWindowOpacity(qBound(0.1, windowOpacity() + delta, 1.0));
        emit wheelTransformRequested(this, 1.0, delta);
        event->accept();
        return;
    }

    QWidget::wheelEvent(event);
}
} // namespace snipaste::ui
