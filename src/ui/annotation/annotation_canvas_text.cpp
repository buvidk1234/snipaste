#include "platform/keyboard.h"
#include "ui/annotation/annotation_canvas.h"
#include "ui/annotation/annotation_renderer.h"
#include "ui/annotation/annotation_selection_geometry.h"
#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QInputMethod>
#include <QKeyEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextLayout>
#include <QTimer>

namespace snipaste::ui
{
void AnnotationCanvas::syncTextStyle(const Shape &)
{
    syncSelectedStyle();
}

void AnnotationCanvas::editTextStyle(const std::function<void(Shape &)> &edit)
{
    if (m_textEditing || (m_selectedShape >= 0 && m_selectedShape < m_document.shapes().size() &&
                         m_document.shapes().at(m_selectedShape).tool == Tool::Text))
        editSelectedStyle(edit);
}

void AnnotationCanvas::beginTextInput(const QPointF &position)
{
    m_textShape = currentShape();
    m_textShape.start = m_textShape.end = toImagePoint(position);
    m_textShape.points.clear();
    m_textShapeIndex = -1;
    m_textEditor->clear();
    showTextEditor();
}

void AnnotationCanvas::editTextShape(int index)
{
    if (index < 0 || index >= m_document.shapes().size() ||
        m_document.shapes().at(index).tool != Tool::Text)
        return;
    finishTextInput();
    finishHistoryGesture();
    m_selectedShape = index;
    m_textShapeIndex = index;
    m_textShape = m_document.shapes().at(index);
    m_textEditor->setPlainText(m_textShape.text);
    syncTextStyle(m_textShape);
    showTextEditor();
    m_textEditor->moveCursor(QTextCursor::End);
}

void AnnotationCanvas::showTextEditor()
{
    m_textEditing = true;
    m_movingTextBox = false;
    m_textBoxHandle = -1;
    m_hoveredText = -1;
    m_hoveredShape = -1;
    updateTextEditorStyle();
    updateTextEditorGeometry();
    m_textEditor->document()->clearUndoRedoStacks();
    m_textEditor->show();
    m_textEditor->raise();
    m_textEditor->setFocus(Qt::MouseFocusReason);
    invalidateRenderCache();
    update();
}

void AnnotationCanvas::updateTextEditorStyle()
{
    if (!m_textEditing)
        return;
    const QColor color = m_textShape.color;
    const QString sheet =
        QStringLiteral("QPlainTextEdit { color: rgba(%1,%2,%3,%4); background: transparent; "
                       "border: 1px dashed rgb(40,137,237); "
                       "padding: 3px; selection-color: white; "
                       "selection-background-color: rgba(40,137,237,190); }")
            .arg(color.red()).arg(color.green()).arg(color.blue()).arg(color.alpha());
    if (m_textEditor->styleSheet() != sheet)
        m_textEditor->setStyleSheet(sheet);

    // Decorate the input without inserting format commands into its native
    // character/IME undo history.
    QTextEdit::ExtraSelection decoration;
    decoration.cursor = QTextCursor(m_textEditor->document());
    decoration.cursor.select(QTextCursor::Document);
    if (m_textShape.textBackground)
        decoration.format.setBackground(QColor(color.red(), color.green(), color.blue(), 80));
    if (m_textShape.textOutlineWidth > 0 && m_textShape.textOutlineColor.alpha() > 0)
    {
        QPen outline(m_textShape.textOutlineColor);
        outline.setWidthF(m_textShape.textOutlineWidth * imageRect().width() / qMax(1, m_image.width()));
        outline.setJoinStyle(Qt::RoundJoin);
        decoration.format.setTextOutline(outline);
    }
    m_textEditor->setExtraSelections({decoration});
}

void AnnotationCanvas::updateTextEditorGeometry()
{
    if (!m_textEditing || m_image.isNull())
        return;
    const QRectF target = imageRect();
    const qreal scale = target.width() / m_image.width();
    QFont textFont = font();
    textFont.setFamily(m_textShape.fontFamily);
    textFont.setPixelSize(qMax(1, qRound(m_textShape.fontSize * scale)));
    textFont.setBold(m_textShape.textBold);
    textFont.setItalic(m_textShape.textItalic);
    if (m_textEditor->font() != textFont)
        m_textEditor->setFont(textFont);
    Shape preview = m_textShape;
    preview.text = m_textEditor->toPlainText();
    // IME preedit is visible in the layout but absent from toPlainText().
    for (auto block = m_textEditor->document()->begin(); block.isValid(); block = block.next())
    {
        const QTextLayout *layout = block.layout();
        if (layout && !layout->preeditAreaText().isEmpty())
        {
            preview.text.insert(block.position() + layout->preeditAreaPosition(), layout->preeditAreaText());
            break;
        }
    }
    // Font hinting can change glyph extents at non-native preview scales.
    // Measure using the font size actually displayed by the editor.
    preview.fontSize = textFont.pixelSize();
    const QSizeF contentSize = annotation::textBoxRect(preview).size();
    const QRectF localTarget = target.translated(-QPointF(m_coordinateOrigin));
    const QRect area = localTarget.toAlignedRect().intersected(rect());
    if (area.isEmpty())
        return;
    const QPoint anchor = (localTarget.topLeft() + m_textShape.start * scale).toPoint()
                          - m_textEditor->viewport()->pos();
    const int editorWidth = qMin(qCeil(contentSize.width()) + 9, area.width());
    const int editorHeight = qMin(qCeil(contentSize.height()) + 8, area.height());
    m_textEditor->setGeometry(qBound(area.left(), anchor.x(), area.right() - editorWidth + 1),
                              qBound(area.top(), anchor.y(), area.bottom() - editorHeight + 1), editorWidth,
                              editorHeight);
    // Use Qt's laid-out block heights. Font lineSpacing alone can undercount
    // the last line and leave the native editor scrolled after pressing Enter.
    qreal documentHeight = 0;
    for (auto block = m_textEditor->document()->begin(); block.isValid(); block = block.next())
        documentHeight += m_textEditor->document()->documentLayout()->blockBoundingRect(block).height();
    const int neededHeight = qMin(qCeil(documentHeight) + 8, area.height());
    if (m_textEditor->height() < neededHeight)
    {
        QRect geometry = m_textEditor->geometry();
        geometry.setHeight(neededHeight);
        geometry.moveTop(qMin(geometry.top(), area.bottom() - neededHeight + 1));
        m_textEditor->setGeometry(geometry);
    }
    if (documentHeight <= m_textEditor->viewport()->height())
        m_textEditor->verticalScrollBar()->setValue(0);
    // Clamping a reopened editor must not move the saved object or clear redo.
    if (m_textShapeIndex < 0)
        m_textShape.start = m_textShape.end =
            toImagePoint(m_textEditor->pos() + m_textEditor->viewport()->pos());
}

void AnnotationCanvas::finishTextInput(bool commit)
{
    if (!m_textEditing)
        return;
    m_textEditing = false;
    m_movingTextBox = false;
    m_textBoxHandle = -1;
    if (commit)
        QGuiApplication::inputMethod()->commit();
    else
        QGuiApplication::inputMethod()->reset();
    m_textShape.text = m_textEditor->toPlainText();
    m_textEditor->hide();
    const int editedIndex = m_textShapeIndex;
    m_textShapeIndex = -1;
    if (commit)
    {
        if (editedIndex >= 0)
        {
            if (m_textShape.text.isEmpty())
            {
                m_document.remove(editedIndex);
                m_selectedShape = -1;
            }
            else if (m_textShape != m_document.shapes().at(editedIndex))
                m_document.editShape(editedIndex, [this](Shape &shape) { shape = m_textShape; });
        }
        else if (!m_textShape.text.isEmpty())
        {
            const int index = m_document.append(m_textShape);
            if (m_autoSelectOnCreation)
                m_selectedShape = index;
        }
        recordHistory();
    }
    else if (editedIndex >= 0)
        syncTextStyle(m_document.shapes().at(editedIndex));
    m_textEditor->clear();
    m_textEditor->document()->clearUndoRedoStacks();
    invalidateRenderCache();
    update();
}

bool AnnotationCanvas::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_textEditor && m_textEditing && event->type() == QEvent::InputMethod)
        // Measure after Qt updates the preedit layout, including cancellation.
        QTimer::singleShot(0, this, &AnnotationCanvas::updateTextEditorGeometry);
    if (m_textEditing && (watched == m_textEditor || watched == m_textEditor->viewport()) &&
        handleTextEditorPointer(watched, event))
        return true;
    if (watched == m_textEditor && m_textEditing &&
        (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride))
    {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape ||
            ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
             key->modifiers().testFlag(snipaste::platform::primaryModifier())))
        {
            if (event->type() == QEvent::KeyPress)
            {
                finishTextInput(key->key() != Qt::Key_Escape);
                setFocus();
            }
            event->accept();
            return true;
        }
    }
    // Focus may move to font menus and colour dialogs during the transaction.
    // Canvas clicks, tool changes and output explicitly commit it.
    return QWidget::eventFilter(watched, event);
}

