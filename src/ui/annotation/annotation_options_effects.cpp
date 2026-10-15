#include "ui/annotation/annotation_canvas.h"
#include "ui/annotation/annotation_option_widgets.h"
#include "ui/annotation/annotation_options.h"
#include "ui/annotation/annotation_toolbar_icons.h"
#include "ui/snip/capture_icons.h"
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QToolButton>
namespace snipaste::ui
{
using namespace annotation;
QWidget *AnnotationOptions::createMosaicPage()
{
    auto *pageMosaic = new QWidget(this);
    auto *layoutMosaic = new QHBoxLayout(pageMosaic);
    layoutMosaic->setSizeConstraint(QLayout::SetFixedSize);
    layoutMosaic->setContentsMargins(0, 0, 0, 0);
    layoutMosaic->setSpacing(2);

    auto *mosaicPresetGroup = new QButtonGroup(pageMosaic);
    mosaicPresetGroup->setExclusive(true);
    for (int w : {4, 16, 24})
    {
        auto *b = makeWidthPreset(pageMosaic, w, QStringLiteral("penWidth%1").arg(w), w == 4);
        if (w == 4)
        {
            b->setText(QStringLiteral("4"));
        }
        mosaicPresetGroup->addButton(b);
        layoutMosaic->addWidget(b);
        connect(b, &QToolButton::clicked, this, [this] {
            if (m_canvas)
                m_canvas->setEffectShape(AnnotationCanvas::EffectShape::Brush);
        });
        if (w == 4)
            b->setChecked(true);
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
        if (!m_canvas)
            return;
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
        if (!m_canvas)
            return;
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
        if (!m_canvas)
            return;
        m_canvas->setTool(blur ? AnnotationCanvas::Tool::Blur : AnnotationCanvas::Tool::Mosaic);
        refreshAnnotationCursor();
        QTimer::singleShot(0, this, [this] { refreshAnnotationCursor(); });
    });

    auto *sliderEffect = new TickSlider(Qt::Horizontal, pageMosaic);
    sliderEffect->setObjectName(QStringLiteral("annotationEffectSize"));
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
        if (m_canvas)
        {
            m_canvas->setEffectSize(val);
            refreshAnnotationCursor();
        }
        labelEffectVal->setText(QString::number(val));
    });
    return pageMosaic;
}

