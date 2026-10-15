#include "ui/annotation/annotation_canvas.h"
#include "ui/annotation/annotation_option_widgets.h"
#include "ui/annotation/annotation_options.h"
#include "ui/annotation/annotation_toolbar_icons.h"
#include "ui/snip/capture_icons.h"
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMenu>
#include <QToolButton>
#include <QWidgetAction>
namespace snipaste::ui
{
using namespace annotation;
QWidget *AnnotationOptions::createShapesPage()
{
    auto *pageShapes = new QWidget(this);
    auto *layoutShapes = new QHBoxLayout(pageShapes);
    layoutShapes->setSizeConstraint(QLayout::SetFixedSize);
    layoutShapes->setContentsMargins(0, 0, 0, 0);
    layoutShapes->setSpacing(2);

    addWidthPresets(layoutShapes, {2, 4, 8});

    auto *btnFill = new QToolButton(pageShapes);
    btnFill->setObjectName(QStringLiteral("annotationShapeFilled"));
    btnFill->setIcon(captureIcon(CaptureIcon::FillPattern));
    btnFill->setFixedSize(24, 24);
    btnFill->setCheckable(true);
    btnFill->setToolTip(QStringLiteral("填充图形"));
    btnFill->setFocusPolicy(Qt::NoFocus);
    layoutShapes->addWidget(btnFill);
    connect(btnFill, &QToolButton::toggled, this, [this](bool f) {
        if (m_canvas)
            m_canvas->setShapeFilled(f);
    });

    layoutShapes->addWidget(makeSeparator(pageShapes));

    auto *shapeModeGroup = new QButtonGroup(pageShapes);
    shapeModeGroup->setExclusive(true);
    auto *btnRect = new QToolButton(pageShapes);
    btnRect->setObjectName(QStringLiteral("annotationRectangle"));
    btnRect->setIcon(captureIcon(CaptureIcon::Rectangle));
    btnRect->setFixedSize(24, 24);
    btnRect->setCheckable(true);
    btnRect->setChecked(true);
    btnRect->setToolTip(QStringLiteral("矩形（点击或悬停设置圆角半径）"));
    btnRect->setFocusPolicy(Qt::NoFocus);
    shapeModeGroup->addButton(btnRect);
    layoutShapes->addWidget(btnRect);

    auto *btnEllipse = new QToolButton(pageShapes);
    btnEllipse->setObjectName(QStringLiteral("annotationEllipse"));
    btnEllipse->setIcon(captureIcon(CaptureIcon::Ellipse));
    btnEllipse->setFixedSize(24, 24);
    btnEllipse->setCheckable(true);
    btnEllipse->setToolTip(QStringLiteral("椭圆"));
    btnEllipse->setFocusPolicy(Qt::NoFocus);
    shapeModeGroup->addButton(btnEllipse);
    layoutShapes->addWidget(btnEllipse);

    btnRect->installEventFilter(new HoverPopupFilter(
        [this, btnRect] {
            if (btnRect && btnRect->underMouse())
            {
                showCornerRadiusPopup(btnRect);
            }
        },
        btnRect));
    connect(btnRect, &QToolButton::clicked, this, [this, btnRect] {
        if (!m_canvas)
            return;
        m_canvas->setTool(m_canvas->cornerRadius() > 0 ? AnnotationCanvas::Tool::RoundedRectangle
                                                       : AnnotationCanvas::Tool::Rectangle);
        refreshAnnotationCursor();
        showCornerRadiusPopup(btnRect);
    });
    connect(btnEllipse, &QToolButton::clicked, this, [this] {
        if (m_cornerRadiusPopup)
        {
            m_cornerRadiusPopup->close();
        }
        if (!m_canvas)
            return;
        m_canvas->setTool(AnnotationCanvas::Tool::Ellipse);
        refreshAnnotationCursor();
    });

    layoutShapes->addWidget(makeSeparator(pageShapes));
    layoutShapes->addWidget(makeLineStyleDropdown(pageShapes));
    layoutShapes->addWidget(makeSeparator(pageShapes));
    layoutShapes->addWidget(makeColorModule(pageShapes));
    return pageShapes;
}

QWidget *AnnotationOptions::createLinesPage()
{
    auto *pageLines = new QWidget(this);
    auto *layoutLines = new QHBoxLayout(pageLines);
    layoutLines->setSizeConstraint(QLayout::SetFixedSize);
    layoutLines->setContentsMargins(0, 0, 0, 0);
    layoutLines->setSpacing(2);

    addWidthPresets(layoutLines, {2, 4, 8});

    layoutLines->addWidget(makeSeparator(pageLines));

    class ArrowMenu : public QMenu
    {
    public:
        explicit ArrowMenu(QWidget *parent = nullptr) : QMenu(parent)
        {
            setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
            setAttribute(Qt::WA_TranslucentBackground, false);
            setContentsMargins(0, 0, 0, 0);
        }
        QSize sizeHint() const override { return QSize(54, 194); }
    };

    const auto setupArrowPopup = [this](QToolButton *btn, bool isStart) {
        btn->setFixedSize(54, 24);
        btn->setIconSize(QSize(50, 20));

        auto *menu = new ArrowMenu(btn);
        menu->setStyleSheet(QStringLiteral(
            "QMenu { background: #ffffff; border: 1px solid #7f7f7f; border-radius: 0px; padding: 0px; margin: 0px; }"
        ));
        menu->setFixedSize(54, 194);

        auto *widgetAction = new QWidgetAction(menu);
        auto *listWidget = new QListWidget(menu);
        listWidget->setFixedSize(52, 192);
        listWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        listWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        listWidget->setFocusPolicy(Qt::NoFocus);
        listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
        listWidget->setIconSize(QSize(34, 22));
        listWidget->setSpacing(0);
        listWidget->setContentsMargins(0, 0, 0, 0);
        listWidget->setStyleSheet(QStringLiteral(
            "QListWidget { background: #ffffff; border: none; outline: none; padding: 0px; margin: 0px; }"
            "QListWidget::item { height: 22px; margin: 0px; padding: 0px; border: 1px solid transparent; border-radius: 2px; }"
            "QListWidget::item:hover { background: #cde8ff; border: 1px solid #2889ed; }"
            "QListWidget::item:selected { background: #a3d2ff; border: 1px solid #1c85e8; }"
        ));

        connect(menu, &QMenu::aboutToShow, this, [this, listWidget, isStart] {
            if (!m_canvas)
                return;
            const auto head = isStart ? m_canvas->startArrowHead() : m_canvas->endArrowHead();
            for (int i = 0; i < listWidget->count(); ++i)
            {
                auto *it = listWidget->item(i);
                it->setSelected(it->data(Qt::UserRole).toInt() == int(head));
            }
            listWidget->scrollToItem(listWidget->item(0), QAbstractItemView::PositionAtTop);
        });

        const QList<QPair<QString, core::ArrowHead>> items{
            {QStringLiteral("无"), core::ArrowHead::None},
            {QStringLiteral("垂直扁平"), core::ArrowHead::Flat},
            {QStringLiteral("圆点"), core::ArrowHead::Circle},
            {QStringLiteral("菱形"), core::ArrowHead::Diamond},
            {QStringLiteral("尖角三角"), core::ArrowHead::Triangle},
            {QStringLiteral("经典开角"), core::ArrowHead::Classic},
            {QStringLiteral("凹底燕尾"), core::ArrowHead::Stealth},
            {QStringLiteral("空心三角"), core::ArrowHead::Hollow},
        };

        for (const auto &item : items)
        {
            auto *listItem = new QListWidgetItem(listWidget);
            listItem->setSizeHint(QSize(50, 24));
            const QIcon icon = isStart ? makeStartArrowMenuItemIcon(item.second, 34, 22)
                                       : makeEndArrowMenuItemIcon(item.second, 34, 22);
            listItem->setIcon(icon);
            listItem->setData(Qt::UserRole, int(item.second));
            listItem->setToolTip(item.first);
            const bool defSelected = isStart ? (item.second == core::ArrowHead::None)
                                             : (item.second == core::ArrowHead::Classic);
            if (defSelected)
                listItem->setSelected(true);
        }

        widgetAction->setDefaultWidget(listWidget);
        menu->addAction(widgetAction);

        const auto onSelectHead = [this, btn, menu, listWidget, isStart](core::ArrowHead head) {
            const QIcon dropIcon = isStart ? makeStartArrowDropdownIcon(head, 50, 22)
                                           : makeEndArrowDropdownIcon(head, 50, 22);
            btn->setIcon(dropIcon);
            for (int i = 0; i < listWidget->count(); ++i)
            {
                auto *it = listWidget->item(i);
                it->setSelected(it->data(Qt::UserRole).toInt() == int(head));
            }
            menu->close();
            if (!m_canvas)
                return;
            if (isStart)
                m_canvas->setStartArrowHead(head);
            else
                m_canvas->setEndArrowHead(head);

            if (m_canvas->arrowStyle() != core::ArrowStyle::None)
                m_lastArrowStyle = m_canvas->arrowStyle();

            if (m_toggleArrowButton)
            {
                const bool active = (m_canvas->arrowStyle() != core::ArrowStyle::None &&
                                     m_canvas->tool() != AnnotationCanvas::Tool::Line);
                m_toggleArrowButton->setChecked(active);
                m_toggleArrowButton->setIcon(makeToggleArrowIcon(active));
            }
            refreshAnnotationCursor();
        };

        connect(listWidget, &QListWidget::itemClicked, this, [onSelectHead](QListWidgetItem *it) {
            onSelectHead(core::ArrowHead(it->data(Qt::UserRole).toInt()));
        });

        btn->setMenu(menu);
    };

    // 1. 左箭头 (起点箭头样式下拉选择器)
    auto *btnStartArrow = new QToolButton(pageLines);
    btnStartArrow->setObjectName(QStringLiteral("annotationStartArrowStyle"));
    btnStartArrow->setStyleSheet(dropdownButtonStyle());
    btnStartArrow->setPopupMode(QToolButton::InstantPopup);
    btnStartArrow->setIcon(makeStartArrowDropdownIcon(core::ArrowHead::None, 50, 22));
    btnStartArrow->setToolTip(QStringLiteral("起点箭头样式"));
    m_startArrowButton = btnStartArrow;
    setupArrowPopup(btnStartArrow, true);
    layoutLines->addWidget(btnStartArrow);

    // 2. 中间箭头 (线条线型下拉选择器，72px)
    layoutLines->addWidget(makeLineStyleDropdown(pageLines));

    // 3. 右箭头 (终点箭头样式下拉选择器)
    auto *btnEndArrow = new QToolButton(pageLines);
    btnEndArrow->setObjectName(QStringLiteral("annotationEndArrowStyle"));
    btnEndArrow->setStyleSheet(dropdownButtonStyle());
    btnEndArrow->setPopupMode(QToolButton::InstantPopup);
    btnEndArrow->setIcon(makeEndArrowDropdownIcon(core::ArrowHead::Classic, 50, 22));
    btnEndArrow->setToolTip(QStringLiteral("终点箭头样式"));
    m_endArrowButton = btnEndArrow;
    m_arrowStyleButton = btnEndArrow;
    setupArrowPopup(btnEndArrow, false);
    layoutLines->addWidget(btnEndArrow);

    // 4. 开关箭头 (开/关箭头按钮，快捷键 Tab)
    auto *btnToggleArrow = new QToolButton(pageLines);
    btnToggleArrow->setObjectName(QStringLiteral("annotationToggleArrow"));
    btnToggleArrow->setFixedSize(24, 24);
    btnToggleArrow->setCheckable(true);
    btnToggleArrow->setChecked(true);
    btnToggleArrow->setIcon(makeToggleArrowIcon(true));
    btnToggleArrow->setIconSize(QSize(20, 20));
    btnToggleArrow->setToolTip(QStringLiteral("开/关箭头 (Tab)"));
    btnToggleArrow->setFocusPolicy(Qt::NoFocus);
    m_toggleArrowButton = btnToggleArrow;

    connect(btnToggleArrow, &QToolButton::clicked, this, [this, btnStartArrow, btnEndArrow, btnToggleArrow] {
        if (!m_canvas)
            return;
        const bool hasArrow = (m_canvas->arrowStyle() != core::ArrowStyle::None) &&
                              (m_canvas->tool() != AnnotationCanvas::Tool::Line);
        if (hasArrow)
        {
            if (m_canvas->arrowStyle() != core::ArrowStyle::None)
                m_lastArrowStyle = m_canvas->arrowStyle();
            m_canvas->setArrowStyle(core::ArrowStyle::None);
            m_canvas->setTool(AnnotationCanvas::Tool::Line);
            btnStartArrow->setIcon(makeStartArrowDropdownIcon(core::ArrowHead::None, 50, 22));
            btnEndArrow->setIcon(makeEndArrowDropdownIcon(core::ArrowHead::None, 50, 22));
            btnToggleArrow->setChecked(false);
            btnToggleArrow->setIcon(makeToggleArrowIcon(false));
        }
        else
        {
            const auto target = (m_lastArrowStyle != core::ArrowStyle::None)
                                    ? m_lastArrowStyle
                                    : core::ArrowStyle::End;
            m_canvas->setArrowStyle(target);
            m_canvas->setTool(AnnotationCanvas::Tool::Arrow);
            btnStartArrow->setIcon(makeStartArrowDropdownIcon(core::startArrowHead(target), 50, 22));
            btnEndArrow->setIcon(makeEndArrowDropdownIcon(core::endArrowHead(target), 50, 22));
            btnToggleArrow->setChecked(true);
            btnToggleArrow->setIcon(makeToggleArrowIcon(true));
        }
        refreshAnnotationCursor();
    });
    layoutLines->addWidget(btnToggleArrow);

    layoutLines->addWidget(makeSeparator(pageLines));

    layoutLines->addWidget(makeColorModule(pageLines));
    return pageLines;
}
} // namespace snipaste::ui
