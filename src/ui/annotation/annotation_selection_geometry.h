#pragma once
#include "core/annotation/annotation_types.h"
#include <QTransform>
#include <array>

namespace snipaste::ui::annotation
{
QRectF selectionRect(const core::AnnotationShape &shape);
QTransform selectionTransform(const core::AnnotationShape &shape);
std::array<QPointF, 8> selectionHandles(const QRectF &rect);
bool hasEdgeHandles(const core::AnnotationShape &shape, qreal tolerance = 7.0);
int selectionHandleAt(const core::AnnotationShape &shape, QPointF point, qreal tolerance);
bool hasRotationHandle(const core::AnnotationShape &shape);
QPointF rotationHandle(const core::AnnotationShape &shape);
bool selectable(const core::AnnotationShape &shape);
bool containsShape(const core::AnnotationShape &shape, QPointF point, qreal tolerance);
bool textInteriorContains(const core::AnnotationShape &shape, QPointF point, qreal edgeWidth);
void resizeShape(core::AnnotationShape &shape, int handle, QPointF delta);
} // namespace snipaste::ui::annotation
