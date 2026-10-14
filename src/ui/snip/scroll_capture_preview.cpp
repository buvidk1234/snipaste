#include "ui/snip/scroll_capture_preview.h"

#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QScrollBar>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

namespace snipaste::ui {

ScrollCapturePreview::ScrollCapturePreview(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("scrollCapturePreviewPanel"));
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setFocusPolicy(Qt::NoFocus);
    setFixedWidth(260);
    setMinimumHeight(240);

    setStyleSheet(QStringLiteral(
        "QWidget#scrollCapturePreviewPanel { background: #ffffff; border: 1px solid #dfe2e6; border-radius: 8px; }"
        "QScrollArea { background: transparent; border: none; }"
        "QScrollBar:vertical {"
        "  background: transparent;"
        "  width: 6px;"
        "  margin: 4px 2px 4px 0px;"
        "  border-radius: 3px;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background: #c0c4cc;"
        "  min-height: 24px;"
        "  border-radius: 3px;"
        "}"
        "QScrollBar::handle:vertical:hover {"
        "  background: #909399;"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
        "  height: 0px;"
        "}"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {"
        "  background: none;"
        "}"));

    auto *shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(20);
    shadow->setOffset(0, 4);
    shadow->setColor(QColor(0, 0, 0, 60));
    setGraphicsEffect(shadow);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(0);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setObjectName(QStringLiteral("scrollCaptureScrollArea"));
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setWidgetResizable(true);

    m_imageLabel = new QLabel(m_scrollArea);
    m_imageLabel->setObjectName(QStringLiteral("scrollCapturePreviewImage"));
    m_imageLabel->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    m_imageLabel->setStyleSheet(QStringLiteral("background: transparent;"));
    m_scrollArea->setWidget(m_imageLabel);

    layout->addWidget(m_scrollArea);
}

void ScrollCapturePreview::setImage(const QImage &image)
{
    m_stitchedImage = image;
    if (image.isNull()) {
        clear();
        return;
    }

    const int availableW = qMax(80, width() - 14);
    const qreal scale = qreal(availableW) / qMax(1, image.width());
    const int targetH = qMax(1, qRound(image.height() * scale));

    QPixmap pm = QPixmap::fromImage(
        image.scaled(QSize(availableW, targetH), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_imageLabel->setPixmap(pm);
    m_imageLabel->setFixedSize(availableW, targetH);

    QTimer::singleShot(10, this, [this] {
        if (m_scrollArea && m_scrollArea->verticalScrollBar()) {
            auto *bar = m_scrollArea->verticalScrollBar();
            bar->setValue(bar->maximum());
        }
    });
}

void ScrollCapturePreview::clear()
{
    m_stitchedImage = QImage();
    if (m_imageLabel) {
        m_imageLabel->clear();
        m_imageLabel->setFixedSize(0, 0);
    }
}

void ScrollCapturePreview::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(223, 226, 230), 1));
    painter.setBrush(QColor(255, 255, 255));
    painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 8, 8);
}

} // namespace snipaste::ui
