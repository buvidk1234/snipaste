#include "platform/desktop_integration.h"

#include <QDebug>
#include <QFileInfo>
#include <QKeySequence>
#include <QtGlobal>

#ifdef Q_OS_WIN
#include <malloc.h>
#include <windows.h>
#elif defined(Q_OS_MACOS)
#include "platform/macos/system.h"
#include <Carbon/Carbon.h>
#endif

namespace snipaste::platform
{
#ifdef Q_OS_WIN
namespace
{
struct __declspec(uuid("B5A399E7-1C87-46B8-88E9-FC5747B171BD")) VirtualDesktopPinnedAppsClass;
struct __declspec(uuid("4CE81583-1E4C-4632-A621-07A53543148F")) VirtualDesktopPinnedAppsInterface : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE IsAppIdPinned(void *, BOOL *) = 0;
    virtual HRESULT STDMETHODCALLTYPE PinAppID(void *) = 0;
    virtual HRESULT STDMETHODCALLTYPE UnpinAppID(void *) = 0;
    virtual HRESULT STDMETHODCALLTYPE IsWindowPinned(HWND, BOOL *) = 0;
    virtual HRESULT STDMETHODCALLTYPE PinWindow(HWND) = 0;
    virtual HRESULT STDMETHODCALLTYPE UnpinWindow(HWND) = 0;
};

void pinWindowToVirtualDesktops(HWND window, bool enabled)
{
    if (!window)
        return;
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    VirtualDesktopPinnedAppsInterface *apps = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(VirtualDesktopPinnedAppsClass), nullptr, CLSCTX_INPROC_SERVER,
                                   __uuidof(VirtualDesktopPinnedAppsInterface),
                                   reinterpret_cast<void **>(&apps))) &&
        apps)
    {
        if (enabled)
            apps->PinWindow(window);
        else
            apps->UnpinWindow(window);
        apps->Release();
    }
    if (SUCCEEDED(init))
        CoUninitialize();
}
} // namespace
#endif

