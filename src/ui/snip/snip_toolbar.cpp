#include "ui/snip/snip_overlay.h"
#include "ui/snip/capture_icons.h"
#include "ui/annotation/annotation_dialog.h"
#include "platform/keyboard.h"

#include <QButtonGroup>
#include <QColorDialog>
#include <QComboBox>
#include <QCursor>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QInputDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QRegion>
#include <QScreen>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStackedLayout>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace snipaste::ui {

namespace {

QIcon makeColorSwatchIcon(const QColor &color)
{
    QPixmap pix(22, 22);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QColor(160, 160, 160));
    p.setBrush(color);
    p.drawRect(QRectF(1, 1, 20, 20));
    QPolygonF tri{QPointF(21, 13), QPointF(21, 21), QPointF(13, 21)};
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(237, 28, 36));
    p.drawPolygon(tri);
    QPolygonF tri2{QPointF(21, 16), QPointF(21, 21), QPointF(16, 21)};
    p.setBrush(QColor(255, 242, 0));
    p.drawPolygon(tri2);
    QPolygonF tri3{QPointF(21, 19), QPointF(21, 21), QPointF(19, 21)};
    p.setBrush(QColor(0, 162, 232));
    p.drawPolygon(tri3);
    p.end();
    return QIcon(pix);
}

QIcon makeDynamicWidthIcon(qreal width, bool checked = true)
{
    QIcon icon;
    for (int size : {20, 24, 32}) {
        for (const auto state : {QIcon::Off, QIcon::On}) {
            QPixmap pix(size, size);
            pix.fill(Qt::transparent);
            QPainter p(&pix);
            p.setRenderHint(QPainter::Antialiasing, true);
            const QPointF center(size / 2.0, size / 2.0);
            qreal r;
            if (width <= 2.0) {
                r = 0.8 + width * 0.45;
            } else if (width <= 8.0) {
                r = 1.7 + (width - 2.0) * 0.4;
            } else if (width <= 24.0) {
                r = 4.1 + (width - 8.0) * 0.22;
            } else {
                r = qMin(10.0, 7.6 + (width - 24.0) * 0.06);
            }
            const bool isWhite = (state == QIcon::On) || checked;
            const QColor fill = isWhite ? Qt::white : QColor(QStringLiteral("#52616b"));
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
            p.drawEllipse(center, r, r);
            p.end();
            icon.addPixmap(pix, QIcon::Normal, state);
        }
    }
    return icon;
}

