#pragma once

#include <QIcon>

namespace snipaste::ui {

enum class CaptureIcon { Rectangle, Ellipse, Line, Arrow, Pen, Marker, Mosaic, Blur,
                         Text, Eraser, Undo, Redo, Cancel, Save, Paste, Finish,
                         Shapes, Number, Magnifier, Ocr, Share, More, RoundedCorners, StraightCorners,
                         SelectScreen, Refresh, Scroll,
                         FillPattern, PatternRect, PatternEllipse,
                         ArrowHeadPlain, ArrowHeadFlat, ArrowHeadEnd, ArrowHeadStart,
                         ArrowHeadBoth, ArrowHeadHollowEnd, ArrowHeadHollowStart, ArrowHeadHollowBoth,
                         SwapDirection,
                         TextBold, TextItalic, TextOutline, TextBackground,
                         NumberFilled, NumberDoubleRing, NumberOutline,
                         MagnifierCutout, MagnifierBorder, FinishEditing };

QIcon captureIcon(CaptureIcon kind, bool light = false);

} // namespace snipaste::ui
