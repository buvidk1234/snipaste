#include "core/annotation/annotation_types.h"

namespace snipaste::core {

bool isNonMovableBrush(AnnotationTool tool)
{
    return tool == AnnotationTool::Pen
        || tool == AnnotationTool::Marker
        || tool == AnnotationTool::Mosaic
        || tool == AnnotationTool::Blur;
}

void translateShape(AnnotationShape &shape, const QPointF &delta)
{
    shape.start += delta;
    shape.end += delta;
    shape.sourceRect.translate(delta);
    for (QPointF &point : shape.points)
        point += delta;
    shape.erasedPath.translate(delta);
}

} // namespace snipaste::core