bool AnnotationCanvas::handleTextEditorPointer(QObject *watched, QEvent *event)
{
    if (event->type() != QEvent::MouseMove && event->type() != QEvent::MouseButtonPress &&
        event->type() != QEvent::MouseButtonRelease)
        return false;
    auto *mouse = static_cast<QMouseEvent *>(event);
    auto *widget = static_cast<QWidget *>(watched);
    const QPointF canvasPoint = widget->mapTo(this, mouse->position());
    const QPointF local = m_textEditor->mapFrom(this, canvasPoint);
    const QRectF frame = m_textEditor->rect();
    constexpr qreal edge = 5;
    // Corners scale all the text; the rest of the border moves it. Inside
    // stays native text input, including character selection and IME.
    int handle = -1;
    const auto corners = annotation::selectionHandles(frame);
    for (int corner : {0, 2, 4, 6})
        if (QLineF(local, corners[corner]).length() <= edge + 1)
            handle = corner;
    const bool border = !frame.adjusted(edge, edge, -edge, -edge).contains(local);
    if (event->type() == QEvent::MouseButtonPress && mouse->button() == Qt::LeftButton &&
        (handle >= 0 || border)) {
        m_textBoxHandle = handle;
        m_movingTextBox = handle < 0;
        m_textBoxPointer = toImagePoint(canvasPoint);
        event->accept();
        return true;
    }
    if (m_movingTextBox || m_textBoxHandle >= 0) {
        if (event->type() == QEvent::MouseMove) {
            const QPointF point = toImagePoint(canvasPoint);
            const QPointF delta = point - m_textBoxPointer;
            if (m_movingTextBox)
                core::translateShape(m_textShape, delta);
            else {
                m_textShape.text = m_textEditor->toPlainText();
                annotation::resizeShape(m_textShape, m_textBoxHandle, delta);
                m_textFontSize = m_textShape.fontSize;
            }
            m_textBoxPointer = point;
            updateTextEditorGeometry();
            update();
        } else if (event->type() == QEvent::MouseButtonRelease && mouse->button() == Qt::LeftButton) {
            const bool scaled = m_textBoxHandle >= 0;
            m_movingTextBox = false;
            m_textBoxHandle = -1;
            if (scaled) {
                updateTextEditorStyle();
                emit textStyleChanged();
                emit annotationStyleChanged();
            }
        }
        event->accept();
        return true;
    }
    if (event->type() == QEvent::MouseMove) {
        const Qt::CursorShape cursor = handle >= 0 ? ((handle == 0 || handle == 4) ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor)
                                                   : border ? Qt::SizeAllCursor : Qt::IBeamCursor;
        m_textEditor->setCursor(cursor);
        m_textEditor->viewport()->setCursor(cursor);
    }
    return false;
}
} // namespace snipaste::ui
