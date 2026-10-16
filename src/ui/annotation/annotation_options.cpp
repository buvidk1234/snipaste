#include "ui/annotation/annotation_options.h"
#include "platform/keyboard.h"
#include "ui/annotation/annotation_canvas.h"
#include "ui/annotation/annotation_option_widgets.h"
#include "ui/annotation/annotation_toolbar_icons.h"
#include "ui/snip/capture_icons.h"

#include <QButtonGroup>
#include <QColorDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QScreen>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStackedLayout>
#include <QToolButton>

namespace snipaste::ui
{
using namespace annotation;
QToolButton *AnnotationOptions::makeWidthPreset(QWidget *parent, int initialWidth, const QString &objName,
                                                bool isChecked)
{

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
        if (!m_canvas)
            return;
        const qreal w = b->property("presetWidth").toReal();
        m_canvas->setPenWidth(w);
        refreshAnnotationCursor();
    });

    return b;
}

QToolButton *AnnotationOptions::makeLineStyleDropdown(QWidget *parent)
{
    auto *btn = new QToolButton(parent);
    btn->setObjectName(QStringLiteral("annotationLineStyle"));
    btn->setStyleSheet(dropdownButtonStyle());
    btn->setFixedSize(72, 24);
    btn->setToolTip(QStringLiteral("线条样式"));
    btn->setPopupMode(QToolButton::InstantPopup);
    btn->setIcon(makeLineStyleDropdownIcon(Qt::SolidLine, 68, 20));
    btn->setIconSize(QSize(68, 20));
    m_lineStyleButton = btn;

    auto *menu = new QMenu(btn);
    menu->setStyleSheet(
        QStringLiteral("QMenu { background: #ffffff; border: 1px solid #7f7f7f; border-radius: 1px; padding: 2px; icon-size: 64px; }"
                       "QMenu::item { min-width: 66px; max-width: 66px; height: 26px; margin: 1px 0px; padding: 0px; border: 1px solid transparent; border-radius: 2px; }"
                       "QMenu::item:hover { background: #cde8ff; border: 1px solid #2889ed; }"
                       "QMenu::item:selected { background: #a3d2ff; border: 1px solid #1c85e8; }"
                       "QMenu::indicator { width: 0px; height: 0px; }"));
    menu->setFixedWidth(72);
    const QList<QPair<QString, Qt::PenStyle>> styles{
        {QStringLiteral("实线"), Qt::SolidLine},
        {QStringLiteral("虚线"), Qt::DashLine},
        {QStringLiteral("点线"), Qt::DotLine},
        {QStringLiteral("点划线"), Qt::DashDotLine},
        {QStringLiteral("双点划线"), Qt::DashDotDotLine},
    };
    for (const auto &s : styles)
    {
        auto *action = menu->addAction(makeLineMenuItemIcon(s.second, 64, 22), QString());
        action->setCheckable(true);
        action->setData(int(s.second));
        action->setToolTip(s.first);
        if (s.second == Qt::SolidLine)
            action->setChecked(true);
        connect(action, &QAction::triggered, this, [this, btn, menu, action, s] {
            if (m_canvas)
                m_canvas->setPenStyle(s.second);
            btn->setIcon(makeLineStyleDropdownIcon(s.second, 68, 20));
            for (auto *other : menu->actions())
                other->setChecked(other == action);
        });
    }
    btn->setMenu(menu);
    return btn;
}