QIcon makeLineStyleDropdownIcon(Qt::PenStyle style, int w = 52, int h = 22)
{
    QPixmap pix(w, h);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);

    QPen pen(QColor(QStringLiteral("#52616b")), 2.5);
    if (style == Qt::DashLine) {
        pen.setStyle(Qt::CustomDashLine);
        pen.setDashPattern({4, 3});
    } else if (style == Qt::DotLine) {
        pen.setStyle(Qt::CustomDashLine);
        pen.setDashPattern({1.5, 2.5});
    } else if (style == Qt::DashDotLine) {
        pen.setStyle(Qt::CustomDashLine);
        pen.setDashPattern({4, 2.5, 1.5, 2.5});
    } else {
        pen.setStyle(Qt::SolidLine);
    }
    p.setPen(pen);
    p.drawLine(QPointF(6, h / 2.0), QPointF(32, h / 2.0));

    p.setPen(QPen(QColor(QStringLiteral("#666666")), 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    QPolygonF chevron{QPointF(w - 14, h / 2.0 - 2), QPointF(w - 10, h / 2.0 + 2), QPointF(w - 6, h / 2.0 - 2)};
    p.drawPolyline(chevron);
    p.end();
    return QIcon(pix);
}

QIcon makeArrowStyleDropdownIcon(AnnotationCanvas::ArrowStyle style, int w = 52, int h = 22)
{
    QPixmap pix(w, h);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal midY = h / 2.0;
    const qreal startX = 6;
    const qreal endX = 32;

    p.setPen(QPen(QColor(QStringLiteral("#52616b")), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawLine(QPointF(startX, midY), QPointF(endX, midY));

    const auto drawHead = [&](const QPointF &tip, qreal dir, bool hollow) {
        QPainterPath path;
        path.moveTo(tip);
        path.lineTo(tip.x() - dir * 6, tip.y() - 4);
        path.lineTo(tip.x() - dir * 6, tip.y() + 4);
        path.closeSubpath();
        if (hollow) {
            p.setPen(QPen(QColor(QStringLiteral("#52616b")), 1.5));
            p.setBrush(Qt::white);
        } else {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(QStringLiteral("#52616b")));
        }
        p.drawPath(path);
    };

    switch (style) {
    case AnnotationCanvas::ArrowStyle::None:
        break;
    case AnnotationCanvas::ArrowStyle::Flat:
        p.setPen(QPen(QColor(QStringLiteral("#52616b")), 2.0));
        p.drawLine(QPointF(endX, midY - 4), QPointF(endX, midY + 4));
        break;
    case AnnotationCanvas::ArrowStyle::End:
        drawHead(QPointF(endX, midY), 1.0, false);
        break;
    case AnnotationCanvas::ArrowStyle::Start:
        drawHead(QPointF(startX, midY), -1.0, false);
        break;
    case AnnotationCanvas::ArrowStyle::Both:
        drawHead(QPointF(startX, midY), -1.0, false);
        drawHead(QPointF(endX, midY), 1.0, false);
        break;
    case AnnotationCanvas::ArrowStyle::HollowEnd:
        drawHead(QPointF(endX, midY), 1.0, true);
        break;
    case AnnotationCanvas::ArrowStyle::HollowStart:
        drawHead(QPointF(startX, midY), -1.0, true);
        break;
    case AnnotationCanvas::ArrowStyle::HollowBoth:
        drawHead(QPointF(startX, midY), -1.0, true);
        drawHead(QPointF(endX, midY), 1.0, true);
        break;
    }

    p.setPen(QPen(QColor(QStringLiteral("#666666")), 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    QPolygonF chevron{QPointF(w - 14, midY - 2), QPointF(w - 10, midY + 2), QPointF(w - 6, midY - 2)};
    p.drawPolyline(chevron);
    p.end();
    return QIcon(pix);
}

QIcon makeCurveStyleDropdownIcon(int curveStyle, int w = 48, int h = 22)
{
    QPixmap pix(w, h);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal midY = h / 2.0;
    p.setPen(QPen(QColor(QStringLiteral("#52616b")), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (curveStyle == 1) {
        QPainterPath path;
        path.moveTo(6, midY + 3);
        path.quadTo(18, midY - 6, 30, midY + 3);
        p.drawPath(path);
    } else {
        p.drawLine(QPointF(6, midY), QPointF(30, midY));
    }

    p.setPen(QPen(QColor(QStringLiteral("#666666")), 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    QPolygonF chevron{QPointF(w - 13, midY - 2), QPointF(w - 9, midY + 2), QPointF(w - 5, midY - 2)};
    p.drawPolyline(chevron);
    p.end();
    return QIcon(pix);
}

QIcon makeLineMenuItemIcon(Qt::PenStyle style, int w = 64, int h = 18)
{
    QPixmap pix(w, h);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor(QStringLiteral("#333333")), 2.5);
    if (style == Qt::DashLine) {
        pen.setStyle(Qt::CustomDashLine);
        pen.setDashPattern({4, 3});
    } else if (style == Qt::DotLine) {
        pen.setStyle(Qt::CustomDashLine);
        pen.setDashPattern({1.5, 2.5});
    } else if (style == Qt::DashDotLine) {
        pen.setStyle(Qt::CustomDashLine);
        pen.setDashPattern({4, 2.5, 1.5, 2.5});
    } else {
        pen.setStyle(Qt::SolidLine);
    }
    p.setPen(pen);
    p.drawLine(QPointF(6, h / 2.0), QPointF(w - 6, h / 2.0));
    p.end();
    return QIcon(pix);
}

QIcon makeArrowMenuItemIcon(AnnotationCanvas::ArrowStyle style, int w = 64, int h = 18)
{
    QPixmap pix(w, h);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal midY = h / 2.0;
    const qreal startX = 6;
    const qreal endX = w - 6;

    p.setPen(QPen(QColor(QStringLiteral("#333333")), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawLine(QPointF(startX, midY), QPointF(endX, midY));

    const auto drawHead = [&](const QPointF &tip, qreal dir, bool hollow) {
        QPainterPath path;
        path.moveTo(tip);
        path.lineTo(tip.x() - dir * 6, tip.y() - 4);
        path.lineTo(tip.x() - dir * 6, tip.y() + 4);
        path.closeSubpath();
        if (hollow) {
            p.setPen(QPen(QColor(QStringLiteral("#333333")), 1.5));
            p.setBrush(Qt::white);
        } else {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(QStringLiteral("#333333")));
        }
        p.drawPath(path);
    };

    switch (style) {
    case AnnotationCanvas::ArrowStyle::None:
        break;
    case AnnotationCanvas::ArrowStyle::Flat:
        p.setPen(QPen(QColor(QStringLiteral("#333333")), 2.0));
        p.drawLine(QPointF(endX, midY - 4), QPointF(endX, midY + 4));
        break;
    case AnnotationCanvas::ArrowStyle::End:
        drawHead(QPointF(endX, midY), 1.0, false);
        break;
    case AnnotationCanvas::ArrowStyle::Start:
        drawHead(QPointF(startX, midY), -1.0, false);
        break;
    case AnnotationCanvas::ArrowStyle::Both:
        drawHead(QPointF(startX, midY), -1.0, false);
        drawHead(QPointF(endX, midY), 1.0, false);
        break;
    case AnnotationCanvas::ArrowStyle::HollowEnd:
        drawHead(QPointF(endX, midY), 1.0, true);
        break;
    case AnnotationCanvas::ArrowStyle::HollowStart:
        drawHead(QPointF(startX, midY), -1.0, true);
        break;
    case AnnotationCanvas::ArrowStyle::HollowBoth:
        drawHead(QPointF(startX, midY), -1.0, true);
        drawHead(QPointF(endX, midY), 1.0, true);
        break;
    }
    p.end();
    return QIcon(pix);
}

QIcon makeMagnifierConnectorDropdownIcon(int mode, int w = 48, int h = 22)
{
    QPixmap pix(w, h);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal midY = h / 2.0;
    p.setPen(QPen(QColor(QStringLiteral("#52616b")), 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(5.5, midY - 4.5, 9, 9));

    if (mode == 0) {
        p.drawLine(QPointF(14.5, midY), QPointF(28, midY));
    } else if (mode == 1) {
        p.drawLine(QPointF(14.5, midY), QPointF(21, midY));
        p.drawLine(QPointF(21, midY), QPointF(21, midY + 4));
        p.drawLine(QPointF(21, midY + 4), QPointF(28, midY + 4));
    }

    p.setPen(QPen(QColor(QStringLiteral("#666666")), 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    QPolygonF chevron{QPointF(w - 13, midY - 2), QPointF(w - 9, midY + 2), QPointF(w - 5, midY - 2)};
    p.drawPolyline(chevron);
    p.end();
    return QIcon(pix);
}

QIcon makeMagnifierConnectorMenuItemIcon(int mode, int w = 44, int h = 18)
{
    QPixmap pix(w, h);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal midY = h / 2.0;
    p.setPen(QPen(QColor(QStringLiteral("#52616b")), 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(3.5, midY - 4.5, 9, 9));

    if (mode == 0) {
        p.drawLine(QPointF(12.5, midY), QPointF(38, midY));
    } else if (mode == 1) {
        p.drawLine(QPointF(12.5, midY), QPointF(25, midY));
        p.drawLine(QPointF(25, midY), QPointF(25, midY + 4));
        p.drawLine(QPointF(25, midY + 4), QPointF(38, midY + 4));
    }
    p.end();
    return QIcon(pix);
}

class TickSlider : public QSlider {
public:
    explicit TickSlider(Qt::Orientation orientation, QWidget *parent = nullptr)
        : QSlider(orientation, parent)
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
        if (m_tickValue >= minimum() && m_tickValue <= maximum() && maximum() > minimum()) {
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

class SubtoolbarStackedLayout : public QStackedLayout {
public:
    explicit SubtoolbarStackedLayout(QWidget *parent = nullptr) : QStackedLayout(parent) {
        setSizeConstraint(QLayout::SetFixedSize);
        setContentsMargins(0, 0, 0, 0);
        setSpacing(0);
    }

    QSize sizeHint() const override {
        if (QWidget *w = currentWidget()) {
            return w->sizeHint();
        }
        return QStackedLayout::sizeHint();
    }

    QSize minimumSize() const override {
        return sizeHint();
    }
};

class HoverPopupFilter : public QObject {
public:
    HoverPopupFilter(std::function<void()> onHover, QObject *parent = nullptr)
        : QObject(parent), m_onHover(std::move(onHover)) {
        m_timer.setSingleShot(true);
        m_timer.setInterval(300);
        connect(&m_timer, &QTimer::timeout, this, [this] {
            if (m_onHover) m_onHover();
        });
    }
protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (event->type() == QEvent::Enter) {
            m_timer.start();
        } else if (event->type() == QEvent::Leave || event->type() == QEvent::MouseButtonPress) {
            m_timer.stop();
        }
        return QObject::eventFilter(watched, event);
    }
private:
    std::function<void()> m_onHover;
    QTimer m_timer;
};

} // namespace

void SnipOverlay::updateDynamicWidthButtons(qreal width)
{
    QWidget *activePage = m_toolOptionsStack ? m_toolOptionsStack->currentWidget() : nullptr;
    if (activePage) {
        QToolButton *checkedBtn = nullptr;
        for (auto *b : m_dynamicWidthButtons) {
            if (b && b->parentWidget() == activePage && b->isChecked()) {
                checkedBtn = b;
                break;
            }
        }
        if (checkedBtn) {
            checkedBtn->setProperty("presetWidth", width);
            checkedBtn->setIcon(makeDynamicWidthIcon(width, true));
            checkedBtn->setToolTip(QStringLiteral("画笔粗细: %1 px (滚轮可微调)").arg(qRound(width)));
        } else {
            for (auto *b : m_dynamicWidthButtons) {
                if (b && b->parentWidget() == activePage) {
                    QSignalBlocker blocker(b);
                    b->setChecked(true);
                    b->setProperty("presetWidth", width);
                    b->setIcon(makeDynamicWidthIcon(width, true));
                    b->setToolTip(QStringLiteral("画笔粗细: %1 px (滚轮可微调)").arg(qRound(width)));
                    break;
                }
            }
        }
    }
    refreshAnnotationCursor();
}

void SnipOverlay::showCornerRadiusPopup(QWidget *sourceBtn)
{
    if (!sourceBtn) return;
    if (m_cornerRadiusPopup && m_cornerRadiusPopup->isVisible()) {
        if (m_cornerRadiusPopup->property("sourceBtn").value<QWidget *>() == sourceBtn) {
            m_cornerRadiusPopup->close();
            return;
        }
    }
    if (m_cornerRadiusPopup) {
        m_cornerRadiusPopup->close();
    }
    auto *popup = new QFrame(this, Qt::Popup | Qt::FramelessWindowHint);
    popup->setAttribute(Qt::WA_DeleteOnClose);
    popup->setProperty("sourceBtn", QVariant::fromValue(sourceBtn));
    popup->setStyleSheet(QStringLiteral(
        "QFrame { background: #ffffff; border: 1px solid #2889ed; border-radius: 4px; }"
        "QLabel { color: #222222; font-size: 12px; }"
        "QSlider::groove:horizontal { height: 3px; background: #c4c9cf; border-radius: 1px; }"
        "QSlider::sub-page:horizontal { background: #2889ed; border-radius: 1px; }"
        "QSlider::add-page:horizontal { background: #c4c9cf; border-radius: 1px; }"
        "QSlider::handle:horizontal { width: 10px; height: 10px; margin: -4px 0; border-radius: 5px; background: white; border: 2px solid #2889ed; }"
        "QSpinBox { border: 1px solid #c4c9cf; border-radius: 2px; padding: 1px 3px; font-size: 11px; }"));
    auto *h = new QHBoxLayout(popup);
    h->setContentsMargins(6, 4, 6, 4);
    h->setSpacing(6);
    auto *label = new QLabel(QStringLiteral("圆角半径:"), popup);
    auto *slider = new QSlider(Qt::Horizontal, popup);
    slider->setRange(0, 50);
    slider->setValue(m_canvas ? qRound(m_canvas->cornerRadius()) : 0);
    slider->setFixedWidth(80);
    auto *spin = new QSpinBox(popup);
    spin->setRange(0, 50);
    spin->setValue(slider->value());
    spin->setFixedWidth(44);
    h->addWidget(label);
    h->addWidget(slider);
    h->addWidget(spin);
    connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
    connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), slider, &QSlider::setValue);
    connect(slider, &QSlider::valueChanged, this, [this](int v) {
        if (!m_canvas) return;
        m_canvas->setCornerRadius(v);
        if (m_canvas->tool() != AnnotationCanvas::Tool::Magnifier) {
            if (v > 0) {
                m_canvas->setTool(AnnotationCanvas::Tool::RoundedRectangle);
            } else {
                m_canvas->setTool(AnnotationCanvas::Tool::Rectangle);
            }
        }
    });
    popup->adjustSize();
    QPoint pos = sourceBtn->mapToGlobal(QPoint(0, sourceBtn->height() + 2));
    if (screen()) {
        const QRect screenGeom = screen()->geometry();
        if (pos.x() + popup->width() > screenGeom.right()) {
            pos.setX(screenGeom.right() - popup->width() - 2);
        }
        if (pos.y() + popup->height() > screenGeom.bottom()) {
            pos.setY(sourceBtn->mapToGlobal(QPoint(0, -popup->height() - 2)).y());
        }
    }
    popup->move(pos);
    m_cornerRadiusPopup = popup;
    popup->show();
}

void SnipOverlay::updateAllColorWatchers(const QColor &color)
{
    m_currentAnnotationColor = color;
    const QIcon icon = makeColorSwatchIcon(color);
    for (auto *w : m_colorWatchers) {
        if (w) w->setIcon(icon);
    }
    const QString colorHex = color.name(QColor::HexRgb).toLower();
    for (const auto &pair : m_paletteButtons) {
        auto *btn = pair.first;
        if (!btn) continue;
        const bool shouldBeChecked = (pair.second.compare(colorHex, Qt::CaseInsensitive) == 0);
        if (btn->isChecked() != shouldBeChecked) {
            QSignalBlocker blocker(btn);
            btn->setChecked(shouldBeChecked);
        }
    }
}

void SnipOverlay::createToolbar()
{
    if (m_toolbar)
        return;
    m_toolbar = new QFrame(this);
    m_toolbar->setObjectName(QStringLiteral("captureToolbar"));
    m_toolbar->setCursor(Qt::ArrowCursor);
    m_toolbar->setStyleSheet(QStringLiteral(
        "QFrame#captureToolbar { background: transparent; border: none; }"
        "QFrame#captureActions, QFrame#captureOptions { background: #ffffff; border: 1px solid #2889ed; }"
        "QToolButton { background: transparent; border: none; border-radius: 0; padding: 0; }"
        "QToolButton:hover { background: #d6e8fa; }"
        "QToolButton:checked { background: #2889ed; color: white; }"
        "QToolButton::menu-indicator { image: none; width: 0; }"
        "QSlider { background: transparent; height: 24px; }"
        "QSlider::groove:horizontal { height: 3px; background: #c4c9cf; border-radius: 1px; }"
        "QSlider::sub-page:horizontal { background: #2889ed; border-radius: 1px; }"
        "QSlider::add-page:horizontal { background: #c4c9cf; border-radius: 1px; }"
        "QSlider::handle:horizontal { width: 10px; height: 10px; margin: -4px 0; border-radius: 5px; background: #ffffff; border: 2px solid #2889ed; }"
        "QSlider::handle:horizontal:hover { border: 2px solid #1874cd; }"
        "QComboBox { border: 1px solid #c4c9cf; border-radius: 1px; padding: 1px 3px; font-size: 11px; background: white; }"
        "QComboBox::drop-down { width: 14px; border: none; }"));
    auto *stack = new QVBoxLayout(m_toolbar);
    stack->setContentsMargins(0, 0, 0, 0);
    stack->setSpacing(1);
    auto *actions = new QFrame(m_toolbar);
    actions->setObjectName(QStringLiteral("captureActions"));
    auto *row = new QHBoxLayout(actions);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    stack->addWidget(actions);

    m_toolOptions = new QFrame(m_toolbar);
    m_toolOptions->setObjectName(QStringLiteral("captureOptions"));
    auto *optionsStack = new SubtoolbarStackedLayout(m_toolOptions);
    optionsStack->setContentsMargins(0, 0, 0, 0);
    optionsStack->setSpacing(0);
    m_toolOptionsStack = optionsStack;
    stack->addWidget(m_toolOptions, 0, Qt::AlignLeft);

    const qreal compact = qMin<qreal>(1, (width() - 20) / 572.0);
    const auto button = [row, actions, compact](CaptureIcon icon, const QString &name, const QString &tip) {
        auto *b = new QToolButton(actions);
        b->setObjectName(name);
        b->setAccessibleName(name);
        b->setIcon(captureIcon(icon));
        b->setIconSize(QSize(16, 16));
        b->setFixedSize(qMax(16, int(32 * compact)), 30);
        b->setFocusPolicy(Qt::NoFocus);
        b->setToolTip(tip);
        row->addWidget(b);
        return b;
    };
    const auto separator = [actions, row] {
        auto *line = new QFrame(actions);
        line->setFrameShape(QFrame::VLine);
        line->setStyleSheet(QStringLiteral("background: #c4c9cf;"));
        line->setFixedSize(1, 19);
        row->addSpacing(1);
        row->addWidget(line);
        row->addSpacing(1);
    };
    auto *toolGroup = new QButtonGroup(m_toolbar);
    toolGroup->setObjectName(QStringLiteral("annotationTools"));
    toolGroup->setExclusive(false);
    const auto tool = [this, button, toolGroup](CaptureIcon icon, const QString &name,
                                                AnnotationCanvas::Tool kind, int pageIndex) {
        auto *b = button(icon, name, name);
        b->setProperty("annotationTool", int(kind));
        b->setProperty("pageIndex", pageIndex);
        b->setCheckable(true);
        toolGroup->addButton(b);
        connect(b, &QToolButton::clicked, this, [this, b, toolGroup, kind, pageIndex](bool checked) {
            if (!m_canvas)
                return;
            for (auto *other : toolGroup->buttons()) {
                if (other != b) { QSignalBlocker blocker(other); other->setChecked(false); }
            }
            if (checked) {
                m_canvas->setAttribute(Qt::WA_TransparentForMouseEvents, false);
                const auto toolKind = static_cast<AnnotationCanvas::Tool>(
                    b->property("annotationTool").isValid() ? b->property("annotationTool").toInt() : int(kind));
                m_toolOptionsStack->setCurrentIndex(pageIndex);
                m_canvas->setTool(toolKind);
                refreshAnnotationCursor();
                QTimer::singleShot(0, this, [this] { refreshAnnotationCursor(); });
                if (QWidget *w = m_toolOptionsStack->currentWidget()) {
                    QSize s = w->sizeHint();
                    m_toolOptions->setFixedSize(s.width() + 2, s.height() + 2);
                    m_toolOptions->updateGeometry();
                }
                m_toolOptions->show();
                m_toolbar->adjustSize();
            } else {
                m_canvas->setAttribute(Qt::WA_TransparentForMouseEvents, true);
                m_toolOptions->hide();
            }
            updateToolbarGeometry();
            setFocus();
        });
        return b;
    };

    auto *shapes = tool(CaptureIcon::Shapes, QStringLiteral("形状"), AnnotationCanvas::Tool::Rectangle, 0);
    auto *lines = tool(CaptureIcon::Line, QStringLiteral("线条"), AnnotationCanvas::Tool::Line, 1);
    const auto choices = [this](QToolButton *b,
                                const QList<QPair<QString, AnnotationCanvas::Tool>> &items) {
        auto *menu = new QMenu(b);
        for (const auto &item : items) {
            auto *action = menu->addAction(item.first);
            action->setObjectName(item.first);
            connect(action, &QAction::triggered, this, [b, kind = item.second] {
                b->setProperty("annotationTool", int(kind));
                b->setChecked(false);
                b->click();
            });
        }
        b->setMenu(menu);
        b->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(b, &QWidget::customContextMenuRequested, this,
                [b, menu] { menu->popup(b->mapToGlobal(QPoint(0, b->height()))); });
    };
    choices(shapes, {{QStringLiteral("矩形"), AnnotationCanvas::Tool::Rectangle},
                     {QStringLiteral("椭圆"), AnnotationCanvas::Tool::Ellipse},
                     {QStringLiteral("圆角矩形"), AnnotationCanvas::Tool::RoundedRectangle}});
    shapes->setToolTip(QStringLiteral("形状（右键选择矩形、椭圆、圆角矩形）"));
    choices(lines, {{QStringLiteral("直线"), AnnotationCanvas::Tool::Line},
                    {QStringLiteral("箭头"), AnnotationCanvas::Tool::Arrow}});
    auto *arrowStyles = lines->menu()->addMenu(QStringLiteral("箭头样式"));
    const auto addArrowStyle = [this, arrowStyles](const QString &label,
                                                   AnnotationCanvas::ArrowStyle style) {
        auto *action = arrowStyles->addAction(label);
        connect(action, &QAction::triggered, this, [this, style] {
            if (m_canvas)
                m_canvas->setArrowStyle(style);
        });
    };
    addArrowStyle(QStringLiteral("末端箭头"), AnnotationCanvas::ArrowStyle::End);
    addArrowStyle(QStringLiteral("起点箭头"), AnnotationCanvas::ArrowStyle::Start);
    addArrowStyle(QStringLiteral("双向箭头"), AnnotationCanvas::ArrowStyle::Both);
    addArrowStyle(QStringLiteral("空心末端"), AnnotationCanvas::ArrowStyle::HollowEnd);
    addArrowStyle(QStringLiteral("空心双向"), AnnotationCanvas::ArrowStyle::HollowBoth);
    lines->setToolTip(QStringLiteral("线条（右键选择直线、箭头；Tab 切换）"));

    tool(CaptureIcon::Pen, QStringLiteral("画笔"), AnnotationCanvas::Tool::Pen, 2);
    tool(CaptureIcon::Marker, QStringLiteral("荧光笔"), AnnotationCanvas::Tool::Marker, 3);
    tool(CaptureIcon::Mosaic, QStringLiteral("马赛克"), AnnotationCanvas::Tool::Mosaic, 4);
    auto *textTool = tool(CaptureIcon::Text, QStringLiteral("文字"), AnnotationCanvas::Tool::Text, 5);
    textTool->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(textTool, &QWidget::customContextMenuRequested, this,
            [this, textTool](const QPoint &position) {
                QMenu menu;
                QAction *disable = menu.addAction(QStringLiteral("无描边"));
                QAction *colorAction = menu.addAction(QStringLiteral("描边颜色…"));
                QAction *widthAction = menu.addAction(QStringLiteral("描边宽度…"));
                QAction *chosen = menu.exec(textTool->mapToGlobal(position));
                if (!m_canvas)
                    return;
                if (chosen == disable) {
                    m_canvas->setTextOutlineWidth(0.0);
                } else if (chosen == colorAction) {
                    const QColor color = QColorDialog::getColor(m_canvas->textOutlineColor(),
                                                                 this,
                                                                 QStringLiteral("文字描边颜色"),
                                                                 QColorDialog::ShowAlphaChannel);
                    if (color.isValid()) {
                        m_canvas->setTextOutlineColor(color);
                        if (m_canvas->textOutlineWidth() <= 0.0)
                            m_canvas->setTextOutlineWidth(1.0);
                    }
                } else if (chosen == widthAction) {
                    bool ok = false;
                    const double width = QInputDialog::getDouble(
                        this, QStringLiteral("文字描边宽度"), QStringLiteral("宽度 (px):"),
                        m_canvas->textOutlineWidth(), 0.0, 16.0, 1, &ok);
                    if (ok)
                        m_canvas->setTextOutlineWidth(width);
                }
            });
    tool(CaptureIcon::Number, QStringLiteral("序号"), AnnotationCanvas::Tool::Number, 6);
    tool(CaptureIcon::Magnifier, QStringLiteral("放大镜标注"), AnnotationCanvas::Tool::Magnifier, 7);
    tool(CaptureIcon::Eraser, QStringLiteral("橡皮擦"), AnnotationCanvas::Tool::Eraser, 8);
    separator();
    connect(button(CaptureIcon::Ocr, QStringLiteral("文字识别"), QStringLiteral("识别选区文字")),
            &QToolButton::clicked, this, &SnipOverlay::ocrSelection);
    auto *undo = button(CaptureIcon::Undo, QStringLiteral("撤销"),
                        QStringLiteral("撤销 (%1)").arg(snipaste::platform::primaryShortcutText(QStringLiteral("Z"))));
    auto *redo = button(CaptureIcon::Redo, QStringLiteral("重做"),
                        QStringLiteral("重做 (%1)").arg(snipaste::platform::primaryShortcutText(QStringLiteral("Y"))));
    undo->setEnabled(false);
    redo->setEnabled(false);
    connect(undo, &QToolButton::clicked, this, [this] { if (m_canvas) m_canvas->undo(); });
    connect(redo, &QToolButton::clicked, this, [this] { if (m_canvas) m_canvas->redo(); });
    separator();
    connect(button(CaptureIcon::Cancel, QStringLiteral("取消"), QStringLiteral("取消截图 (Esc)")),
            &QToolButton::clicked, this, &SnipOverlay::close);
    auto *share = button(CaptureIcon::Share, QStringLiteral("分享"), QStringLiteral("分享图像"));
    connect(share, &QToolButton::clicked, this, [this, share] {
        showOutputMenu(share->mapToGlobal(QPoint(0, share->height())), true);
    });
    connect(button(CaptureIcon::Paste, QStringLiteral("贴图"),
                  QStringLiteral("贴到屏幕 (%1)").arg(snipaste::platform::primaryShortcutText(QStringLiteral("T")))),
            &QToolButton::clicked, this, [this] { pasteSelection(); });
    connect(button(CaptureIcon::Save, QStringLiteral("保存"),
                  QStringLiteral("保存 (%1)").arg(snipaste::platform::primaryShortcutText(QStringLiteral("S")))),
            &QToolButton::clicked, this, [this] { saveSelection(); });
    connect(button(CaptureIcon::Finish, QStringLiteral("完成"), QStringLiteral("复制到剪贴板 (Enter)")),
            &QToolButton::clicked, this, [this] { copySelection(); });
    m_outputOptionsButton = button(CaptureIcon::More, QStringLiteral("更多"), QStringLiteral("更多输出操作"));
    m_outputOptionsButton->setFixedWidth(16);
    m_outputOptionsButton->setVisible(!m_hideOutputOptions);
    connect(m_outputOptionsButton, &QToolButton::clicked, this, [this] {
        showOutputMenu(m_outputOptionsButton->mapToGlobal(QPoint(0, m_outputOptionsButton->height())));
    });

    m_colorWatchers.clear();
    m_paletteButtons.clear();
    m_dynamicWidthButtons.clear();
    m_toolbar->installEventFilter(this);
    m_toolOptions->installEventFilter(this);

    const auto makeWidthPreset = [this](QWidget *parent, int initialWidth, const QString &objName, bool isChecked = false) {
        auto *b = new QToolButton(parent);
        b->setObjectName(objName);
        b->setFixedSize(24, 24);
        b->setCheckable(true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        b->setProperty("presetWidth", double(initialWidth));
        b->setChecked(isChecked);
        b->setIcon(makeDynamicWidthIcon(initialWidth, isChecked));
        b->setIconSize(QSize(22, 22));
        b->setToolTip(QStringLiteral("画笔粗细: %1 px (滚轮可微调)").arg(initialWidth));
        m_dynamicWidthButtons.append(b);

        connect(b, &QToolButton::toggled, this, [b](bool checked) {
            const qreal w = b->property("presetWidth").toReal();
            b->setIcon(makeDynamicWidthIcon(w, checked));
            b->setToolTip(QStringLiteral("画笔粗细: %1 px (滚轮可微调)").arg(qRound(w)));
        });

        connect(b, &QToolButton::clicked, this, [this, b] {
            if (!m_canvas) return;
            const qreal w = b->property("presetWidth").toReal();
            m_canvas->setPenWidth(w);
            refreshAnnotationCursor();
        });

        return b;
    };

    const auto makeSeparator = [](QWidget *parent) {
        auto *line = new QFrame(parent);
        line->setFrameShape(QFrame::VLine);
        line->setStyleSheet(QStringLiteral("background: #c4c9cf;"));
        line->setFixedSize(1, 19);
        return line;
    };

    const QString dropdownBtnStyle = QStringLiteral(
        "QToolButton { background: #ffffff; border: 1px solid #c4c9cf; border-radius: 1px; }"
        "QToolButton:hover { border: 1px solid #2889ed; background: #f0f7fe; }"
        "QToolButton:pressed, QToolButton:open { border: 1px solid #2889ed; background: #e0effc; }");

    const auto makeLineStyleDropdown = [this, dropdownBtnStyle](QWidget *parent) {
        auto *btn = new QToolButton(parent);
        btn->setObjectName(QStringLiteral("annotationLineStyle"));
        btn->setStyleSheet(dropdownBtnStyle);
        btn->setFixedSize(54, 24);
        btn->setToolTip(QStringLiteral("线条样式（实线、虚线、点线、点划线）"));
        btn->setPopupMode(QToolButton::InstantPopup);
        btn->setIcon(makeLineStyleDropdownIcon(Qt::SolidLine));
        btn->setIconSize(QSize(50, 20));
        m_lineStyleButton = btn;

        auto *menu = new QMenu(btn);
        menu->setStyleSheet(QStringLiteral("QMenu { background: white; border: 1px solid #c4c9cf; padding: 2px; }"
                                           "QMenu::item { padding: 4px 8px; }"
                                           "QMenu::item:selected { background: #d6e8fa; }"));
        const QList<QPair<QString, Qt::PenStyle>> styles{
            {QStringLiteral("实线"), Qt::SolidLine},
            {QStringLiteral("虚线"), Qt::DashLine},
            {QStringLiteral("点线"), Qt::DotLine},
            {QStringLiteral("点划线"), Qt::DashDotLine},
        };
        for (const auto &s : styles) {
            auto *action = menu->addAction(makeLineMenuItemIcon(s.second), QString());
            action->setCheckable(true);
            if (s.second == Qt::SolidLine) action->setChecked(true);
            connect(action, &QAction::triggered, this, [this, btn, menu, action, s] {
                if (m_canvas) m_canvas->setPenStyle(s.second);
                btn->setIcon(makeLineStyleDropdownIcon(s.second));
                for (auto *other : menu->actions()) other->setChecked(other == action);
            });
        }
        btn->setMenu(menu);
        return btn;
    };

    const auto makeColorModule = [this](QWidget *parent) {
        auto *container = new QWidget(parent);
        container->setFixedSize(146, 24);
        container->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        auto *layout = new QHBoxLayout(container);
        layout->setContentsMargins(1, 0, 1, 0);
        layout->setSpacing(2);

        auto *currentBtn = new QToolButton(container);
        currentBtn->setFixedSize(22, 22);
        currentBtn->setToolTip(QStringLiteral("当前颜色（点击自定义）"));
        currentBtn->setIcon(makeColorSwatchIcon(m_currentAnnotationColor));
        currentBtn->setIconSize(QSize(20, 20));
        currentBtn->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(currentBtn);
        m_colorWatchers.append(currentBtn);

        auto *gridWidget = new QWidget(container);
        gridWidget->setFixedSize(119, 23);
        auto *grid = new QGridLayout(gridWidget);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setSpacing(1);

        const QStringList row0 = {
            QStringLiteral("#000000"), QStringLiteral("#7f7f7f"), QStringLiteral("#880015"), QStringLiteral("#ed1c24"), QStringLiteral("#ff7f27"),
            QStringLiteral("#fff200"), QStringLiteral("#22b14c"), QStringLiteral("#00a2e8"), QStringLiteral("#3f48cc"), QStringLiteral("#a349a4")
        };
        const QStringList row1 = {
            QStringLiteral("#ffffff"), QStringLiteral("#c3c3c3"), QStringLiteral("#b97a57"), QStringLiteral("#ffaec9"), QStringLiteral("#ffc90e"),
            QStringLiteral("#efe4b0"), QStringLiteral("#b5e61d"), QStringLiteral("#99d9ea"), QStringLiteral("#7092be"), QStringLiteral("#c8bfe7")
        };

        auto addRow = [&](const QStringList &colors, int rowIdx) {
            for (int col = 0; col < colors.size(); ++col) {
                const QString hex = colors.at(col);
                auto *b = new QToolButton(gridWidget);
                b->setObjectName(QStringLiteral("annotationColor_%1").arg(hex.mid(1)));
                b->setFixedSize(11, 11);
                b->setCheckable(true);
                b->setStyleSheet(QStringLiteral(
                    "QToolButton { background: %1; border: 1px solid #777777; }"
                    "QToolButton:hover { border: 1px solid #2889ed; }"
                    "QToolButton:checked { background: %1; border: 2px solid #2889ed; outline: 1px solid white; }").arg(hex));
                const bool isCur = (hex.compare(m_currentAnnotationColor.name(QColor::HexRgb), Qt::CaseInsensitive) == 0);
                if (isCur) {
                    b->setChecked(true);
                }
                b->setToolTip(hex);
                b->setFocusPolicy(Qt::NoFocus);
                grid->addWidget(b, rowIdx, col);
                m_paletteButtons.append({b, hex});
                connect(b, &QToolButton::clicked, this, [this, hex] {
                    const QColor c(hex);
                    m_currentAnnotationColor = c;
                    if (m_canvas) {
                        m_canvas->setPenColor(c);
                        refreshAnnotationCursor();
                    } else {
                        updateAllColorWatchers(c);
                    }
                });
            }
        };
        addRow(row0, 0);
        addRow(row1, 1);
        layout->addWidget(gridWidget);

        auto *customBtn = new QToolButton(container);
        customBtn->setObjectName(QStringLiteral("annotationCustomColor"));
        customBtn->setText(QStringLiteral("+"));
        customBtn->setFixedSize(0, 0);
        customBtn->setVisible(false);
        layout->addWidget(customBtn);

        const auto pickCustom = [this] {
            const QColor cur = m_canvas ? m_canvas->penColor() : m_currentAnnotationColor;
            const QColor chosen = QColorDialog::getColor(cur, this, QStringLiteral("选择标注颜色"), QColorDialog::ShowAlphaChannel);
            if (chosen.isValid()) {
                m_currentAnnotationColor = chosen;
                if (m_canvas) {
                    m_canvas->setPenColor(chosen);
                    refreshAnnotationCursor();
                }
                updateAllColorWatchers(chosen);
            }
        };
        connect(currentBtn, &QToolButton::clicked, this, pickCustom);
        connect(customBtn, &QToolButton::clicked, this, pickCustom);

        return container;
    };

    // ----------------------------------------------------
    // Page 0: Shapes (形状) - 09-交互-01.png
    // ----------------------------------------------------
    auto *pageShapes = new QWidget(m_toolOptions);
    auto *layoutShapes = new QHBoxLayout(pageShapes);
    layoutShapes->setSizeConstraint(QLayout::SetFixedSize);
    layoutShapes->setContentsMargins(0, 0, 0, 0);
    layoutShapes->setSpacing(2);

    auto *shapeWidthGroup = new QButtonGroup(pageShapes);
    shapeWidthGroup->setExclusive(true);
    for (int w : {2, 4, 8}) {
        auto *b = makeWidthPreset(pageShapes, w, QStringLiteral("penWidth%1").arg(w), w == 2);
        shapeWidthGroup->addButton(b);
        layoutShapes->addWidget(b);
        if (w == 2) b->setChecked(true);
    }

    auto *btnFill = new QToolButton(pageShapes);
    btnFill->setIcon(captureIcon(CaptureIcon::FillPattern));
    btnFill->setFixedSize(24, 24);
    btnFill->setCheckable(true);
    btnFill->setToolTip(QStringLiteral("填充图形"));
    btnFill->setFocusPolicy(Qt::NoFocus);
    layoutShapes->addWidget(btnFill);
    connect(btnFill, &QToolButton::toggled, this, [this](bool f) {
        if (m_canvas) m_canvas->setShapeFilled(f);
    });

    layoutShapes->addWidget(makeSeparator(pageShapes));

    auto *shapeModeGroup = new QButtonGroup(pageShapes);
    shapeModeGroup->setExclusive(true);
    auto *btnRect = new QToolButton(pageShapes);
    btnRect->setIcon(captureIcon(CaptureIcon::Rectangle));
    btnRect->setFixedSize(24, 24);
    btnRect->setCheckable(true);
    btnRect->setChecked(true);
    btnRect->setToolTip(QStringLiteral("矩形（点击或悬停设置圆角半径）"));
    btnRect->setFocusPolicy(Qt::NoFocus);
    shapeModeGroup->addButton(btnRect);
    layoutShapes->addWidget(btnRect);

    auto *btnEllipse = new QToolButton(pageShapes);
    btnEllipse->setIcon(captureIcon(CaptureIcon::Ellipse));
    btnEllipse->setFixedSize(24, 24);
    btnEllipse->setCheckable(true);
    btnEllipse->setToolTip(QStringLiteral("椭圆"));
    btnEllipse->setFocusPolicy(Qt::NoFocus);
    shapeModeGroup->addButton(btnEllipse);
    layoutShapes->addWidget(btnEllipse);

    btnRect->installEventFilter(new HoverPopupFilter([this, btnRect] {
        if (btnRect && btnRect->underMouse()) {
            showCornerRadiusPopup(btnRect);
        }
    }, btnRect));
    connect(btnRect, &QToolButton::clicked, this, [this, btnRect] {
        if (!m_canvas) return;
        m_canvas->setTool(m_canvas->cornerRadius() > 0 ? AnnotationCanvas::Tool::RoundedRectangle : AnnotationCanvas::Tool::Rectangle);
        refreshAnnotationCursor();
        showCornerRadiusPopup(btnRect);
    });
    connect(btnEllipse, &QToolButton::clicked, this, [this] {
        if (m_cornerRadiusPopup) {
            m_cornerRadiusPopup->close();
        }
        if (!m_canvas) return;
        m_canvas->setTool(AnnotationCanvas::Tool::Ellipse);
        refreshAnnotationCursor();
    });

    layoutShapes->addWidget(makeSeparator(pageShapes));
    layoutShapes->addWidget(makeLineStyleDropdown(pageShapes));
    layoutShapes->addWidget(makeSeparator(pageShapes));
    layoutShapes->addWidget(makeColorModule(pageShapes));
    m_toolOptionsStack->addWidget(pageShapes);

    // ----------------------------------------------------
    // Page 1: Lines / Arrows (线条) - 09-交互-02.png
    // ----------------------------------------------------
    auto *pageLines = new QWidget(m_toolOptions);
    auto *layoutLines = new QHBoxLayout(pageLines);
    layoutLines->setSizeConstraint(QLayout::SetFixedSize);
    layoutLines->setContentsMargins(0, 0, 0, 0);
    layoutLines->setSpacing(2);

    auto *lineWidthGroup = new QButtonGroup(pageLines);
    lineWidthGroup->setExclusive(true);
    for (int w : {2, 4, 8}) {
        auto *b = makeWidthPreset(pageLines, w, QStringLiteral("penWidth%1").arg(w), w == 2);
        lineWidthGroup->addButton(b);
        layoutLines->addWidget(b);
        if (w == 2) b->setChecked(true);
    }

    layoutLines->addWidget(makeSeparator(pageLines));

    auto *btnArrowDropdown = new QToolButton(pageLines);
    btnArrowDropdown->setObjectName(QStringLiteral("annotationArrowStyle"));
    btnArrowDropdown->setStyleSheet(dropdownBtnStyle);
    btnArrowDropdown->setFixedSize(54, 24);
    btnArrowDropdown->setPopupMode(QToolButton::InstantPopup);
    btnArrowDropdown->setIcon(makeArrowStyleDropdownIcon(AnnotationCanvas::ArrowStyle::End));
    btnArrowDropdown->setIconSize(QSize(50, 20));
    btnArrowDropdown->setToolTip(QStringLiteral("箭头样式"));
    m_arrowStyleButton = btnArrowDropdown;

    auto *menuArrow = new QMenu(btnArrowDropdown);
    menuArrow->setStyleSheet(QStringLiteral("QMenu { background: white; border: 1px solid #c4c9cf; padding: 2px; }"
                                           "QMenu::item { padding: 4px 8px; }"
                                           "QMenu::item:selected { background: #d6e8fa; }"));
    const QList<QPair<QString, AnnotationCanvas::ArrowStyle>> arrowStyleItems{
        {QStringLiteral("无箭头"), AnnotationCanvas::ArrowStyle::None},
        {QStringLiteral("垂直扁平"), AnnotationCanvas::ArrowStyle::Flat},
        {QStringLiteral("末端实心"), AnnotationCanvas::ArrowStyle::End},
        {QStringLiteral("起点实心"), AnnotationCanvas::ArrowStyle::Start},
        {QStringLiteral("双向实心"), AnnotationCanvas::ArrowStyle::Both},
        {QStringLiteral("末端空心"), AnnotationCanvas::ArrowStyle::HollowEnd},
        {QStringLiteral("起点空心"), AnnotationCanvas::ArrowStyle::HollowStart},
        {QStringLiteral("双向空心"), AnnotationCanvas::ArrowStyle::HollowBoth},
    };
    for (const auto &item : arrowStyleItems) {
        auto *action = menuArrow->addAction(makeArrowMenuItemIcon(item.second), QString());
        action->setCheckable(true);
        if (item.second == AnnotationCanvas::ArrowStyle::End) action->setChecked(true);
        connect(action, &QAction::triggered, this, [this, btnArrowDropdown, menuArrow, action, s = item.second] {
            btnArrowDropdown->setIcon(makeArrowStyleDropdownIcon(s));
            for (auto *other : menuArrow->actions()) other->setChecked(other == action);
            if (m_canvas) {
                m_canvas->setArrowStyle(s);
                if (s == AnnotationCanvas::ArrowStyle::None)
                    m_canvas->setTool(AnnotationCanvas::Tool::Line);
                else
                    m_canvas->setTool(AnnotationCanvas::Tool::Arrow);
            }
        });
    }
    btnArrowDropdown->setMenu(menuArrow);
    layoutLines->addWidget(btnArrowDropdown);
    layoutLines->addWidget(makeSeparator(pageLines));

    layoutLines->addWidget(makeLineStyleDropdown(pageLines));
    layoutLines->addWidget(makeSeparator(pageLines));

    auto *btnCurve = new QToolButton(pageLines);
    btnCurve->setObjectName(QStringLiteral("annotationCurveStyle"));
    btnCurve->setStyleSheet(dropdownBtnStyle);
    btnCurve->setFixedSize(48, 24);
    btnCurve->setPopupMode(QToolButton::InstantPopup);
    btnCurve->setIcon(makeCurveStyleDropdownIcon(0));
    btnCurve->setIconSize(QSize(44, 20));
    btnCurve->setToolTip(QStringLiteral("直线 / 曲线"));
    m_curveStyleButton = btnCurve;

    auto *curveMenu = new QMenu(btnCurve);
    curveMenu->setStyleSheet(QStringLiteral("QMenu { background: white; border: 1px solid #c4c9cf; padding: 2px; }"
                                           "QMenu::item { padding: 4px 8px; }"
                                           "QMenu::item:selected { background: #d6e8fa; }"));
    auto *actLine = curveMenu->addAction(makeCurveStyleDropdownIcon(0, 48, 18), QString());
    auto *actCurve = curveMenu->addAction(makeCurveStyleDropdownIcon(1, 48, 18), QString());
    actLine->setCheckable(true);
    actLine->setChecked(true);
    actCurve->setCheckable(true);
    connect(actLine, &QAction::triggered, this, [btnCurve, actLine, actCurve] {
        btnCurve->setIcon(makeCurveStyleDropdownIcon(0));
        actLine->setChecked(true);
        actCurve->setChecked(false);
    });
    connect(actCurve, &QAction::triggered, this, [btnCurve, actLine, actCurve] {
        btnCurve->setIcon(makeCurveStyleDropdownIcon(1));
        actLine->setChecked(false);
        actCurve->setChecked(true);
    });
    btnCurve->setMenu(curveMenu);
    layoutLines->addWidget(btnCurve);

    auto *btnSwap = new QToolButton(pageLines);
    btnSwap->setIcon(captureIcon(CaptureIcon::SwapDirection));
    btnSwap->setFixedSize(24, 24);
    btnSwap->setToolTip(QStringLiteral("反转方向"));
    btnSwap->setFocusPolicy(Qt::NoFocus);
    layoutLines->addWidget(btnSwap);
    connect(btnSwap, &QToolButton::clicked, this, [this] {
        if (!m_canvas) return;
        auto cur = m_canvas->arrowStyle();
        if (cur == AnnotationCanvas::ArrowStyle::End) m_canvas->setArrowStyle(AnnotationCanvas::ArrowStyle::Start);
        else if (cur == AnnotationCanvas::ArrowStyle::Start) m_canvas->setArrowStyle(AnnotationCanvas::ArrowStyle::End);
        else if (cur == AnnotationCanvas::ArrowStyle::HollowEnd) m_canvas->setArrowStyle(AnnotationCanvas::ArrowStyle::HollowStart);
        else if (cur == AnnotationCanvas::ArrowStyle::HollowStart) m_canvas->setArrowStyle(AnnotationCanvas::ArrowStyle::HollowEnd);
    });

    auto *sepLines = new QFrame(pageLines);
    sepLines->setFrameShape(QFrame::VLine);
    sepLines->setStyleSheet(QStringLiteral("background: #c4c9cf;"));
    sepLines->setFixedSize(1, 19);
    layoutLines->addWidget(sepLines, 0, Qt::AlignVCenter);

    layoutLines->addWidget(makeColorModule(pageLines));
    m_toolOptionsStack->addWidget(pageLines);

    // ----------------------------------------------------
    // Page 2: Pen (画笔) - 09-交互-03.png
    // ----------------------------------------------------
    auto *pagePen = new QWidget(m_toolOptions);
    auto *layoutPen = new QHBoxLayout(pagePen);
    layoutPen->setSizeConstraint(QLayout::SetFixedSize);
    layoutPen->setContentsMargins(0, 0, 0, 0);
    layoutPen->setSpacing(2);

    auto *penWidthGroup = new QButtonGroup(pagePen);
    penWidthGroup->setExclusive(true);
    for (int w : {2, 4, 8}) {
        auto *b = makeWidthPreset(pagePen, w, QStringLiteral("penWidth%1").arg(w), w == 2);
        penWidthGroup->addButton(b);
        layoutPen->addWidget(b);
        if (w == 2) b->setChecked(true);
    }
    layoutPen->addWidget(makeSeparator(pagePen));
    layoutPen->addWidget(makeLineStyleDropdown(pagePen));
    layoutPen->addWidget(makeSeparator(pagePen));
    layoutPen->addWidget(makeColorModule(pagePen));
    m_toolOptionsStack->addWidget(pagePen);

    // ----------------------------------------------------
    // Page 3: Marker (荧光笔) - 09-交互-04.png
    // ----------------------------------------------------
    auto *pageMarker = new QWidget(m_toolOptions);
    auto *layoutMarker = new QHBoxLayout(pageMarker);
    layoutMarker->setSizeConstraint(QLayout::SetFixedSize);
    layoutMarker->setContentsMargins(0, 0, 0, 0);
    layoutMarker->setSpacing(2);

    auto *markerWidthGroup = new QButtonGroup(pageMarker);
    markerWidthGroup->setExclusive(true);
    for (int w : {16, 23, 31}) {
        auto *b = makeWidthPreset(pageMarker, w, QStringLiteral("penWidth%1").arg(w), w == 16);
        markerWidthGroup->addButton(b);
        layoutMarker->addWidget(b);
        if (w == 16) b->setChecked(true);
    }
    auto *markerRect = new QToolButton(pageMarker);
    markerRect->setIcon(captureIcon(CaptureIcon::PatternRect));
    markerRect->setFixedSize(24, 24);
    markerRect->setCheckable(true);
    markerRect->setToolTip(QStringLiteral("矩形荧光笔"));
    layoutMarker->addWidget(markerRect);

    auto *markerEllipse = new QToolButton(pageMarker);
    markerEllipse->setIcon(captureIcon(CaptureIcon::PatternEllipse));
    markerEllipse->setFixedSize(24, 24);
    markerEllipse->setCheckable(true);
    markerEllipse->setToolTip(QStringLiteral("椭圆荧光笔"));
    layoutMarker->addWidget(markerEllipse);

    auto *sepMarker = new QFrame(pageMarker);
    sepMarker->setFrameShape(QFrame::VLine);
    sepMarker->setStyleSheet(QStringLiteral("background: #c4c9cf;"));
    sepMarker->setFixedSize(1, 19);
    layoutMarker->addWidget(sepMarker, 0, Qt::AlignVCenter);

    layoutMarker->addWidget(makeColorModule(pageMarker));
    m_toolOptionsStack->addWidget(pageMarker);

    // ----------------------------------------------------
    // Page 4: Mosaic (马赛克 / 模糊) - 09-交互-05.png
    // ----------------------------------------------------
    auto *pageMosaic = new QWidget(m_toolOptions);
    auto *layoutMosaic = new QHBoxLayout(pageMosaic);
    layoutMosaic->setSizeConstraint(QLayout::SetFixedSize);
    layoutMosaic->setContentsMargins(0, 0, 0, 0);
    layoutMosaic->setSpacing(2);

    auto *mosaicPresetGroup = new QButtonGroup(pageMosaic);
    mosaicPresetGroup->setExclusive(true);
    for (int w : {4, 16, 24}) {
        auto *b = makeWidthPreset(pageMosaic, w, QStringLiteral("penWidth%1").arg(w), w == 4);
        if (w == 4) {
            b->setText(QStringLiteral("4"));
        }
        mosaicPresetGroup->addButton(b);
        layoutMosaic->addWidget(b);
        connect(b, &QToolButton::clicked, this, [this] {
            if (m_canvas) m_canvas->setEffectShape(AnnotationCanvas::EffectShape::Brush);
        });
        if (w == 4) b->setChecked(true);
    }

    auto *mosaicShapeGroup = new QButtonGroup(pageMosaic);
    mosaicShapeGroup->setExclusive(true);
    auto *mosaicRect = new QToolButton(pageMosaic);
    mosaicRect->setIcon(captureIcon(CaptureIcon::PatternRect));
    mosaicRect->setFixedSize(24, 24);
    mosaicRect->setCheckable(true);
    mosaicRect->setProperty("mosaicOption", true);
    mosaicRect->setProperty("effectShapeOption", true);
    mosaicRect->setProperty("effectShape", int(AnnotationCanvas::EffectShape::Rectangle));
    mosaicRect->setToolTip(QStringLiteral("矩形覆盖"));
    mosaicRect->setFocusPolicy(Qt::NoFocus);
    mosaicShapeGroup->addButton(mosaicRect);
    layoutMosaic->addWidget(mosaicRect);
    connect(mosaicRect, &QToolButton::clicked, this, [this] {
        if (!m_canvas) return;
        m_canvas->setEffectShape(AnnotationCanvas::EffectShape::Rectangle);
        refreshAnnotationCursor();
    });

    auto *mosaicEllipse = new QToolButton(pageMosaic);
    mosaicEllipse->setIcon(captureIcon(CaptureIcon::PatternEllipse));
    mosaicEllipse->setFixedSize(24, 24);
    mosaicEllipse->setCheckable(true);
    mosaicEllipse->setProperty("mosaicOption", true);
    mosaicEllipse->setProperty("effectShapeOption", true);
    mosaicEllipse->setProperty("effectShape", int(AnnotationCanvas::EffectShape::Ellipse));
    mosaicEllipse->setToolTip(QStringLiteral("椭圆覆盖"));
    mosaicEllipse->setFocusPolicy(Qt::NoFocus);
    mosaicShapeGroup->addButton(mosaicEllipse);
    layoutMosaic->addWidget(mosaicEllipse);
    connect(mosaicEllipse, &QToolButton::clicked, this, [this] {
        if (!m_canvas) return;
        m_canvas->setEffectShape(AnnotationCanvas::EffectShape::Ellipse);
        refreshAnnotationCursor();
    });

    auto *mosaicDiv = new QFrame(pageMosaic);
    mosaicDiv->setFrameShape(QFrame::VLine);
    mosaicDiv->setStyleSheet(QStringLiteral("background: #c4c9cf;"));
    mosaicDiv->setFixedSize(1, 19);
    layoutMosaic->addWidget(mosaicDiv, 0, Qt::AlignVCenter);

    auto *effectToggle = new QToolButton(pageMosaic);
    effectToggle->setIcon(captureIcon(CaptureIcon::Mosaic));
    effectToggle->setFixedSize(24, 24);
    effectToggle->setCheckable(true);
    effectToggle->setChecked(false);
    effectToggle->setProperty("mosaicOption", true);
    effectToggle->setProperty("effectToggle", true);
    effectToggle->setToolTip(QStringLiteral("点击切换马赛克 / 模糊"));
    effectToggle->setFocusPolicy(Qt::NoFocus);
    layoutMosaic->addWidget(effectToggle);
    connect(effectToggle, &QToolButton::toggled, this, [this, effectToggle](bool blur) {
        effectToggle->setIcon(captureIcon(blur ? CaptureIcon::Blur : CaptureIcon::Mosaic));
        if (!m_canvas) return;
        m_canvas->setTool(blur ? AnnotationCanvas::Tool::Blur : AnnotationCanvas::Tool::Mosaic);
        refreshAnnotationCursor();
        QTimer::singleShot(0, this, [this] { refreshAnnotationCursor(); });
    });

    auto *sliderEffect = new TickSlider(Qt::Horizontal, pageMosaic);
    sliderEffect->setRange(2, 40);
    sliderEffect->setValue(6);
    sliderEffect->setFixedSize(80, 24);
    sliderEffect->setProperty("mosaicOption", true);
    sliderEffect->setToolTip(QStringLiteral("强度"));
    layoutMosaic->addWidget(sliderEffect);

    auto *labelEffectVal = new QLabel(QStringLiteral("6"), pageMosaic);
    labelEffectVal->setObjectName(QStringLiteral("mosaicEffectValue"));
    labelEffectVal->setAlignment(Qt::AlignCenter);
    labelEffectVal->setFixedWidth(20);
    labelEffectVal->setProperty("mosaicOption", true);
    layoutMosaic->addWidget(labelEffectVal);

    connect(sliderEffect, &QSlider::valueChanged, this, [this, labelEffectVal](int val) {
        if (m_canvas) {
            m_canvas->setEffectSize(val);
            refreshAnnotationCursor();
        }
        labelEffectVal->setText(QString::number(val));
    });
    m_toolOptionsStack->addWidget(pageMosaic);

    // ----------------------------------------------------
    // Page 5: Text (文字) - 09-交互-06.png
    // ----------------------------------------------------
    auto *pageText = new QWidget(m_toolOptions);
    auto *layoutText = new QHBoxLayout(pageText);
    layoutText->setSizeConstraint(QLayout::SetFixedSize);
    layoutText->setContentsMargins(0, 0, 0, 0);
    layoutText->setSpacing(2);

    auto *btnBold = new QToolButton(pageText);
    btnBold->setIcon(captureIcon(CaptureIcon::TextBold));
    btnBold->setFixedSize(24, 24);
    btnBold->setCheckable(true);
    btnBold->setToolTip(QStringLiteral("加粗"));
    btnBold->setFocusPolicy(Qt::NoFocus);
    layoutText->addWidget(btnBold);
    connect(btnBold, &QToolButton::toggled, this, [this](bool b) { if (m_canvas) m_canvas->setTextBold(b); });

    auto *btnItalic = new QToolButton(pageText);
    btnItalic->setIcon(captureIcon(CaptureIcon::TextItalic));
    btnItalic->setFixedSize(24, 24);
    btnItalic->setCheckable(true);
    btnItalic->setToolTip(QStringLiteral("斜体"));
    btnItalic->setFocusPolicy(Qt::NoFocus);
    layoutText->addWidget(btnItalic);
    connect(btnItalic, &QToolButton::toggled, this, [this](bool i) { if (m_canvas) m_canvas->setTextItalic(i); });

    auto *btnOutline = new QToolButton(pageText);
    btnOutline->setIcon(captureIcon(CaptureIcon::TextOutline));
    btnOutline->setFixedSize(24, 24);
    btnOutline->setCheckable(true);
    btnOutline->setToolTip(QStringLiteral("描边"));
    btnOutline->setFocusPolicy(Qt::NoFocus);
    layoutText->addWidget(btnOutline);
    connect(btnOutline, &QToolButton::toggled, this, [this](bool o) {
        if (m_canvas) m_canvas->setTextOutlineWidth(o ? 1.5 : 0.0);
    });

    auto *btnTextBg = new QToolButton(pageText);
    btnTextBg->setIcon(captureIcon(CaptureIcon::TextBackground));
    btnTextBg->setFixedSize(24, 24);
    btnTextBg->setCheckable(true);
    btnTextBg->setToolTip(QStringLiteral("背景填充"));
    btnTextBg->setFocusPolicy(Qt::NoFocus);
    layoutText->addWidget(btnTextBg);
    connect(btnTextBg, &QToolButton::toggled, this, [this](bool bg) {
        if (m_canvas) m_canvas->setTextBackground(bg);
    });

    layoutText->addWidget(makeSeparator(pageText));

    auto *comboFontFamily = new QComboBox(pageText);
    comboFontFamily->setFixedSize(110, 24);
    comboFontFamily->addItems({QStringLiteral("Microsoft YaHei UI"), QStringLiteral("SimSun"), QStringLiteral("SimHei"),
                               QStringLiteral("Segoe UI"), QStringLiteral("Arial")});
    comboFontFamily->setFocusPolicy(Qt::NoFocus);
    layoutText->addWidget(comboFontFamily);
    connect(comboFontFamily, &QComboBox::currentTextChanged, this, [this](const QString &f) {
        if (m_canvas) m_canvas->setTextFontFamily(f);
    });

    auto *comboFontSize = new QComboBox(pageText);
    comboFontSize->setFixedSize(45, 24);
    for (int i = 1; i <= 10; ++i)
        comboFontSize->addItem(QString::number(i));
    comboFontSize->setCurrentText(QStringLiteral("4"));
    comboFontSize->setFocusPolicy(Qt::NoFocus);
    layoutText->addWidget(comboFontSize);
    connect(comboFontSize, &QComboBox::currentTextChanged, this, [this](const QString &s) {
        if (m_canvas) m_canvas->setTextFontSize(s.toInt() * 4.0 + 4.0);
    });

    layoutText->addWidget(makeSeparator(pageText));
    layoutText->addWidget(makeColorModule(pageText));
    m_toolOptionsStack->addWidget(pageText);

    // ----------------------------------------------------
    // Page 6: Number (序号) - 09-交互-07.png
    // ----------------------------------------------------
    auto *pageNumber = new QWidget(m_toolOptions);
    auto *layoutNumber = new QHBoxLayout(pageNumber);
    layoutNumber->setSizeConstraint(QLayout::SetFixedSize);
    layoutNumber->setContentsMargins(0, 0, 0, 0);
    layoutNumber->setSpacing(2);

    auto *numStyleGroup = new QButtonGroup(pageNumber);
    numStyleGroup->setExclusive(true);
    const QList<QPair<CaptureIcon, int>> numStyles{
        {CaptureIcon::NumberFilled, 0},
        {CaptureIcon::NumberDoubleRing, 1},
        {CaptureIcon::NumberOutline, 2}
    };
    for (const auto &ns : numStyles) {
        auto *b = new QToolButton(pageNumber);
        b->setIcon(captureIcon(ns.first));
        b->setFixedSize(24, 24);
        b->setCheckable(true);
        b->setFocusPolicy(Qt::NoFocus);
        numStyleGroup->addButton(b);
        layoutNumber->addWidget(b);
        if (ns.second == 0) b->setChecked(true);
        connect(b, &QToolButton::clicked, this, [this, style = ns.second] {
            if (m_canvas) m_canvas->setNumberStyle(style);
        });
    }

    layoutNumber->addWidget(makeSeparator(pageNumber));

    auto *comboNumScale = new QComboBox(pageNumber);
    comboNumScale->setFixedSize(45, 24);
    for (int i = 1; i <= 10; ++i)
        comboNumScale->addItem(QString::number(i));
    comboNumScale->setCurrentText(QStringLiteral("5"));
    comboNumScale->setToolTip(QStringLiteral("字体缩放"));
    comboNumScale->setFocusPolicy(Qt::NoFocus);
    layoutNumber->addWidget(comboNumScale);
    connect(comboNumScale, &QComboBox::currentTextChanged, this, [this](const QString &s) {
        if (m_canvas) m_canvas->setNumberSize(s.toInt() * 4.0 + 4.0);
    });

    layoutNumber->addWidget(makeSeparator(pageNumber));
    layoutNumber->addWidget(makeColorModule(pageNumber));
    m_toolOptionsStack->addWidget(pageNumber);

    // ----------------------------------------------------
    // Page 7: Magnifier (放大镜标注) - 09-交互-08.png
    // ----------------------------------------------------
    auto *pageMagnifier = new QWidget(m_toolOptions);
    auto *layoutMagnifier = new QHBoxLayout(pageMagnifier);
    layoutMagnifier->setSizeConstraint(QLayout::SetFixedSize);
    layoutMagnifier->setContentsMargins(0, 0, 0, 0);
    layoutMagnifier->setSpacing(2);

    auto *magWidthGroup = new QButtonGroup(pageMagnifier);
    magWidthGroup->setExclusive(true);
    for (int w : {2, 4, 8}) {
        auto *b = makeWidthPreset(pageMagnifier, w, QStringLiteral("penWidth%1").arg(w), w == 2);
        magWidthGroup->addButton(b);
        layoutMagnifier->addWidget(b);
        if (w == 2) b->setChecked(true);
    }

    layoutMagnifier->addWidget(makeSeparator(pageMagnifier));

    auto *magShapeGroup = new QButtonGroup(pageMagnifier);
    magShapeGroup->setExclusive(true);

    auto *btnMagRect = new QToolButton(pageMagnifier);
    btnMagRect->setIcon(captureIcon(CaptureIcon::Rectangle));
    btnMagRect->setFixedSize(24, 24);
    btnMagRect->setCheckable(true);
    btnMagRect->setChecked(true);
    btnMagRect->setToolTip(QStringLiteral("矩形放大镜（点击或悬停设置圆角半径）"));
    btnMagRect->setFocusPolicy(Qt::NoFocus);
    magShapeGroup->addButton(btnMagRect);
    layoutMagnifier->addWidget(btnMagRect);

    auto *btnMagEllipse = new QToolButton(pageMagnifier);
    btnMagEllipse->setIcon(captureIcon(CaptureIcon::Ellipse));
    btnMagEllipse->setFixedSize(24, 24);
    btnMagEllipse->setCheckable(true);
    btnMagEllipse->setToolTip(QStringLiteral("椭圆放大镜"));
    btnMagEllipse->setFocusPolicy(Qt::NoFocus);
    magShapeGroup->addButton(btnMagEllipse);
    layoutMagnifier->addWidget(btnMagEllipse);

    btnMagRect->installEventFilter(new HoverPopupFilter([this, btnMagRect] {
        if (btnMagRect && btnMagRect->underMouse()) {
            showCornerRadiusPopup(btnMagRect);
        }
    }, btnMagRect));
    connect(btnMagRect, &QToolButton::clicked, this, [this, btnMagRect] {
        if (!m_canvas) return;
        m_canvas->setEffectShape(AnnotationCanvas::EffectShape::Rectangle);
        refreshAnnotationCursor();
        showCornerRadiusPopup(btnMagRect);
    });
    connect(btnMagEllipse, &QToolButton::clicked, this, [this] {
        if (m_cornerRadiusPopup) {
            m_cornerRadiusPopup->close();
        }
        if (!m_canvas) return;
        m_canvas->setEffectShape(AnnotationCanvas::EffectShape::Ellipse);
        refreshAnnotationCursor();
    });

    layoutMagnifier->addWidget(makeSeparator(pageMagnifier));

    auto *btnMagCutout = new QToolButton(pageMagnifier);
    btnMagCutout->setIcon(captureIcon(CaptureIcon::MagnifierCutout));
    btnMagCutout->setFixedSize(24, 24);
    btnMagCutout->setCheckable(true);
    btnMagCutout->setChecked(true);
    btnMagCutout->setToolTip(QStringLiteral("截取边框"));
    btnMagCutout->setFocusPolicy(Qt::NoFocus);
    layoutMagnifier->addWidget(btnMagCutout);
    connect(btnMagCutout, &QToolButton::toggled, this, [this](bool c) {
        if (m_canvas) m_canvas->setMagnifierCutout(c);
    });

    layoutMagnifier->addWidget(makeSeparator(pageMagnifier));

    auto *dotLabel = new QLabel(pageMagnifier);
    dotLabel->setFixedSize(10, 24);
    dotLabel->setAlignment(Qt::AlignCenter);
    {
        QPixmap pix(10, 24);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(QStringLiteral("#444444")));
        p.drawEllipse(QPointF(5, 12), 2.0, 2.0);
        dotLabel->setPixmap(pix);
    }
    layoutMagnifier->addWidget(dotLabel);

    auto *sliderMagZoom = new TickSlider(Qt::Horizontal, pageMagnifier);
    sliderMagZoom->setRange(120, 400);
    sliderMagZoom->setValue(200);
    sliderMagZoom->setFixedSize(66, 24);
    sliderMagZoom->setToolTip(QStringLiteral("放大倍数"));
    layoutMagnifier->addWidget(sliderMagZoom);

    auto *labelZoomVal = new QLabel(QStringLiteral("2"), pageMagnifier);
    labelZoomVal->setFixedSize(30, 24);
    labelZoomVal->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    labelZoomVal->setStyleSheet(QStringLiteral("QLabel { color: #222222; font-size: 11px; font-family: 'Segoe UI', 'Microsoft YaHei UI', sans-serif; }"));
    layoutMagnifier->addWidget(labelZoomVal);

    connect(sliderMagZoom, &QSlider::valueChanged, this, [this, labelZoomVal](int v) {
        const qreal mag = v / 100.0;
        if (m_canvas) m_canvas->setMagnification(mag);
        if (v % 100 == 0)
            labelZoomVal->setText(QString::number(v / 100));
        else if (v % 10 == 0)
            labelZoomVal->setText(QString::number(mag, 'f', 1));
        else
            labelZoomVal->setText(QString::number(mag, 'f', 2));
    });

    layoutMagnifier->addWidget(makeSeparator(pageMagnifier));

    auto *btnMagConn = new QToolButton(pageMagnifier);
    btnMagConn->setObjectName(QStringLiteral("annotationMagConn"));
    btnMagConn->setStyleSheet(dropdownBtnStyle);
    btnMagConn->setFixedSize(52, 24);
    btnMagConn->setPopupMode(QToolButton::InstantPopup);
    btnMagConn->setIcon(makeMagnifierConnectorDropdownIcon(0));
    btnMagConn->setIconSize(QSize(48, 20));
    btnMagConn->setToolTip(QStringLiteral("指示线样式"));

    auto *menuConn = new QMenu(btnMagConn);
    menuConn->setStyleSheet(QStringLiteral(
        "QMenu { background: white; border: 1px solid #c4c9cf; padding: 2px; }"
        "QMenu::item { padding: 4px 10px; font-size: 12px; }"
        "QMenu::item:selected { background: #d6e8fa; color: #111; }"));
    auto *actStraight = menuConn->addAction(makeMagnifierConnectorMenuItemIcon(0), QStringLiteral("直线指示"));
    auto *actBroken = menuConn->addAction(makeMagnifierConnectorMenuItemIcon(1), QStringLiteral("折线指示"));
    auto *actNone = menuConn->addAction(makeMagnifierConnectorMenuItemIcon(2), QStringLiteral("无指示线"));
    actStraight->setCheckable(true);
    actStraight->setChecked(true);
    actBroken->setCheckable(true);
    actNone->setCheckable(true);

    const auto setConnMode = [btnMagConn, actStraight, actBroken, actNone, this](int mode) {
        btnMagConn->setIcon(makeMagnifierConnectorDropdownIcon(mode));
        actStraight->setChecked(mode == 0);
        actBroken->setChecked(mode == 1);
        actNone->setChecked(mode == 2);
        if (m_canvas) m_canvas->setMagnifierConnectorStyle(mode);
    };
    connect(actStraight, &QAction::triggered, this, [=] { setConnMode(0); });
    connect(actBroken, &QAction::triggered, this, [=] { setConnMode(1); });
    connect(actNone, &QAction::triggered, this, [=] { setConnMode(2); });
    btnMagConn->setMenu(menuConn);
    layoutMagnifier->addWidget(btnMagConn);

    layoutMagnifier->addWidget(makeSeparator(pageMagnifier));
    layoutMagnifier->addWidget(makeColorModule(pageMagnifier));
    m_toolOptionsStack->addWidget(pageMagnifier);

    // ----------------------------------------------------
    // Page 8: Eraser (橡皮擦) - 09-交互-09.png
    // ----------------------------------------------------
    auto *pageEraser = new QWidget(m_toolOptions);
    auto *layoutEraser = new QHBoxLayout(pageEraser);
    layoutEraser->setSizeConstraint(QLayout::SetFixedSize);
    layoutEraser->setContentsMargins(0, 0, 0, 0);
    layoutEraser->setSpacing(2);

    auto *eraserPresetGroup = new QButtonGroup(pageEraser);
    eraserPresetGroup->setExclusive(true);
    for (int w : {16, 24, 31}) {
        auto *b = makeWidthPreset(pageEraser, w, QStringLiteral("penWidth%1").arg(w), w == 16);
        eraserPresetGroup->addButton(b);
        layoutEraser->addWidget(b);
        if (w == 16) b->setChecked(true);
    }
    auto *eraserRect = new QToolButton(pageEraser);
    eraserRect->setIcon(captureIcon(CaptureIcon::PatternRect));
    eraserRect->setFixedSize(24, 24);
    eraserRect->setCheckable(true);
    eraserRect->setToolTip(QStringLiteral("矩形橡皮擦"));
    layoutEraser->addWidget(eraserRect);

    auto *eraserEllipse = new QToolButton(pageEraser);
    eraserEllipse->setIcon(captureIcon(CaptureIcon::PatternEllipse));
    eraserEllipse->setFixedSize(24, 24);
    eraserEllipse->setCheckable(true);
    eraserEllipse->setToolTip(QStringLiteral("椭圆橡皮擦"));
    layoutEraser->addWidget(eraserEllipse);

    m_toolOptionsStack->addWidget(pageEraser);

    // Keep additional commands accessible without displacing the reference toolbar actions.
    m_toolbar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_toolbar, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        showOutputMenu(m_toolbar->mapToGlobal(pos));
    });

    m_toolOptions->hide();
    m_toolbar->hide();
    createSizeToolbar();

    connect(m_toolOptionsStack, &QStackedLayout::currentChanged, this, [this](int index) {
        if (!m_toolOptions || !m_toolOptionsStack) return;
        if (QWidget *w = m_toolOptionsStack->widget(index)) {
            QSize s = w->sizeHint();
            m_toolOptions->setFixedSize(s.width() + 2, s.height() + 2);
            m_toolOptions->updateGeometry();
        }
        if (m_toolbar) {
            m_toolbar->adjustSize();
        }
        if (m_toolOptions->isVisible()) {
            updateToolbarGeometry();
        }
    });
}

void SnipOverlay::setHideOutputOptions(bool enabled)
{
    m_hideOutputOptions = enabled;
    if (m_outputOptionsButton)
        m_outputOptionsButton->setVisible(!enabled);
    updateToolbarGeometry();
}

void SnipOverlay::confirmSelection()
{
    if (m_selection.isEmpty() || m_canvas)
        return;
    if (!m_toolbar)
        createToolbar();
    // Keep normal captures in full-screen image coordinates.  A history item
    // can have a pixel size that differs from the current desktop frame, so it
    // remains a compact canvas until the user changes the selection.
    const bool historyOverride = !m_selectionImageOverride.isNull();
    QImage image = historyOverride ? m_selectionImageOverride : m_background;
    m_selectionImageOverride = {};
    image.setDevicePixelRatio(1);
    m_canvas = new AnnotationCanvas(image, this);
    m_canvas->setCaptureImage(image, {}, historyOverride
                                         ? QSizeF(m_selection.size()) : QSizeF(size()));
    m_canvas->setCanvasFrame(historyOverride ? QPoint() : m_selection.topLeft(),
                             historyOverride ? QSizeF(m_selection.size()) : QSizeF(size()),
                             historyOverride ? QRect() : m_selection);
    m_canvas->setAutoSelectOnCreation(m_autoSelectAnnotations);
    m_canvas->setObjectName(QStringLiteral("selectionCanvas"));
    m_canvas->installEventFilter(this);
    m_canvas->setMinimumSize(0, 0);
    m_canvas->setGeometry(m_selection);
    m_canvas->setAttribute(Qt::WA_TranslucentBackground);
    m_canvas->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_canvas->show();
    if (m_selectionBorder) {
        m_selectionBorder->raise();
        m_selectionBorder->update();
    }
    m_toolbar->raise();
    m_sizeToolbar->raise();
    connect(m_canvas, &AnnotationCanvas::historyChanged, this, [this] {
        m_toolbar->findChild<QToolButton *>(QStringLiteral("撤销"))->setEnabled(m_canvas->canUndo());
        m_toolbar->findChild<QToolButton *>(QStringLiteral("重做"))->setEnabled(m_canvas->canRedo());
    });
    connect(m_canvas, &AnnotationCanvas::penWidthChanged, this, &SnipOverlay::updateDynamicWidthButtons);
    connect(m_canvas, &AnnotationCanvas::penColorChanged, this, &SnipOverlay::updateAllColorWatchers);
    connect(m_canvas, &AnnotationCanvas::penStyleChanged, this, [this](Qt::PenStyle s) {
        if (m_lineStyleButton) m_lineStyleButton->setIcon(makeLineStyleDropdownIcon(s));
    });
    connect(m_canvas, &AnnotationCanvas::arrowStyleChanged, this, [this](AnnotationCanvas::ArrowStyle s) {
        if (m_arrowStyleButton) m_arrowStyleButton->setIcon(makeArrowStyleDropdownIcon(s));
    });
    m_canvas->setPenColor(m_currentAnnotationColor);
    updateDynamicWidthButtons(m_canvas->penWidth());
    updateAllColorWatchers(m_currentAnnotationColor);
    m_selectionConfirmed = true;
    updateToolbarGeometry();
    update();
}

void SnipOverlay::resetSelection()
{
    if (!m_toolbar)
        createToolbar();
    m_transformingSelection = false;
    m_movingSelection = false;
    m_resizeEdges = {};
    m_selectionConfirmed = false;
    m_externalDrawing = false;
    m_selection = {};
    m_selectionImageOverride = {};
    delete m_canvas;
    m_canvas = nullptr;
    m_toolbar->hide();
    m_sizeToolbar->hide();
    m_toolOptions->hide();
    for (auto *b : m_toolbar->findChildren<QToolButton *>()) {
        QSignalBlocker blocker(b);
        b->setChecked(false);
    }
    m_toolbar->findChild<QToolButton *>(QStringLiteral("撤销"))->setEnabled(false);
    m_toolbar->findChild<QToolButton *>(QStringLiteral("重做"))->setEnabled(false);
    update();
}

} // namespace snipaste::ui
