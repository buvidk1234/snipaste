#pragma once

#include "core/annotation/annotation_types.h"

namespace snipaste::core {

// Vector state and gesture history, independent of widgets and pixel buffers.
class AnnotationDocument
{
public:
    QVector<AnnotationShape> &shapes() { return m_current.shapes; }
    const QVector<AnnotationShape> &shapes() const { return m_current.shapes; }
    int nextNumber() const { return m_current.nextNumber; }
    int takeNumber() { return m_current.nextNumber++; }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    bool commit();
    bool undo();
    bool redo();
    void translate(const QPointF &delta);
    void reset();

private:
    struct State {
        QVector<AnnotationShape> shapes;
        int nextNumber = 1;
        bool operator==(const State &) const = default;
    };
    State m_current;
    State m_committed;
    QVector<State> m_undo;
    QVector<State> m_redo;
};

} // namespace snipaste::core
