#pragma once
#include "core/annotation/annotation_types.h"
#include <QIcon>
namespace snipaste::ui::annotation
{
QIcon makeColorSwatchIcon(const QColor &color);
QIcon makeDynamicWidthIcon(qreal width, bool checked = true);
QIcon makeLineStyleDropdownIcon(Qt::PenStyle style, int w = 68, int h = 20);
QIcon makeArrowStyleDropdownIcon(core::ArrowStyle style, int w = 52, int h = 22);
QIcon makeCurveStyleDropdownIcon(int curveStyle, int w = 48, int h = 22);
QIcon makeStartArrowDropdownIcon(core::ArrowHead style, int w = 50, int h = 22);
QIcon makeStartArrowMenuItemIcon(core::ArrowHead style, int w = 34, int h = 22);
QIcon makeEndArrowDropdownIcon(core::ArrowHead style, int w = 50, int h = 22);
QIcon makeEndArrowMenuItemIcon(core::ArrowHead style, int w = 34, int h = 22);
QIcon makeToggleArrowIcon(bool active, int w = 20, int h = 20);
QIcon makeLineMenuItemIcon(Qt::PenStyle style, int w = 64, int h = 18);
QIcon makeArrowMenuItemIcon(core::ArrowStyle style, int w = 64, int h = 18);
QIcon makeMagnifierConnectorDropdownIcon(int mode, int w = 48, int h = 22);
QIcon makeMagnifierConnectorMenuItemIcon(int mode, int w = 44, int h = 18);
} // namespace snipaste::ui::annotation
