#pragma once
#include <QEvent>
#include <QPainter>
#include <QSlider>
#include <QStackedLayout>
#include <QTimer>
#include <QWidget>
#include <functional>
namespace snipaste::ui::annotation
{
inline QString dropdownButtonStyle()
{
    return QStringLiteral(
        "QToolButton { background: #ffffff; border: 1px solid #c4c9cf; border-radius: 1px; }"
        "QToolButton:hover { border: 1px solid #2889ed; background: #f0f7fe; }"
        "QToolButton:pressed, QToolButton:open { border: 1px solid #2889ed; background: #e0effc; }");
}

class SubtoolbarStackedLayout : public QStackedLayout
{
  public:
    explicit SubtoolbarStackedLayout(QWidget *parent = nullptr) : QStackedLayout(parent)
    {
        setSizeConstraint(QLayout::SetFixedSize);
        setContentsMargins(0, 0, 0, 0);
        setSpacing(0);
    }

    QSize sizeHint() const override
    {
        if (QWidget *w = currentWidget())
        {
            return w->sizeHint();
        }
        return QStackedLayout::sizeHint();
    }

    QSize minimumSize() const override { return sizeHint(); }
};
class TickSlider : public QSlider
{
  public:
    explicit TickSlider(Qt::Orientation orientation, QWidget *parent = nullptr) : QSlider(orientation, parent)
    {
    }

    void setRedTickValue(int value)
    {
        m_tickValue = value;
        update();
    }

  protected:
    void paintEvent(QPaintEvent *event) override
    {
        QSlider::paintEvent(event);
        if (m_tickValue >= minimum() && m_tickValue <= maximum() && maximum() > minimum())
        {
            QPainter p(this);
            p.setRenderHint(QPainter::Antialiasing, true);
            const int handleRadius = 5;
            const qreal trackStart = handleRadius;
            const qreal trackLength = width() - 2 * handleRadius;
            const qreal ratio = qreal(m_tickValue - minimum()) / (maximum() - minimum());
            const qreal cx = trackStart + ratio * trackLength;
            const qreal cy = 3.5;
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(QStringLiteral("#ed1c24")));
            p.drawEllipse(QPointF(cx, cy), 2.0, 2.0);
        }
    }

  private:
    int m_tickValue = -1;
};

class HoverPopupFilter : public QObject
{
  public:
    HoverPopupFilter(std::function<void()> onHover, QObject *parent = nullptr)
        : QObject(parent), m_onHover(std::move(onHover))
    {
        m_timer.setSingleShot(true);
        m_timer.setInterval(300);
        connect(&m_timer, &QTimer::timeout, this, [this] {
            if (m_onHover)
                m_onHover();
        });
    }

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Enter)
        {
            m_timer.start();
        }
        else if (event->type() == QEvent::Leave || event->type() == QEvent::MouseButtonPress)
        {
            m_timer.stop();
        }
        return QObject::eventFilter(watched, event);
    }

  private:
    std::function<void()> m_onHover;
    QTimer m_timer;
};

} // namespace snipaste::ui::annotation
