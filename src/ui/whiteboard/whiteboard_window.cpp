#include "ui/whiteboard/whiteboard_window.h"
#include <QKeyEvent>
#include <QPainter>
#include <QScreen>
#include <utility>

namespace snipaste::ui {

WhiteboardWindow::WhiteboardWindow(QScreen *screen, QColor color, bool transparent, QWidget *parent)
    : QWidget(parent)
    , m_color(std::move(color))
    , m_transparent(transparent)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setFocusPolicy(Qt::StrongFocus);
    if (m_transparent)
        setAttribute(Qt::WA_TranslucentBackground);
    if (screen)
        setGeometry(screen->geometry());
}

void WhiteboardWindow::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    QColor fill = m_color;
    if (m_transparent)
        fill.setAlpha(80);
    painter.fillRect(rect(), fill);
}

void WhiteboardWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape)
        close();
    else
        QWidget::keyPressEvent(event);
}

} // namespace snipaste::ui
