#include "ui/ai/ai_dialog.h"

#include <QAction>
#include <QCloseEvent>
#include <QClipboard>
#include <QDir>
#include <QFileDialog>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QStandardPaths>
#include <QTimer>
#include <QTextDocument>
#include <QVBoxLayout>

namespace snipaste::ui
{
namespace
{
enum class ActionIcon { Send, Stop, Close };

QIcon actionIcon(ActionIcon action)
{
    QIcon icon;
    for (int scale : {1, 2, 3})
    {
        QPixmap pixels(16 * scale, 16 * scale);
        pixels.setDevicePixelRatio(scale);
        pixels.fill(Qt::transparent);
        QPainter painter(&pixels);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor("#66666f"), 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (action == ActionIcon::Close)
        {
            painter.drawLine(QPointF(4, 4), QPointF(12, 12));
            painter.drawLine(QPointF(12, 4), QPointF(4, 12));
        }
        else if (action == ActionIcon::Send)
        {
            painter.drawLine(QPointF(8, 12), QPointF(8, 4));
            painter.drawPolyline(QPolygonF{QPointF(4, 8), QPointF(8, 4), QPointF(12, 8)});
        }
        else
        {
            painter.setBrush(QColor("#66666f"));
            painter.setPen(Qt::NoPen);
            painter.drawRoundedRect(QRectF(4, 4, 8, 8), 1, 1);
        }
        painter.end();
        icon.addPixmap(pixels);
    }
    return icon;
}
} // namespace

AiDialog::AiDialog(const QImage &image, core::AiSettings settings, QWidget *parent)
    : QWidget(parent, parent ? Qt::Widget : Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
      m_client(settings, image)
{
    setObjectName(QStringLiteral("aiDialog"));
    setWindowTitle(QStringLiteral("截图对话"));
    setAttribute(Qt::WA_QuitOnClose, false);
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::ArrowCursor);
    resize(416, 440);
    setStyleSheet(QStringLiteral(
        "QFrame#aiCard { background: #f8f8fa; border: 1px solid #e3e3e8; border-radius: 14px; }"
        "QLabel { background: transparent; border: none; color: #242429; font-size: 13px; }"
        "QLabel#aiTitle { font-weight: 600; font-size: 12px; }"
        "QLabel#aiEmpty { color: #92929b; }"
        "QLabel#aiStatus { color: #a0a0a8; font-size: 10px; }"
        "QScrollArea#aiHistory { background: transparent; border: none; }"
        "QWidget#aiMessages, QWidget#qt_scrollarea_viewport { background: transparent; }"
        "QFrame#aiUserBubble { background: #ededf0; border: none; border-radius: 10px; }"
        "QFrame#aiComposer { background: #ffffff; border: 1px solid #e3e3e8; border-radius: 12px; }"
        "QPlainTextEdit#aiQuestion { background: transparent; border: none; color: #242429; "
        "font-size: 13px; padding: 0; selection-background-color: #dddded; }"
        "QPushButton { background: transparent; border: none; border-radius: 7px; padding: 0; }"
        "QPushButton:hover { background: #e9e9ed; }"
        "QPushButton#aiSend { border: 1px solid #dedee4; border-radius: 14px; background: #ffffff; }"
        "QPushButton#aiSend:hover { background: #f0f0f3; }"
        "QPushButton#aiSend:disabled { border-color: #ededf0; background: #f8f8fa; }"
        "QScrollBar:vertical { width: 5px; background: transparent; margin: 3px 0; }"
        "QScrollBar::handle:vertical { background: #d8d8df; min-height: 24px; border-radius: 2px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"));
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(14, 12, 14, 18);
    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("aiCard"));
    auto *shadow = new QGraphicsDropShadowEffect(card);
    shadow->setBlurRadius(24);
    shadow->setOffset(0, 4);
    shadow->setColor(QColor(20, 20, 35, 24));
    card->setGraphicsEffect(shadow);
    outer->addWidget(card);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(0, 0, 0, 12);
    layout->setSpacing(0);

