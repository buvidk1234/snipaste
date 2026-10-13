#include "platform/windows/super_capture.h"

#ifdef Q_OS_WIN
#include <windows.h>

namespace {
int buttonDownMessage(Qt::MouseButton button)
{
    return button == Qt::RightButton ? WM_RBUTTONDOWN : WM_LBUTTONDOWN;
}

int buttonUpMessage(Qt::MouseButton button)
{
    return button == Qt::RightButton ? WM_RBUTTONUP : WM_LBUTTONUP;
}

bool modifierDown(Qt::KeyboardModifier modifier)
{
    const bool winDown = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0
        || (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;
    if (!winDown)
        return false;
    if (modifier == Qt::ControlModifier)
        return (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    if (modifier == Qt::ShiftModifier)
        return (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    if (modifier == Qt::AltModifier)
        return (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    return true;
}
} // namespace
#endif

namespace snipaste::platform {

#ifdef Q_OS_WIN
SuperCapture *SuperCapture::s_instance = nullptr;
#endif

SuperCapture::SuperCapture(QObject *parent)
    : QObject(parent)
{
}

SuperCapture::~SuperCapture()
{
    setEnabled(false);
}

void SuperCapture::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
#ifdef Q_OS_WIN
    if (enabled)
        installHook();
    else {
        cancelCapture();
        uninstallHook();
    }
#else
    Q_UNUSED(enabled)
#endif
}

void SuperCapture::setModifier(Qt::KeyboardModifier modifier)
{
    m_modifier = modifier;
}

void SuperCapture::setMouseButton(Qt::MouseButton button)
{
    if (button == Qt::LeftButton || button == Qt::RightButton)
        m_button = button;
}

void SuperCapture::setBlockWindowsKeys(bool left, bool right)
{
    m_blockWinLeft = left;
    m_blockWinRight = right;
}

#ifdef Q_OS_WIN
void SuperCapture::installHook()
{
    if (m_hook && m_keyboardHook)
        return;
    s_instance = this;
    const HMODULE module = GetModuleHandleW(nullptr);
    if (!m_hook) {
        m_hook = reinterpret_cast<void *>(SetWindowsHookExW(
            WH_MOUSE_LL, reinterpret_cast<HOOKPROC>(&SuperCapture::hookProc), module, 0));
    }
    if (!m_keyboardHook) {
        m_keyboardHook = reinterpret_cast<void *>(SetWindowsHookExW(
            WH_KEYBOARD_LL, reinterpret_cast<HOOKPROC>(&SuperCapture::keyboardHookProc), module, 0));
    }
    if (!m_hook && !m_keyboardHook)
        s_instance = nullptr;
}

void SuperCapture::uninstallHook()
{
    if (m_hook) {
        UnhookWindowsHookEx(reinterpret_cast<HHOOK>(m_hook));
        m_hook = nullptr;
    }
    if (m_keyboardHook) {
        UnhookWindowsHookEx(reinterpret_cast<HHOOK>(m_keyboardHook));
        m_keyboardHook = nullptr;
    }
    if (s_instance == this)
        s_instance = nullptr;
}

long __stdcall SuperCapture::hookProc(int code, quintptr wParam, qintptr lParam)
{
    if (code >= 0 && s_instance && s_instance->m_enabled) {
        const auto *data = reinterpret_cast<const MSLLHOOKSTRUCT *>(lParam);
        if (s_instance->handleMouseMessage(wParam, QPoint(data->pt.x, data->pt.y)))
            return 1;
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

long __stdcall SuperCapture::keyboardHookProc(int code, quintptr wParam, qintptr lParam)
{
    if (code >= 0 && s_instance && s_instance->m_enabled) {
        const auto *data = reinterpret_cast<const KBDLLHOOKSTRUCT *>(lParam);
        if (data && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN
                     || wParam == WM_KEYUP || wParam == WM_SYSKEYUP)
            && s_instance->shouldBlockWindowsKey(data->vkCode)) {
            return 1;
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

bool SuperCapture::shouldBlockWindowsKey(unsigned int virtualKey) const
{
    if (virtualKey == VK_LWIN)
        return m_blockWinLeft;
    if (virtualKey == VK_RWIN)
        return m_blockWinRight;
    return false;
}

bool SuperCapture::handleMouseMessage(quintptr message, const QPoint &globalPosition)
{
    if (!modifierDown(m_modifier)) {
        if (m_dragging)
            cancelCapture();
        return false;
    }
    if (message == static_cast<quintptr>(buttonDownMessage(m_button))) {
        m_dragging = true;
        m_start = globalPosition;
        return true;
    }
    if (message == static_cast<quintptr>(buttonUpMessage(m_button)) && m_dragging) {
        finishCapture(globalPosition);
        return true;
    }
    return m_dragging && message == WM_MOUSEMOVE;
}

void SuperCapture::finishCapture(const QPoint &globalPosition)
{
    const QRect rect = QRect(m_start, globalPosition).normalized();
    m_dragging = false;
    if (rect.width() >= 2 && rect.height() >= 2)
        emit rectangleCaptured(rect);
}

void SuperCapture::cancelCapture()
{
    m_dragging = false;
    m_start = {};
}
#endif

} // namespace snipaste::platform
