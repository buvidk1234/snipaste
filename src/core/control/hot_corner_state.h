#pragma once

#include <QPoint>
#include <QRect>

namespace snipaste::core {

// Pure dwell state. Clock, cursor and screen are inputs so monitor changes,
// suspension and re-entry can be tested without moving the desktop mouse.
class HotCornerState final
{
public:
    void reset() { m_corner = -1; m_triggered = false; }

    int sample(QPoint cursor, QRect screen, int size, qint64 now, int delay, bool allowed)
    {
        if (!allowed || !screen.contains(cursor)) { reset(); return -1; }
        size = qBound(1, size, qMin(screen.width(), screen.height()) / 2 + 1);
        int corner = -1;
        const bool left = cursor.x() < screen.left() + size;
        const bool right = cursor.x() > screen.right() - size;
        const bool top = cursor.y() < screen.top() + size;
        const bool bottom = cursor.y() > screen.bottom() - size;
        if (top && left) corner = 0;
        else if (top && right) corner = 1;
        else if (bottom && left) corner = 2;
        else if (bottom && right) corner = 3;
        if (corner < 0) { reset(); return -1; }
        if (corner != m_corner || screen != m_screen || size != m_size) {
            m_corner = corner;
            m_screen = screen;
            m_size = size;
            m_entered = now;
            m_triggered = false;
        }
        if (m_triggered || now - m_entered < qMax(0, delay)) return -1;
        // Latch before the caller dispatches a command, which may run a nested event loop.
        m_triggered = true;
        return corner;
    }

private:
    int m_corner = -1;
    int m_size = 0;
    QRect m_screen;
    qint64 m_entered = 0;
    bool m_triggered = false;
};

} // namespace snipaste::core
