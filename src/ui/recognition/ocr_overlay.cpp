#include "ui/recognition/ocr_overlay.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFrame>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QTextBoundaryFinder>
#include <QTextLayout>
#include <QToolButton>
#include <QWheelEvent>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <limits>

namespace snipaste::ui {

OcrOverlay::OcrOverlay(QImage image, QRectF imageFrame, QWidget *parent)
    : QWidget(parent), m_image(std::move(image)), m_imageFrame(imageFrame)
{
    setObjectName(QStringLiteral("ocrOverlay"));
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setCursor(Qt::ArrowCursor);
    m_toolbar = new QFrame(this);
    m_toolbar->setObjectName(QStringLiteral("ocrToolbar"));
    m_toolbar->setCursor(Qt::ArrowCursor);
    m_toolbar->setStyleSheet(QStringLiteral(
        "QFrame#ocrToolbar { background: #fafafa; border: 1px solid #d5d9df; border-radius: 8px; }"
        "QToolButton { color: #202124; background: transparent; border: none; padding: 8px 10px; }"
        "QToolButton:hover { background: #e6effa; border-radius: 5px; }"
        "QToolButton:disabled { color: #969a9e; } QLabel { color: #555; border: none; }"));
    auto *row = new QHBoxLayout(m_toolbar);
    row->setContentsMargins(6, 4, 6, 4);
    row->setSpacing(3);
    const auto button = [this, row](const QString &name, const QString &text) {
        auto *control = new QToolButton(m_toolbar);
        control->setObjectName(name);
        control->setText(text);
        control->setFocusPolicy(Qt::NoFocus);
        row->addWidget(control);
        return control;
    };
    m_copyAll = button(QStringLiteral("ocrCopyAll"), QStringLiteral("复制所有文本"));
    m_copySelection = button(QStringLiteral("ocrCopySelection"), QStringLiteral("复制选中文字"));
    m_copyAll->setEnabled(false);
    m_copySelection->setEnabled(false);
    m_status = new QLabel(QStringLiteral("正在识别…"), m_toolbar);
    m_status->setObjectName(QStringLiteral("ocrStatus"));
    m_status->setMaximumWidth(260);
    m_status->setWordWrap(false);
    row->addWidget(m_status);
    auto *exit = button(QStringLiteral("ocrExit"), QStringLiteral("退出识别"));
    connect(m_copyAll, &QToolButton::clicked, this, &OcrOverlay::copyAll);
    connect(m_copySelection, &QToolButton::clicked, this, &OcrOverlay::copySelection);
    connect(exit, &QToolButton::clicked, this, &OcrOverlay::exitRequested);
}

void OcrOverlay::startRecognition(const QString &engine, const QString &language)
{
    const quint64 generation = ++m_generation;
    m_result = {};
    m_words.clear();
    m_mask = {};
    m_selecting = false;
    updateSelection(-1, -1);
    m_copyAll->setEnabled(false);
    m_status->setWordWrap(false);
    m_status->setText(QStringLiteral("正在识别…"));
    layoutToolbar();
    update();
    auto *watcher = new QFutureWatcher<core::OcrResult>(this);
    connect(watcher, &QFutureWatcher<core::OcrResult>::finished, this, [this, watcher, generation] {
        if (generation == m_generation)
            applyResult(watcher->result());
        watcher->deleteLater();
    });
    // QImage is implicitly shared. No GUI objects or clipboard access leave
    // the GUI thread; destroying this view disconnects a late completion.
    watcher->setFuture(QtConcurrent::run([image = m_image, engine, language] {
        return core::OcrService::recognize(image, engine, language);
    }));
}

void OcrOverlay::setResult(core::OcrResult result)
{
    ++m_generation;
    applyResult(std::move(result));
}

void OcrOverlay::applyResult(core::OcrResult result)
{
    m_result = std::move(result);
    m_words.clear();
    m_selecting = false;
    if (!m_result.success)
        m_result.boxes.clear();
    for (const auto &box : m_result.boxes) {
        if (box.start < 0 || box.length <= 0 || box.start > m_result.text.size() - box.length)
            continue;
        Word word;
        word.box = box;
        word.box.bounds = box.bounds.intersected(QRectF(m_image.rect()));
        if (word.box.bounds.isEmpty())
            continue;
        const QString text = m_result.text.mid(box.start, box.length);
        QFont textFont = font();
        textFont.setPixelSize(qMax(1, qRound(box.bounds.height())));
        QTextLayout layout(text, textFont);
        layout.beginLayout();
        QTextLine line = layout.createLine();
        if (!line.isValid()) {
            layout.endLayout();
            continue;
        }
        line.setLineWidth(1e9);
        layout.endLayout();
        const qreal textWidth = qMax<qreal>(1, line.horizontalAdvance());
        word.positions.reserve(text.size() + 1);
        for (int index = 0; index <= text.size(); ++index)
            word.positions.append(qBound<qreal>(0, line.cursorToX(index) / textWidth, 1));
        QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
        word.boundaries.append(0);
        for (int offset = finder.toNextBoundary(); offset >= 0; offset = finder.toNextBoundary())
            word.boundaries.append(offset);
        m_words.append(std::move(word));
    }
    m_mask = QRegion(m_imageFrame.toAlignedRect());
    for (const auto &word : m_words)
        m_mask -= wordRect(word).adjusted(-2, -2, 2, 2).toAlignedRect();
    updateSelection(-1, -1);
    m_copyAll->setEnabled(m_result.success && !m_result.text.isEmpty());
    m_status->setWordWrap(!m_result.success);
    m_status->setText(!m_result.success ? m_result.error : m_words.isEmpty()
        ? QStringLiteral("无法定位文字，可复制全部")
        : QStringLiteral("拖动选取文字 · %1 复制")
              .arg(QKeySequence(QKeySequence::Copy).toString(QKeySequence::NativeText)));
    layoutToolbar();
    update();
    if (m_result.success)
        emit recognitionCompleted();
}

QRectF OcrOverlay::wordRect(const Word &word) const
{
    if (m_image.isNull())
        return {};
    const qreal sx = m_imageFrame.width() / m_image.width(), sy = m_imageFrame.height() / m_image.height();
    const QRectF box = word.box.bounds;
    return QRectF(m_imageFrame.topLeft() + QPointF(box.x() * sx, box.y() * sy),
                  QSizeF(box.width() * sx, box.height() * sy));
}

void OcrOverlay::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.drawImage(m_imageFrame, m_image);
    if (m_words.isEmpty())
        return;
    painter.save();
    painter.setClipRegion(m_mask);
    painter.fillRect(m_imageFrame, QColor(0, 0, 0, 100));
    painter.restore();
    if (m_anchor < 0 || m_cursor < 0)
        return;
    const qsizetype first = qMin(m_anchor, m_cursor), last = qMax(m_anchor, m_cursor);
    for (const auto &word : m_words) {
        const qsizetype begin = qMax(first, word.box.start), end = qMin(last, word.box.start + word.box.length);
        if (begin >= end)
            continue;
        const QRectF rect = wordRect(word);
        const qreal x1 = rect.left() + word.positions[begin - word.box.start] * rect.width();
        const qreal x2 = rect.left() + word.positions[end - word.box.start] * rect.width();
        painter.fillRect(QRectF(QPointF(qMin(x1, x2), rect.top()),
                                QPointF(qMax(x1, x2), rect.bottom())), QColor(30, 120, 235, 110));
    }
}