HotkeySpec parseHotkey(const QString &text)
{
    HotkeySpec result;
    const QKeySequence sequence(text, QKeySequence::PortableText);
    if (sequence.isEmpty())
        return result;
    const QKeyCombination combination = sequence[0];
    const Qt::KeyboardModifiers qtModifiers = combination.keyboardModifiers();
    const int base = combination.key();
    const bool functionKey = base >= Qt::Key_F1 && base <= Qt::Key_F24;
    result.simple = !functionKey && (qtModifiers == Qt::NoModifier || qtModifiers == Qt::ShiftModifier);
#ifdef Q_OS_WIN
    if (qtModifiers.testFlag(Qt::ControlModifier))
        result.modifiers |= MOD_CONTROL;
    if (qtModifiers.testFlag(Qt::AltModifier))
        result.modifiers |= MOD_ALT;
    if (qtModifiers.testFlag(Qt::ShiftModifier))
        result.modifiers |= MOD_SHIFT;
    if (qtModifiers.testFlag(Qt::MetaModifier))
        result.modifiers |= MOD_WIN;
    if (base >= Qt::Key_A && base <= Qt::Key_Z)
        result.key = static_cast<unsigned int>('A' + base - Qt::Key_A);
    else if (base >= Qt::Key_0 && base <= Qt::Key_9)
        result.key = static_cast<unsigned int>('0' + base - Qt::Key_0);
    else if (base >= Qt::Key_F1 && base <= Qt::Key_F24)
        result.key = static_cast<unsigned int>(VK_F1 + base - Qt::Key_F1);
    else
    {
        switch (base)
        {
        case Qt::Key_Space:
            result.key = VK_SPACE;
            break;
        case Qt::Key_Tab:
            result.key = VK_TAB;
            break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            result.key = VK_RETURN;
            break;
        case Qt::Key_Escape:
            result.key = VK_ESCAPE;
            break;
        case Qt::Key_Insert:
            result.key = VK_INSERT;
            break;
        case Qt::Key_Delete:
            result.key = VK_DELETE;
            break;
        case Qt::Key_Home:
            result.key = VK_HOME;
            break;
        case Qt::Key_End:
            result.key = VK_END;
            break;
        case Qt::Key_PageUp:
            result.key = VK_PRIOR;
            break;
        case Qt::Key_PageDown:
            result.key = VK_NEXT;
            break;
        case Qt::Key_Left:
            result.key = VK_LEFT;
            break;
        case Qt::Key_Right:
            result.key = VK_RIGHT;
            break;
        case Qt::Key_Up:
            result.key = VK_UP;
            break;
        case Qt::Key_Down:
            result.key = VK_DOWN;
            break;
        case Qt::Key_Print:
            result.key = VK_SNAPSHOT;
            break;
        default:
            break;
        }
    }
#elif defined(Q_OS_MACOS)
    if (qtModifiers.testFlag(Qt::ControlModifier))
        result.modifiers |= controlKey;
    if (qtModifiers.testFlag(Qt::AltModifier))
        result.modifiers |= optionKey;
    if (qtModifiers.testFlag(Qt::ShiftModifier))
        result.modifiers |= shiftKey;
    if (qtModifiers.testFlag(Qt::MetaModifier))
        result.modifiers |= cmdKey;

    static constexpr unsigned short letters[] = {
        kVK_ANSI_A, kVK_ANSI_B, kVK_ANSI_C, kVK_ANSI_D, kVK_ANSI_E, kVK_ANSI_F, kVK_ANSI_G,
        kVK_ANSI_H, kVK_ANSI_I, kVK_ANSI_J, kVK_ANSI_K, kVK_ANSI_L, kVK_ANSI_M, kVK_ANSI_N,
        kVK_ANSI_O, kVK_ANSI_P, kVK_ANSI_Q, kVK_ANSI_R, kVK_ANSI_S, kVK_ANSI_T, kVK_ANSI_U,
        kVK_ANSI_V, kVK_ANSI_W, kVK_ANSI_X, kVK_ANSI_Y, kVK_ANSI_Z,
    };
    static constexpr unsigned short digits[] = {
        kVK_ANSI_0, kVK_ANSI_1, kVK_ANSI_2, kVK_ANSI_3, kVK_ANSI_4,
        kVK_ANSI_5, kVK_ANSI_6, kVK_ANSI_7, kVK_ANSI_8, kVK_ANSI_9,
    };
    if (base >= Qt::Key_A && base <= Qt::Key_Z)
        result.key = letters[base - Qt::Key_A];
    else if (base >= Qt::Key_0 && base <= Qt::Key_9)
        result.key = digits[base - Qt::Key_0];
    else if (base >= Qt::Key_F1 && base <= Qt::Key_F20)
    {
        // Carbon's function-key virtual codes are intentionally sparse (for
        // example F1=122, F2=120, F3=99), so they cannot be derived by adding
        // an offset to kVK_F1.
        static constexpr unsigned short functionKeys[] = {
            kVK_F1,  kVK_F2,  kVK_F3,  kVK_F4,  kVK_F5,  kVK_F6,  kVK_F7,  kVK_F8,  kVK_F9,  kVK_F10,
            kVK_F11, kVK_F12, kVK_F13, kVK_F14, kVK_F15, kVK_F16, kVK_F17, kVK_F18, kVK_F19, kVK_F20,
        };
        result.key = functionKeys[base - Qt::Key_F1];
    }
    else
    {
        switch (base)
        {
        case Qt::Key_Space:
            result.key = kVK_Space;
            break;
        case Qt::Key_Tab:
            result.key = kVK_Tab;
            break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            result.key = kVK_Return;
            break;
        case Qt::Key_Escape:
            result.key = kVK_Escape;
            break;
        case Qt::Key_Delete:
            result.key = kVK_ForwardDelete;
            break;
        case Qt::Key_Home:
            result.key = kVK_Home;
            break;
        case Qt::Key_End:
            result.key = kVK_End;
            break;
        case Qt::Key_PageUp:
            result.key = kVK_PageUp;
            break;
        case Qt::Key_PageDown:
            result.key = kVK_PageDown;
            break;
        case Qt::Key_Left:
            result.key = kVK_LeftArrow;
            break;
        case Qt::Key_Right:
            result.key = kVK_RightArrow;
            break;
        case Qt::Key_Up:
            result.key = kVK_UpArrow;
            break;
        case Qt::Key_Down:
            result.key = kVK_DownArrow;
            break;
        default:
            break;
        }
    }
#else
    Q_UNUSED(qtModifiers)
    Q_UNUSED(base)
#endif
    result.valid = result.key != 0;
    return result;
}