    m_header = new QWidget(card);
    m_header->setObjectName(QStringLiteral("aiHeader"));
    m_header->setCursor(Qt::SizeAllCursor);
    m_header->installEventFilter(this);
    auto *header = new QHBoxLayout(m_header);
    header->setContentsMargins(16, 12, 12, 8);
    header->setSpacing(8);
    auto *preview = new QLabel(m_header);
    preview->setObjectName(QStringLiteral("aiPreview"));
    QImage thumbnail = image.scaled(QSize(20, 20) * devicePixelRatioF(), Qt::KeepAspectRatio,
                                    Qt::SmoothTransformation);
    thumbnail.setDevicePixelRatio(devicePixelRatioF());
    preview->setPixmap(QPixmap::fromImage(thumbnail));
    preview->setFixedSize(20, 20);
    preview->setAlignment(Qt::AlignCenter);
    preview->setAttribute(Qt::WA_TransparentForMouseEvents);
    header->addWidget(preview);
    auto *title = new QLabel(QStringLiteral("截图对话"), m_header);
    title->setObjectName(QStringLiteral("aiTitle"));
    title->setAttribute(Qt::WA_TransparentForMouseEvents);
    header->addWidget(title);
    header->addStretch();
    auto *close = new QPushButton(m_header);
    close->setObjectName(QStringLiteral("aiClose"));
    close->setIcon(actionIcon(ActionIcon::Close));
    close->setFixedSize(24, 24);
    close->setToolTip(QStringLiteral("返回截图 (Esc)"));
    close->setAccessibleName(QStringLiteral("关闭对话"));
    connect(close, &QPushButton::clicked, this, &QWidget::close);
    header->addWidget(close);
    layout->addWidget(m_header);

    m_history = new QScrollArea(card);
    m_history->setObjectName(QStringLiteral("aiHistory"));
    m_history->setWidgetResizable(true);
    m_history->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *messages = new QWidget(m_history);
    messages->setObjectName(QStringLiteral("aiMessages"));
    m_messages = new QVBoxLayout(messages);
    m_messages->setContentsMargins(16, 16, 16, 16);
    m_messages->setSpacing(22);
    m_messages->setAlignment(Qt::AlignTop);
    m_empty = new QLabel(QStringLiteral("想了解这张截图的什么？"), messages);
    m_empty->setObjectName(QStringLiteral("aiEmpty"));
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setWordWrap(true);
    m_messages->addWidget(m_empty);
    m_history->setWidget(messages);
    connect(m_history->verticalScrollBar(), &QScrollBar::rangeChanged, this, &AiDialog::scrollToEnd);
    layout->addWidget(m_history, 1);

    auto *composer = new QFrame(card);
    composer->setObjectName(QStringLiteral("aiComposer"));
    auto *input = new QVBoxLayout(composer);
    input->setContentsMargins(12, 10, 10, 8);
    input->setSpacing(6);
    m_question = new QPlainTextEdit(composer);
    m_question->setObjectName(QStringLiteral("aiQuestion"));
    m_question->setPlaceholderText(QStringLiteral("问点什么…"));
    m_question->setFixedHeight(44);
    m_question->document()->setDocumentMargin(0);
    m_question->installEventFilter(this);
    setFocusProxy(m_question);
    input->addWidget(m_question);
    auto *actions = new QHBoxLayout;
    m_status = new QLabel(QStringLiteral("Enter 发送 · Shift + Enter 换行"), composer);
    m_status->setObjectName(QStringLiteral("aiStatus"));
    m_status->setWordWrap(true);
    actions->addWidget(m_status, 1);
    m_send = new QPushButton(composer);
    m_send->setObjectName(QStringLiteral("aiSend"));
    m_send->setFixedSize(28, 28);
    actions->addWidget(m_send);
    input->addLayout(actions);
    auto *composerRow = new QHBoxLayout;
    composerRow->setContentsMargins(12, 0, 12, 0);
    composerRow->addWidget(composer);
    layout->addLayout(composerRow);

