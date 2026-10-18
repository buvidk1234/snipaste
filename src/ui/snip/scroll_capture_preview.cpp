#include "ui/snip/scroll_capture_preview.h"
#include "core/scroll_stitcher.h"

#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QScrollBar>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

namespace snipaste::ui {

class ScrollCaptureImage final : public QWidget
{
public:
    explicit ScrollCaptureImage(QWidget *parent) : QWidget(parent) {}

    void setCapture(const core::VerticalScrollStitcher &capture, int width)
    {
        // Implicitly shared strip pixels; the preview owns a stable snapshot
        // without retaining a second contiguous copy of the long screenshot.
        m_capture = capture;
        const QSize source = m_capture.size();
        m_scale = source.isEmpty() ? 1.0 : qreal(width) / source.width();
        setFixedSize(source.isEmpty() ? QSize(0, 0)
                                     : QSize(width, qMax(1, qRound(source.height() * m_scale))));
        update();
    }

    QSize imageSize() const { return m_capture.size(); }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        if (m_capture.size().isEmpty())
            return;
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.scale(m_scale, m_scale);
        const QRectF exposed = QRectF(QPointF(event->rect().topLeft()) / m_scale,
                                     QSizeF(event->rect().size()) / m_scale).adjusted(-1, -1, 1, 1);
        m_capture.draw(painter, exposed);
    }

private:
    core::VerticalScrollStitcher m_capture;
    qreal m_scale = 1.0;
};

ScrollCapturePreview::ScrollCapturePreview(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("scrollCapturePreviewPanel"));
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
#if defined(Q_OS_MACOS)
    setAttribute(Qt::WA_MacAlwaysShowToolWindow, true);
#endif
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
    m_scrollArea->setWidgetResizable(false);
    m_scrollArea->setAlignment(Qt::AlignTop | Qt::AlignHCenter);

    m_imageWidget = new ScrollCaptureImage(m_scrollArea);
    m_imageWidget->setObjectName(QStringLiteral("scrollCapturePreviewImage"));
    m_scrollArea->setWidget(m_imageWidget);
    layout->addWidget(m_scrollArea);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("scrollCaptureStatus"));
    m_status->setWordWrap(true);
    m_status->setAlignment(Qt::AlignCenter);
    m_status->setStyleSheet(QStringLiteral("color: #5b6470; font-size: 12px; padding: 6px;"));
    layout->addWidget(m_status);
    clear();
}

void ScrollCapturePreview::setCapture(const core::VerticalScrollStitcher &capture)
{
    m_imageWidget->setCapture(capture, qMax(80, width() - 14));

    QTimer::singleShot(10, this, [this] {
        if (m_scrollArea && m_scrollArea->verticalScrollBar()) {
            auto *bar = m_scrollArea->verticalScrollBar();
            bar->setValue(bar->maximum());
        }
    });
}

void ScrollCapturePreview::clear()
{
    m_imageWidget->setCapture(core::VerticalScrollStitcher{}, 0);
    setStatus(QStringLiteral("等待采集"));
}

void ScrollCapturePreview::setStatus(const QString &status)
{
    m_status->setText(status);
}

QSize ScrollCapturePreview::imageSize() const
{
    return m_imageWidget->imageSize();
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
