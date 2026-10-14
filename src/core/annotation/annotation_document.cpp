#include "core/annotation/annotation_document.h"

#include <utility>

namespace snipaste::core {

bool AnnotationDocument::commit()
{
    if (m_current == m_committed)
        return false;
    m_undo.append(std::move(m_committed));
    m_committed = m_current;
    m_redo.clear();
    return true;
}

bool AnnotationDocument::undo()
{
    if (!canUndo())
        return false;
    m_redo.append(std::move(m_committed));
    m_committed = m_undo.takeLast();
    m_current = m_committed;
    return true;
}

bool AnnotationDocument::redo()
{
    if (!canRedo())
        return false;
    m_undo.append(std::move(m_committed));
    m_committed = m_redo.takeLast();
    m_current = m_committed;
    return true;
}

void AnnotationDocument::translate(const QPointF &delta)
{
    if (delta.isNull())
        return;
    const auto shift = [&delta](State &state) {
        for (auto &shape : state.shapes)
            translateShape(shape, delta);
    };
    shift(m_current);
    shift(m_committed);
    for (auto &state : m_undo) shift(state);
    for (auto &state : m_redo) shift(state);
}

void AnnotationDocument::reset()
{
    m_current = {};
    m_committed = {};
    m_undo.clear();
    m_redo.clear();
}

} // namespace snipaste::core