    connect(m_send, &QPushButton::clicked, this, [this] { send(); });
    connect(m_question, &QPlainTextEdit::textChanged, this, [this] {
        m_send->setEnabled(m_client.isBusy() || !m_question->toPlainText().trimmed().isEmpty());
    });
    connect(&m_client, &core::AiClient::answered, this, [this](const QString &text, const QList<QImage> &images) {
        showAnswer(text, QStringLiteral("#242429"));
        showImages(images);
        m_pendingTurn = nullptr;
        setBusy(false);
        m_status->setText(QStringLiteral("Enter 发送 · Shift + Enter 换行"));
        m_question->setFocus();
    });
    connect(&m_client, &core::AiClient::failed, this, [this](const QString &message) {
        showAnswer(message, QStringLiteral("#b34a4a"));
        m_failedTurn = m_pendingTurn;
        m_question->setPlainText(m_pendingQuestion);
        setBusy(false);
        m_status->setText(QStringLiteral("可修改问题后重试"));
    });
    setBusy(false);
    if (settings.apiKey.isEmpty() || settings.model.isEmpty())
        m_status->setText(QStringLiteral("请先在首选项 → AI 中填写配置"));
    m_question->setFocus();
}

void AiDialog::send()
{
    if (m_client.isBusy())
    {
        m_client.cancel();
        showAnswer(QStringLiteral("已停止"), QStringLiteral("#92929b"));
        m_failedTurn = m_pendingTurn;
        m_question->setPlainText(m_pendingQuestion);
        setBusy(false);
        m_status->setText(QStringLiteral("可修改问题后重试"));
        return;
    }
    m_pendingQuestion = m_question->toPlainText().trimmed();
    if (m_pendingQuestion.isEmpty())
        return;
    delete m_failedTurn;
    m_failedTurn = nullptr;
    m_empty->hide();
    m_pendingTurn = new QWidget;
    m_pendingTurn->setObjectName(QStringLiteral("aiTurn"));
    auto *turn = new QVBoxLayout(m_pendingTurn);
    turn->setContentsMargins(0, 0, 0, 0);
    turn->setSpacing(20);
    auto *questionRow = new QHBoxLayout;
    questionRow->addStretch();
    auto *bubble = new QFrame(m_pendingTurn);
    bubble->setObjectName(QStringLiteral("aiUserBubble"));
    bubble->setMaximumWidth(qMax(160, m_history->viewport()->width() - 64));
    auto *bubbleLayout = new QVBoxLayout(bubble);
    bubbleLayout->setContentsMargins(12, 9, 12, 9);
    auto *question = new QLabel(m_pendingQuestion, bubble);
    question->setObjectName(QStringLiteral("aiUserMessage"));
    question->setTextFormat(Qt::PlainText);
    question->setWordWrap(true);
    question->setTextInteractionFlags(Qt::TextSelectableByMouse);
    bubbleLayout->addWidget(question);
    questionRow->addWidget(bubble);
    turn->addLayout(questionRow);
    m_answer = new QLabel(m_pendingTurn);
    m_answer->setObjectName(QStringLiteral("aiAnswer"));
    m_answer->setTextFormat(Qt::PlainText);
    m_answer->setWordWrap(true);
    m_answer->setTextInteractionFlags(Qt::TextSelectableByMouse);
    turn->addWidget(m_answer);
    m_messages->addWidget(m_pendingTurn);
    m_question->clear();
    setBusy(true);
    m_status->setText(QStringLiteral("正在回答…"));
    showAnswer(QStringLiteral("思考中…"), QStringLiteral("#92929b"));
    m_client.send(m_pendingQuestion);
}

void AiDialog::showAnswer(const QString &text, const QString &color)
{
    m_answer->setText(text);
    m_answer->setVisible(!text.isEmpty());
    m_answer->setStyleSheet(QStringLiteral("color: %1;").arg(color));
    scrollToEnd();
}

