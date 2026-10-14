#pragma once

#include <QWidget>
#include <QColor>

class QScreen;

namespace snipaste::ui {

// A background window; drawing is owned by the capture/annotation canvas.
class WhiteboardWindow final : public QWidget
{
public:
    WhiteboardWindow(QScreen *screen, QColor color, bool transparent, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    QColor m_color;
    bool m_transparent = false;
};

} // namespace snipaste::ui