int OcrOverlay::wordAt(QPointF point, bool nearest) const
{
    int best = -1;
    qreal bestY = std::numeric_limits<qreal>::max(), bestX = bestY;
    for (int index = 0; index < m_words.size(); ++index) {
        const QRectF rect = wordRect(m_words[index]);
        if (rect.contains(point))
            return index;
        if (!nearest)
            continue;
        const qreal dy = qMax<qreal>(0, qMax(rect.top() - point.y(), point.y() - rect.bottom()));
        const qreal dx = qMax<qreal>(0, qMax(rect.left() - point.x(), point.x() - rect.right()));
        if (dy < bestY || (dy == bestY && dx < bestX)) {
            best = index;
            bestY = dy;
            bestX = dx;
        }
    }
    return best;
}

qsizetype OcrOverlay::cursorAt(QPointF point, bool nearest) const
{
    const int index = wordAt(point, nearest);
    if (index < 0)
        return -1;
    const auto &word = m_words[index];
    const QRectF rect = wordRect(word);
    const qreal x = (point.x() - rect.left()) / rect.width();
    qreal distance = std::numeric_limits<qreal>::max();
    int cursor = 0;
    for (const int offset : word.boundaries) {
        const qreal current = qAbs(word.positions[offset] - x);
        if (current < distance) {
            cursor = offset;
            distance = current;
        }
    }
    return word.box.start + cursor;
}

