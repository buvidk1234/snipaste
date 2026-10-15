#pragma once

#include "core/annotation/annotation_types.h"
#include <utility>

namespace snipaste::core
{

// Vector state and gesture history, independent of widgets and pixel buffers.
class AnnotationDocument
{
  public:
    const QVector<AnnotationShape> &shapes() const { return m_current.shapes; }
    quint64 revision() const { return m_revision; }
    int append(AnnotationShape shape);
    bool remove(int index);
    void clear();
    // Edits stay within the document so renderers can always detect changes.
    // commit() groups a complete gesture into one undo step.
    template <typename Edit> bool editShape(int index, Edit &&edit)
    {
        if (index < 0 || index >= m_current.shapes.size())
            return false;
        std::forward<Edit>(edit)(m_current.shapes[index]);
        ++m_revision;
        return true;
    }
    template <typename Edit> bool editShapes(Edit &&edit)
    {
        bool changed = false;
        for (auto &shape : m_current.shapes)
            changed = edit(shape) || changed;
        if (changed)
            ++m_revision;
        return changed;
    }
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
    struct State
    {
        QVector<AnnotationShape> shapes;
        int nextNumber = 1;
        bool operator==(const State &) const = default;
    };
    State m_current;
    State m_committed;
    QVector<State> m_undo;
    QVector<State> m_redo;
    quint64 m_revision = 0;
};

} // namespace snipaste::core
