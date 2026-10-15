#include "ui/annotation/annotation_canvas.h"
#include "ui/annotation/annotation_option_widgets.h"
#include "ui/annotation/annotation_options.h"
#include "ui/annotation/annotation_toolbar_icons.h"
#include "ui/snip/capture_icons.h"
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QToolButton>
namespace snipaste::ui
{
using namespace annotation;
QWidget *AnnotationOptions::createPenPage()
{
    auto *pagePen = new QWidget(this);
    auto *layoutPen = new QHBoxLayout(pagePen);
    layoutPen->setSizeConstraint(QLayout::SetFixedSize);
    layoutPen->setContentsMargins(0, 0, 0, 0);
    layoutPen->setSpacing(2);

    addWidthPresets(layoutPen, {2, 4, 8});
    layoutPen->addWidget(makeSeparator(pagePen));
    layoutPen->addWidget(makeLineStyleDropdown(pagePen));
    layoutPen->addWidget(makeSeparator(pagePen));
    layoutPen->addWidget(makeColorModule(pagePen));
    return pagePen;
}

QWidget *AnnotationOptions::createMarkerPage()
{
    auto *pageMarker = new QWidget(this);
    auto *layoutMarker = new QHBoxLayout(pageMarker);
    layoutMarker->setSizeConstraint(QLayout::SetFixedSize);
    layoutMarker->setContentsMargins(0, 0, 0, 0);
    layoutMarker->setSpacing(2);

    addWidthPresets(layoutMarker, {16, 23, 31});
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
    return pageMarker;
}

QWidget *AnnotationOptions::createEraserPage()
{
    auto *pageEraser = new QWidget(this);
    auto *layoutEraser = new QHBoxLayout(pageEraser);
    layoutEraser->setSizeConstraint(QLayout::SetFixedSize);
    layoutEraser->setContentsMargins(0, 0, 0, 0);
    layoutEraser->setSpacing(2);

    auto *eraserPresetGroup = new QButtonGroup(pageEraser);
    eraserPresetGroup->setExclusive(true);
    for (int w : {16, 24, 31})
    {
        auto *b = makeWidthPreset(pageEraser, w, QStringLiteral("penWidth%1").arg(w), w == 16);
        eraserPresetGroup->addButton(b);
        layoutEraser->addWidget(b);
        if (w == 16)
            b->setChecked(true);
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

    return pageEraser;
}
} // namespace snipaste::ui