void OcrOverlay::updateSelection(qsizetype anchor, qsizetype cursor)
{
    if (m_anchor == anchor && m_cursor == cursor)
        return;
    const QRegion dirty = selectionRegion(m_anchor, m_cursor) | selectionRegion(anchor, cursor);
    m_anchor = anchor;
    m_cursor = cursor;
    m_copySelection->setEnabled(anchor >= 0 && cursor >= 0 && anchor != cursor);
    update(dirty);
}

QRegion OcrOverlay::selectionRegion(qsizetype anchor, qsizetype cursor) const
{
    QRegion region;
    if (anchor < 0 || cursor < 0 || anchor == cursor)
        return region;
    const qsizetype first = qMin(anchor, cursor), last = qMax(anchor, cursor);
    for (const auto &word : m_words)
        if (word.box.start < last && word.box.start + word.box.length > first)
            region += wordRect(word).toAlignedRect();
    return region;
}

QString OcrOverlay::selectedText() const
{
    if (m_anchor < 0 || m_cursor < 0)
        return {};
    return m_result.text.mid(qMin(m_anchor, m_cursor), qAbs(m_anchor - m_cursor));
}

void OcrOverlay::selectAll()
{
    if (m_result.success)
        updateSelection(0, m_result.text.size());
}

void OcrOverlay::copySelection()
{
    const QString text = selectedText();
    if (!text.isEmpty())
        QApplication::clipboard()->setText(text);
}

void OcrOverlay::copyAll()
{
    if (m_result.success && !m_result.text.isEmpty())
        QApplication::clipboard()->setText(m_result.text);
}

void OcrOverlay::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        setFocus(Qt::MouseFocusReason);
        const qsizetype cursor = cursorAt(event->position(), false);
        updateSelection(event->modifiers().testFlag(Qt::ShiftModifier) && m_anchor >= 0 ? m_anchor : cursor, cursor);
        m_selecting = cursor >= 0;
    }
    event->accept();
}

void OcrOverlay::mouseMoveEvent(QMouseEvent *event)
{
    if (m_selecting)
        updateSelection(m_anchor, cursorAt(event->position(), true));
    else
        setCursor(wordAt(event->position(), false) >= 0 ? Qt::IBeamCursor : Qt::ArrowCursor);
    event->accept();
}

void OcrOverlay::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_selecting) {
        updateSelection(m_anchor, cursorAt(event->position(), true));
        m_selecting = false;
    }
    event->accept();
}

void OcrOverlay::mouseDoubleClickEvent(QMouseEvent *event)
{
    const int index = wordAt(event->position(), false);
    if (event->button() == Qt::LeftButton && index >= 0) {
        const auto &box = m_words[index].box;
        updateSelection(box.start, box.start + box.length);
        m_selecting = false;
    }
    event->accept();
}

void OcrOverlay::keyPressEvent(QKeyEvent *event)
{
    if (event->matches(QKeySequence::Copy))
        copySelection();
    else if (event->matches(QKeySequence::SelectAll))
        selectAll();
    else if (event->key() == Qt::Key_Escape)
        emit exitRequested();
    event->accept();
}

void OcrOverlay::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    auto *copy = menu.addAction(QStringLiteral("复制选中文字"), this, &OcrOverlay::copySelection);
    copy->setEnabled(!selectedText().isEmpty());
    auto *all = menu.addAction(QStringLiteral("复制所有文本"), this, &OcrOverlay::copyAll);
    all->setEnabled(m_copyAll->isEnabled());
    menu.addAction(QStringLiteral("全选"), this, &OcrOverlay::selectAll)->setEnabled(m_copyAll->isEnabled());
    menu.exec(event->globalPos());
    event->accept();
}

void OcrOverlay::wheelEvent(QWheelEvent *event)
{
    event->accept();
}

void OcrOverlay::layoutToolbar()
{
    const int available = qMax(1, width() - 16);
    m_toolbar->setMaximumWidth(available);
    m_toolbar->adjustSize();
    const int x = qBound(8, qRound(m_imageFrame.center().x() - m_toolbar->width() / 2.0),
                         qMax(8, width() - m_toolbar->width() - 8));
    int y = qRound(m_imageFrame.top()) - m_toolbar->height() - 8;
    if (y < 8)
        y = qRound(m_imageFrame.bottom()) + 8;
    if (y + m_toolbar->height() > height() - 8)
        y = 8;
    m_toolbar->move(x, y);
}

void OcrOverlay::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    layoutToolbar();
}

} // namespace snipaste::ui