QWidget *AnnotationOptions::makeColorModule(QWidget *parent)
{

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

    const QStringList row0 = {QStringLiteral("#000000"), QStringLiteral("#7f7f7f"), QStringLiteral("#880015"),
                              QStringLiteral("#ed1c24"), QStringLiteral("#ff7f27"), QStringLiteral("#fff200"),
                              QStringLiteral("#22b14c"), QStringLiteral("#00a2e8"), QStringLiteral("#3f48cc"),
                              QStringLiteral("#a349a4")};
    const QStringList row1 = {QStringLiteral("#ffffff"), QStringLiteral("#c3c3c3"), QStringLiteral("#b97a57"),
                              QStringLiteral("#ffaec9"), QStringLiteral("#ffc90e"), QStringLiteral("#efe4b0"),
                              QStringLiteral("#b5e61d"), QStringLiteral("#99d9ea"), QStringLiteral("#7092be"),
                              QStringLiteral("#c8bfe7")};

    auto addRow = [&](const QStringList &colors, int rowIdx) {
        for (int col = 0; col < colors.size(); ++col)
        {
            const QString hex = colors.at(col);
            auto *b = new QToolButton(gridWidget);
            b->setObjectName(QStringLiteral("annotationColor_%1").arg(hex.mid(1)));
            b->setFixedSize(11, 11);
            b->setCheckable(true);
            b->setStyleSheet(QStringLiteral("QToolButton { background: %1; border: 1px solid #777777; }"
                                            "QToolButton:hover { border: 1px solid #2889ed; }"
                                            "QToolButton:checked { background: %1; border: 2px solid "
                                            "#2889ed; outline: 1px solid white; }")
                                 .arg(hex));
            const bool isCur =
                (hex.compare(m_currentAnnotationColor.name(QColor::HexRgb), Qt::CaseInsensitive) == 0);
            if (isCur)
            {
                b->setChecked(true);
            }
            b->setToolTip(hex);
            b->setFocusPolicy(Qt::NoFocus);
            grid->addWidget(b, rowIdx, col);
            m_paletteButtons.append({b, hex});
            connect(b, &QToolButton::clicked, this, [this, hex] {
                const QColor c(hex);
                m_currentAnnotationColor = c;
                if (m_canvas)
                {
                    m_canvas->setPenColor(c);
                    refreshAnnotationCursor();
                }
                else
                {
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
        const QColor chosen =
            QColorDialog::getColor(cur, this, QStringLiteral("选择标注颜色"), QColorDialog::ShowAlphaChannel);
        if (chosen.isValid())
        {
            m_currentAnnotationColor = chosen;
            if (m_canvas)
            {
                m_canvas->setPenColor(chosen);
                refreshAnnotationCursor();
            }
            updateAllColorWatchers(chosen);
        }
    };
    connect(currentBtn, &QToolButton::clicked, this, pickCustom);
    connect(customBtn, &QToolButton::clicked, this, pickCustom);

    return container;
}

void AnnotationOptions::updateDynamicWidthButtons(qreal width)
{
    QWidget *activePage = m_toolOptionsStack ? m_toolOptionsStack->currentWidget() : nullptr;
    if (activePage)
    {
        QToolButton *checkedBtn = nullptr;
        for (auto *b : m_dynamicWidthButtons)
        {
            if (b && b->parentWidget() == activePage && b->isChecked())
            {
                checkedBtn = b;
                break;
            }
        }
        if (checkedBtn)
        {
            checkedBtn->setProperty("presetWidth", width);
            checkedBtn->setIcon(makeDynamicWidthIcon(width, true));
            checkedBtn->setToolTip(QStringLiteral("画笔粗细: %1 px (滚轮可微调)").arg(qRound(width)));
        }
        else
        {
            for (auto *b : m_dynamicWidthButtons)
            {
                if (b && b->parentWidget() == activePage)
                {
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
void AnnotationOptions::showCornerRadiusPopup(QWidget *sourceBtn)
{
    if (!sourceBtn)
        return;
    if (m_cornerRadiusPopup && m_cornerRadiusPopup->isVisible())
    {
        if (m_cornerRadiusPopup->property("sourceBtn").value<QWidget *>() == sourceBtn)
        {
            m_cornerRadiusPopup->close();
            return;
        }
    }
    if (m_cornerRadiusPopup)
    {
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
        "QSlider::handle:horizontal { width: 10px; height: 10px; margin: -4px 0; border-radius: 5px; "
        "background: white; border: 2px solid #2889ed; }"
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
        if (!m_canvas)
            return;
        m_canvas->setCornerRadius(v);
        if (m_canvas->tool() != AnnotationCanvas::Tool::Magnifier)
        {
            if (v > 0)
            {
                m_canvas->setTool(AnnotationCanvas::Tool::RoundedRectangle);
            }
            else
            {
                m_canvas->setTool(AnnotationCanvas::Tool::Rectangle);
            }
        }
    });
    popup->adjustSize();
    QPoint pos = sourceBtn->mapToGlobal(QPoint(0, sourceBtn->height() + 2));
    if (screen())
    {
        const QRect screenGeom = screen()->geometry();
        if (pos.x() + popup->width() > screenGeom.right())
        {
            pos.setX(screenGeom.right() - popup->width() - 2);
        }
        if (pos.y() + popup->height() > screenGeom.bottom())
        {
            pos.setY(sourceBtn->mapToGlobal(QPoint(0, -popup->height() - 2)).y());
        }
    }
    popup->move(pos);
    m_cornerRadiusPopup = popup;
    popup->show();
}
void AnnotationOptions::updateAllColorWatchers(const QColor &color)
{
    m_currentAnnotationColor = color;
    const QIcon icon = makeColorSwatchIcon(color);
    for (auto *w : m_colorWatchers)
    {
        if (w)
            w->setIcon(icon);
    }
    const QString colorHex = color.name(QColor::HexRgb).toLower();
    for (const auto &pair : m_paletteButtons)
    {
        auto *btn = pair.first;
        if (!btn)
            continue;
        const bool shouldBeChecked = (pair.second.compare(colorHex, Qt::CaseInsensitive) == 0);
        if (btn->isChecked() != shouldBeChecked)
        {
            QSignalBlocker blocker(btn);
            btn->setChecked(shouldBeChecked);
        }
    }
}
AnnotationOptions::AnnotationOptions(QWidget *parent, std::function<void()> refreshCursor,
                                     QObject *controlEventFilter)
    : QFrame(parent), m_refreshCursor(std::move(refreshCursor)), m_controlEventFilter(controlEventFilter)
{
    setObjectName(QStringLiteral("captureOptions"));
    m_toolOptionsStack = new annotation::SubtoolbarStackedLayout(this);

    // Preserve tool indices without constructing hundreds of unused controls
    // in every prepared or restored pin's toolbar.
    for (size_t index = 0; index < m_createdPages.size(); ++index)
        m_toolOptionsStack->addWidget(new QWidget(this));
}
void AnnotationOptions::showPage(int index)
{
    if (index < 0 || index >= int(m_createdPages.size()))
        return;
    if (!m_createdPages[index])
    {
        using PageFactory = QWidget *(AnnotationOptions::*)();
        static constexpr PageFactory factories[] = {
            &AnnotationOptions::createShapesPage, &AnnotationOptions::createLinesPage,
            &AnnotationOptions::createPenPage, &AnnotationOptions::createMarkerPage,
            &AnnotationOptions::createMosaicPage, &AnnotationOptions::createTextPage,
            &AnnotationOptions::createNumberPage, &AnnotationOptions::createMagnifierPage,
            &AnnotationOptions::createEraserPage};
        const QSignalBlocker blocker(m_toolOptionsStack);
        const int current = m_toolOptionsStack->currentIndex();
        QWidget *placeholder = m_toolOptionsStack->widget(index);
        QWidget *page = (this->*factories[index])();
        m_createdPages[index] = true;
        m_toolOptionsStack->removeWidget(placeholder);
        delete placeholder;
        m_toolOptionsStack->insertWidget(index, page);
        m_toolOptionsStack->setCurrentIndex(current);
        if (m_controlEventFilter)
        {
            page->installEventFilter(m_controlEventFilter);
            for (auto *widget : page->findChildren<QWidget *>())
                if (!widget->isWindow())
                    widget->installEventFilter(m_controlEventFilter);
        }
    }
    m_toolOptionsStack->setCurrentIndex(index);
}
void AnnotationOptions::setCanvas(AnnotationCanvas *canvas)
{
    if (m_canvas)
        disconnect(m_canvas, nullptr, this, nullptr);
    m_canvas = canvas;
    if (!m_canvas)
        return;
    connect(m_canvas, &AnnotationCanvas::penWidthChanged, this,
            &AnnotationOptions::updateDynamicWidthButtons);
    connect(m_canvas, &AnnotationCanvas::penColorChanged, this, &AnnotationOptions::updateAllColorWatchers);
    connect(m_canvas, &AnnotationCanvas::textStyleChanged, this, &AnnotationOptions::syncTextControls);
    connect(m_canvas, &AnnotationCanvas::annotationStyleChanged, this, &AnnotationOptions::syncStyleControls);
    connect(m_canvas, &AnnotationCanvas::penStyleChanged, this, &AnnotationOptions::syncStyleControls);
    connect(m_canvas, &AnnotationCanvas::arrowStyleChanged, this, &AnnotationOptions::syncStyleControls);
    m_currentAnnotationColor = m_canvas->penColor();
    if (isVisible() && m_createdPages[m_toolOptionsStack->currentIndex()])
        syncStyleControls();
}
void AnnotationOptions::closePopups()
{
    if (m_cornerRadiusPopup)
        m_cornerRadiusPopup->hide();
}
QFrame *AnnotationOptions::makeSeparator(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setFrameShape(QFrame::VLine);
    line->setStyleSheet(QStringLiteral("background: #c4c9cf;"));
    line->setFixedSize(1, 19);
    return line;
}
void AnnotationOptions::addWidthPresets(QHBoxLayout *layout, std::initializer_list<int> widths)
{
    auto *group = new QButtonGroup(layout->parentWidget());
    group->setExclusive(true);
    bool checked = true;
    for (int width : widths)
    {
        auto *button =
            makeWidthPreset(layout->parentWidget(), width, QStringLiteral("penWidth%1").arg(width), checked);
        group->addButton(button);
        layout->addWidget(button);
        checked = false;
    }
}

} // namespace snipaste::ui