void AiDialog::showImages(const QList<QImage> &images)
{
    for (const QImage &image : images)
    {
        auto *label = new QLabel(m_pendingTurn);
        label->setObjectName(QStringLiteral("aiGeneratedImage"));
        label->setAccessibleName(QStringLiteral("生成的图片"));
        label->setToolTip(QStringLiteral("右键复制或保存原图"));
        const QSize bounds(qMax(1, m_history->viewport()->width() - 32), 280);
        QPixmap preview = QPixmap::fromImage(image.scaled(image.size().boundedTo(bounds * devicePixelRatioF()),
                                                         Qt::KeepAspectRatio, Qt::SmoothTransformation));
        preview.setDevicePixelRatio(devicePixelRatioF());
        label->setPixmap(preview);
        label->setFixedSize(preview.deviceIndependentSize().toSize());
        label->setContextMenuPolicy(Qt::ActionsContextMenu);
        auto *copy = label->addAction(QStringLiteral("复制图片"), [image] {
            QGuiApplication::clipboard()->setImage(image);
        });
        copy->setObjectName(QStringLiteral("aiCopyImage"));
        label->addAction(QStringLiteral("保存图片…"), this, [this, image] {
            const QString path = QFileDialog::getSaveFileName(
                this, QStringLiteral("保存图片"),
                QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).filePath(QStringLiteral("AI.png")),
                QStringLiteral("PNG 图片 (*.png)"));
            if (!path.isEmpty() && !image.save(path, "PNG"))
                m_status->setText(QStringLiteral("图片保存失败，请选择其他位置。"));
        });
        static_cast<QVBoxLayout *>(m_pendingTurn->layout())->addWidget(label, 0, Qt::AlignLeft);
    }
    scrollToEnd();
}

void AiDialog::scrollToEnd()
{
    QTimer::singleShot(0, this, [this] {
        m_history->verticalScrollBar()->setValue(m_history->verticalScrollBar()->maximum());
    });
}

void AiDialog::setBusy(bool busy)
{
    m_question->setEnabled(!busy);
    m_send->setIcon(actionIcon(busy ? ActionIcon::Stop : ActionIcon::Send));
    m_send->setAccessibleName(busy ? QStringLiteral("停止") : QStringLiteral("发送"));
    m_send->setToolTip(busy ? QStringLiteral("停止回答") : QStringLiteral("发送选区与问题 (Enter)"));
    m_send->setEnabled(busy || !m_question->toPlainText().trimmed().isEmpty());
}

void AiDialog::placeNear(const QRect &selection, const QRect &bounds)
{
    const QRect area = bounds.adjusted(6, 6, -6, -6);
    resize(QSize(416, 440).boundedTo(area.size()));
    const QList<QPoint> positions{
        {selection.left() - 14, selection.bottom() + 8},
        {selection.right() + 8, selection.top() - 12},
        {selection.left() - width() - 8, selection.top() - 12},
        {selection.left() - 14, selection.top() - height() - 8}};
    for (const QPoint &position : positions)
        if (area.contains(QRect(position, size())))
        {
            move(position);
            return;
        }
    move(qBound(area.left(), positions.first().x(), area.right() - width() + 1),
         qBound(area.top(), positions.first().y(), area.bottom() - height() + 1));
}

bool AiDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_question && event->type() == QEvent::InputMethod)
    {
        m_composing = !static_cast<QInputMethodEvent *>(event)->preeditString().isEmpty();
        m_question->setPlaceholderText(m_composing ? QString() : QStringLiteral("问点什么…"));
    }
    if (watched == m_question && event->type() == QEvent::KeyPress)
    {
        auto *key = static_cast<QKeyEvent *>(event);
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
            !key->modifiers().testFlag(Qt::ShiftModifier) && !m_composing)
        {
            if (!m_client.isBusy())
                send();
            return true;
        }
    }
    if (watched == m_header)
    {
        if (event->type() == QEvent::MouseButtonPress)
        {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton)
            {
                m_dragOffset = mouse->globalPosition().toPoint() - mapToGlobal(QPoint());
                m_dragging = true;
                return true;
            }
        }
        else if (event->type() == QEvent::MouseMove && m_dragging)
        {
            const QPoint global = static_cast<QMouseEvent *>(event)->globalPosition().toPoint() - m_dragOffset;
            QPoint position = parentWidget() ? parentWidget()->mapFromGlobal(global) : global;
            const QRect area = parentWidget() ? parentWidget()->rect() : screen()->availableGeometry();
            position.setX(qBound(area.left(), position.x(), area.right() - width() + 1));
            position.setY(qBound(area.top(), position.y(), area.bottom() - height() + 1));
            move(position);
            return true;
        }
        else if (event->type() == QEvent::MouseButtonRelease)
            m_dragging = false;
    }
    return QWidget::eventFilter(watched, event);
}

void AiDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape)
        close();
    event->accept();
}

void AiDialog::closeEvent(QCloseEvent *event)
{
    m_client.cancel();
    QWidget::closeEvent(event);
    if (event->isAccepted())
        emit closed();
}
} // namespace snipaste::ui
