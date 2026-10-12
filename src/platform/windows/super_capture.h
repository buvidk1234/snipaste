#pragma once

#include <QObject>
#include <QPoint>
#include <QRect>
#include <Qt>

namespace snipate::platform {

class SuperCapture final : public QObject
{
    Q_OBJECT
public:
    explicit SuperCapture(QObject *parent = nullptr);
    ~SuperCapture() override;

    void setEnabled(bool enabled);
    void setModifier(Qt::KeyboardModifier modifier);
    void setMouseButton(Qt::MouseButton button);
    bool isEnabled() const { return m_enabled; }

signals:
    void rectangleCaptured(const QRect &globalRect);

private:
#ifdef Q_OS_WIN
    static long __stdcall hookProc(int code, quintptr wParam, qintptr lParam);
    bool handleMouseMessage(quintptr message, const QPoint &globalPosition);
    static SuperCapture *s_instance;
    void installHook();
    void uninstallHook();
    void finishCapture(const QPoint &globalPosition);
    void cancelCapture();
    void *m_hook = nullptr;
#endif
    bool m_enabled = false;
    Qt::KeyboardModifier m_modifier = Qt::NoModifier;
    Qt::MouseButton m_button = Qt::LeftButton;
    bool m_dragging = false;
    QPoint m_start;
};

} // namespace snipate::platform