void pasteClipboardAsKeyboardInput()
{
#ifdef Q_OS_WIN
    INPUT inputs[4]{};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CONTROL;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 'V';
    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = 'V';
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = VK_CONTROL;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(4, inputs, sizeof(INPUT));
#elif defined(Q_OS_MACOS)
    if (!snipaste::platform::postMacPasteShortcut())
        qWarning() << "macOS rejected Cmd+V injection; grant Accessibility permission to snipaste";
#endif
}

void trimIdleProcessMemory()
{
#ifdef Q_OS_WIN
    // The screenshot/overlay objects have already been released.  Compact
    // free CRT heap blocks, but keep the process working set intact: passing
    // (-1, -1) to SetProcessWorkingSetSize is EmptyWorkingSet and evicts Qt,
    // font, window, and application code pages as well.  That makes the tray
    // process look tiny while making the next hotkey pay the page-fault cost
    // of a cold start again.
    _heapmin();
#endif
}

bool foregroundApplicationMatches(const QStringList &ignored)
{
    if (ignored.isEmpty())
        return false;
#ifdef Q_OS_WIN
    HWND window = GetForegroundWindow();
    if (!window)
        return false;
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process)
        return false;
    wchar_t buffer[MAX_PATH]{};
    DWORD size = MAX_PATH;
    const bool success = QueryFullProcessImageNameW(process, 0, buffer, &size) != FALSE;
    CloseHandle(process);
    if (!success)
        return false;
    const QString executable =
        QFileInfo(QString::fromWCharArray(buffer, static_cast<int>(size))).fileName().toLower();
    for (const QString &entry : ignored)
    {
        const QString normalized = entry.trimmed().toLower();
        if (!normalized.isEmpty() && executable.contains(normalized))
            return true;
    }
#elif defined(Q_OS_MACOS)
    return snipaste::platform::macForegroundApplicationMatches(ignored);
#else
    Q_UNUSED(ignored)
#endif
    return false;
}

void setProcessPriority(int configured)
{
#ifdef Q_OS_WIN
    DWORD priority = NORMAL_PRIORITY_CLASS;
    if (configured == 1)
        priority = ABOVE_NORMAL_PRIORITY_CLASS;
    else if (configured == 2)
        priority = HIGH_PRIORITY_CLASS;
    else if (configured == 3)
        priority = REALTIME_PRIORITY_CLASS;
    if (!SetPriorityClass(GetCurrentProcess(), priority))
        qWarning() << "failed to set process priority" << configured << GetLastError();
#else
    Q_UNUSED(configured)
#endif
}

void setWindowPinned(quintptr handle, bool enabled)
{
#ifdef Q_OS_WIN
    pinWindowToVirtualDesktops(reinterpret_cast<HWND>(handle), enabled);
#elif defined(Q_OS_MACOS)
    setMacWindowCollectionBehavior(handle, enabled);
#else
    Q_UNUSED(handle)
    Q_UNUSED(enabled)
#endif
}
} // namespace snipaste::platform
