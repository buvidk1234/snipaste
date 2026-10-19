#pragma once

#include <QObject>
#include <QPoint>
#include <QRect>
#include <Qt>

namespace snipaste::platform {

class SuperCapture final : public QObject
{
    Q_OBJECT
public:
    explicit SuperCapture(QObject *parent = nullptr);
    ~SuperCapture() override;

    void setEnabled(bool enabled);
    void setModifier(Qt::KeyboardModifier modifier);
    void setMouseButton(Qt::MouseButton button);
    // The Windows super-capture gesture starts with the Win key.  Suppress
    // the configured side while the low-level hook is active so the shell
    // does not open its Start menu or task switcher during the gesture.
    void setBlockWindowsKeys(bool left, bool right);
    bool isEnabled() const { return m_enabled; }

signals:
    void rectangleCaptured(const QRect &globalRect);

private:
#ifdef Q_OS_WIN
    static long __stdcall hookProc(int code, quintptr wParam, qintptr lParam);
    static long __stdcall keyboardHookProc(int code, quintptr wParam, qintptr lParam);
    bool handleMouseMessage(quintptr message, const QPoint &globalPosition);
    bool shouldBlockWindowsKey(unsigned int virtualKey) const;
    static SuperCapture *s_instance;
    void installHook();
    void uninstallHook();
    void finishCapture(const QPoint &globalPosition);
    void cancelCapture();
    void *m_hook = nullptr;
    void *m_keyboardHook = nullptr;
#elif defined(Q_OS_MACOS)
    void installMonitor();
    void uninstallMonitor();
    bool handleNativeMouseEvent(void *event);
    void finishCapture(const QPoint &globalPosition);
    void cancelCapture();
    void *m_eventTap = nullptr;
    void *m_eventTapSource = nullptr;
#endif
    bool m_enabled = false;
    Qt::KeyboardModifier m_modifier = Qt::NoModifier;
    Qt::MouseButton m_button = Qt::LeftButton;
    bool m_blockWinLeft = true;
    bool m_blockWinRight = false;
    bool m_dragging = false;
    QPoint m_start;
};

} // namespace snipaste::platform