QWidget *AnnotationOptions::createMagnifierPage()
{
    auto *pageMagnifier = new QWidget(this);
    auto *layoutMagnifier = new QHBoxLayout(pageMagnifier);
    layoutMagnifier->setSizeConstraint(QLayout::SetFixedSize);
    layoutMagnifier->setContentsMargins(0, 0, 0, 0);
    layoutMagnifier->setSpacing(2);

    addWidthPresets(layoutMagnifier, {2, 4, 8});

    layoutMagnifier->addWidget(makeSeparator(pageMagnifier));

    auto *magShapeGroup = new QButtonGroup(pageMagnifier);
    magShapeGroup->setExclusive(true);

    auto *btnMagRect = new QToolButton(pageMagnifier);
    btnMagRect->setObjectName(QStringLiteral("annotationMagnifierRectangle"));
    btnMagRect->setIcon(captureIcon(CaptureIcon::Rectangle));
    btnMagRect->setFixedSize(24, 24);
    btnMagRect->setCheckable(true);
    btnMagRect->setChecked(true);
    btnMagRect->setToolTip(QStringLiteral("矩形放大镜（点击或悬停设置圆角半径）"));
    btnMagRect->setFocusPolicy(Qt::NoFocus);
    magShapeGroup->addButton(btnMagRect);
    layoutMagnifier->addWidget(btnMagRect);

    auto *btnMagEllipse = new QToolButton(pageMagnifier);
    btnMagEllipse->setObjectName(QStringLiteral("annotationMagnifierEllipse"));
    btnMagEllipse->setIcon(captureIcon(CaptureIcon::Ellipse));
    btnMagEllipse->setFixedSize(24, 24);
    btnMagEllipse->setCheckable(true);
    btnMagEllipse->setToolTip(QStringLiteral("椭圆放大镜"));
    btnMagEllipse->setFocusPolicy(Qt::NoFocus);
    magShapeGroup->addButton(btnMagEllipse);
    layoutMagnifier->addWidget(btnMagEllipse);

    btnMagRect->installEventFilter(new HoverPopupFilter(
        [this, btnMagRect] {
            if (btnMagRect && btnMagRect->underMouse())
            {
                showCornerRadiusPopup(btnMagRect);
            }
        },
        btnMagRect));
    connect(btnMagRect, &QToolButton::clicked, this, [this, btnMagRect] {
        if (!m_canvas)
            return;
        m_canvas->setEffectShape(AnnotationCanvas::EffectShape::Rectangle);
        refreshAnnotationCursor();
        showCornerRadiusPopup(btnMagRect);
    });
    connect(btnMagEllipse, &QToolButton::clicked, this, [this] {
        if (m_cornerRadiusPopup)
        {
            m_cornerRadiusPopup->close();
        }
        if (!m_canvas)
            return;
        m_canvas->setEffectShape(AnnotationCanvas::EffectShape::Ellipse);
        refreshAnnotationCursor();
    });

    layoutMagnifier->addWidget(makeSeparator(pageMagnifier));

    auto *btnMagCutout = new QToolButton(pageMagnifier);
    btnMagCutout->setObjectName(QStringLiteral("annotationMagnifierCutout"));
    btnMagCutout->setIcon(captureIcon(CaptureIcon::MagnifierCutout));
    btnMagCutout->setFixedSize(24, 24);
    btnMagCutout->setCheckable(true);
    btnMagCutout->setChecked(true);
    btnMagCutout->setToolTip(QStringLiteral("截取边框"));
    btnMagCutout->setFocusPolicy(Qt::NoFocus);
    layoutMagnifier->addWidget(btnMagCutout);
    connect(btnMagCutout, &QToolButton::toggled, this, [this](bool c) {
        if (m_canvas)
            m_canvas->setMagnifierCutout(c);
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
    sliderMagZoom->setObjectName(QStringLiteral("annotationMagnification"));
    sliderMagZoom->setRange(120, 400);
    sliderMagZoom->setValue(200);
    sliderMagZoom->setFixedSize(66, 24);
    sliderMagZoom->setToolTip(QStringLiteral("放大倍数"));
    layoutMagnifier->addWidget(sliderMagZoom);

    auto *labelZoomVal = new QLabel(QStringLiteral("2"), pageMagnifier);
    labelZoomVal->setObjectName(QStringLiteral("annotationMagnificationValue"));
    labelZoomVal->setFixedSize(30, 24);
    labelZoomVal->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    labelZoomVal->setStyleSheet(QStringLiteral("QLabel { color: #222222; font-size: 11px; font-family: "
                                               "'Segoe UI', 'Microsoft YaHei UI', sans-serif; }"));
    layoutMagnifier->addWidget(labelZoomVal);

    connect(sliderMagZoom, &QSlider::valueChanged, this, [this, labelZoomVal](int v) {
        const qreal mag = v / 100.0;
        if (m_canvas)
            m_canvas->setMagnification(mag);
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
    btnMagConn->setStyleSheet(dropdownButtonStyle());
    btnMagConn->setFixedSize(52, 24);
    btnMagConn->setPopupMode(QToolButton::InstantPopup);
    btnMagConn->setIcon(makeMagnifierConnectorDropdownIcon(0));
    btnMagConn->setIconSize(QSize(48, 20));
    btnMagConn->setToolTip(QStringLiteral("指示线样式"));

    auto *menuConn = new QMenu(btnMagConn);
    menuConn->setStyleSheet(
        QStringLiteral("QMenu { background: white; border: 1px solid #c4c9cf; padding: 2px; }"
                       "QMenu::item { padding: 4px 10px; font-size: 12px; }"
                       "QMenu::item:selected { background: #d6e8fa; color: #111; }"));
    auto *actStraight =
        menuConn->addAction(makeMagnifierConnectorMenuItemIcon(0), QStringLiteral("直线指示"));
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
        if (m_canvas)
            m_canvas->setMagnifierConnectorStyle(mode);
    };
    connect(actStraight, &QAction::triggered, this, [=] { setConnMode(0); });
    connect(actBroken, &QAction::triggered, this, [=] { setConnMode(1); });
    connect(actNone, &QAction::triggered, this, [=] { setConnMode(2); });
    btnMagConn->setMenu(menuConn);
    layoutMagnifier->addWidget(btnMagConn);

    layoutMagnifier->addWidget(makeSeparator(pageMagnifier));
    layoutMagnifier->addWidget(makeColorModule(pageMagnifier));
    return pageMagnifier;
}
} // namespace snipaste::ui
