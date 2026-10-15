#include "ui/annotation/annotation_canvas.h"
#include "ui/annotation/annotation_option_widgets.h"
#include "ui/annotation/annotation_options.h"
#include "ui/annotation/annotation_toolbar_icons.h"
#include "ui/snip/capture_icons.h"
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QMenu>
#include <QToolButton>
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

    auto *btnArrowDropdown = new QToolButton(pageLines);
    btnArrowDropdown->setObjectName(QStringLiteral("annotationArrowStyle"));
    btnArrowDropdown->setStyleSheet(dropdownButtonStyle());
    btnArrowDropdown->setFixedSize(54, 24);
    btnArrowDropdown->setPopupMode(QToolButton::InstantPopup);
    btnArrowDropdown->setIcon(makeArrowStyleDropdownIcon(AnnotationCanvas::ArrowStyle::End));
    btnArrowDropdown->setIconSize(QSize(50, 20));
    btnArrowDropdown->setToolTip(QStringLiteral("箭头样式"));
    m_arrowStyleButton = btnArrowDropdown;

    auto *menuArrow = new QMenu(btnArrowDropdown);
    menuArrow->setStyleSheet(
        QStringLiteral("QMenu { background: white; border: 1px solid #c4c9cf; padding: 2px; }"
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
    for (const auto &item : arrowStyleItems)
    {
        auto *action = menuArrow->addAction(makeArrowMenuItemIcon(item.second), QString());
        action->setCheckable(true);
        if (item.second == AnnotationCanvas::ArrowStyle::End)
            action->setChecked(true);
        connect(action, &QAction::triggered, this,
                [this, btnArrowDropdown, menuArrow, action, s = item.second] {
                    btnArrowDropdown->setIcon(makeArrowStyleDropdownIcon(s));
                    for (auto *other : menuArrow->actions())
                        other->setChecked(other == action);
                    if (m_canvas)
                    {
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
    btnCurve->setStyleSheet(dropdownButtonStyle());
    btnCurve->setFixedSize(48, 24);
    btnCurve->setPopupMode(QToolButton::InstantPopup);
    btnCurve->setIcon(makeCurveStyleDropdownIcon(0));
    btnCurve->setIconSize(QSize(44, 20));
    btnCurve->setToolTip(QStringLiteral("直线 / 曲线"));

    auto *curveMenu = new QMenu(btnCurve);
    curveMenu->setStyleSheet(
        QStringLiteral("QMenu { background: white; border: 1px solid #c4c9cf; padding: 2px; }"
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
        if (!m_canvas)
            return;
        auto cur = m_canvas->arrowStyle();
        if (cur == AnnotationCanvas::ArrowStyle::End)
            m_canvas->setArrowStyle(AnnotationCanvas::ArrowStyle::Start);
        else if (cur == AnnotationCanvas::ArrowStyle::Start)
            m_canvas->setArrowStyle(AnnotationCanvas::ArrowStyle::End);
        else if (cur == AnnotationCanvas::ArrowStyle::HollowEnd)
            m_canvas->setArrowStyle(AnnotationCanvas::ArrowStyle::HollowStart);
        else if (cur == AnnotationCanvas::ArrowStyle::HollowStart)
            m_canvas->setArrowStyle(AnnotationCanvas::ArrowStyle::HollowEnd);
    });

    auto *sepLines = new QFrame(pageLines);
    sepLines->setFrameShape(QFrame::VLine);
    sepLines->setStyleSheet(QStringLiteral("background: #c4c9cf;"));
    sepLines->setFixedSize(1, 19);
    layoutLines->addWidget(sepLines, 0, Qt::AlignVCenter);

    layoutLines->addWidget(makeColorModule(pageLines));
    return pageLines;
}
} // namespace snipaste::ui
