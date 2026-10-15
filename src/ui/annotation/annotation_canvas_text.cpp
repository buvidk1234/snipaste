#include "platform/keyboard.h"
#include "ui/annotation/annotation_canvas.h"
#include <QApplication>
#include <QFocusEvent>
#include <QInputMethod>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QTextDocument>
namespace snipaste::ui
{
void AnnotationCanvas::beginTextInput(const QPointF &position)
{
    m_textShape = currentShape();
    m_textShape.start = toImagePoint(position);
    m_textEditor->clear();
    m_textEditor->setStyleSheet(
        QStringLiteral("QPlainTextEdit { color: %1; background: rgba(255,255,255,12); "
                       "border: 1px solid rgba(40,137,237,150); border-radius: 2px; "
                       "padding: 2px; selection-color: white; "
                       "selection-background-color: rgba(40,137,237,190); }")
            .arg(m_textShape.color.name()));
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
    const int editorHeight =
        qMin(qMax(metrics.lineSpacing() + 6, int(lines.size()) * metrics.lineSpacing() + 6), area.height());
    m_textEditor->setGeometry(qBound(area.left(), anchor.x(), area.right() - editorWidth + 1),
                              qBound(area.top(), anchor.y(), area.bottom() - editorHeight + 1), editorWidth,
                              editorHeight);
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
    if (commit && !m_textShape.text.isEmpty())
    {
        m_document.append(m_textShape);
        invalidateRenderCache();
        if (m_autoSelectOnCreation)
            m_selectedShape = m_document.shapes().size() - 1;
        recordHistory();
    }
    m_textEditor->clear();
    update();
}
bool AnnotationCanvas::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_textEditor && m_textEditing)
    {
        if (event->type() == QEvent::KeyPress)
        {
            auto *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_Escape ||
                ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
                 key->modifiers().testFlag(snipaste::platform::primaryModifier())))
            {
                finishTextInput(key->key() != Qt::Key_Escape);
                setFocus();
                event->accept();
                return true;
            }
        }
        else if (event->type() == QEvent::FocusOut &&
                 static_cast<QFocusEvent *>(event)->reason() != Qt::PopupFocusReason)
        {
            finishTextInput();
        }
    }
    return QWidget::eventFilter(watched, event);
}
} // namespace snipaste::ui
