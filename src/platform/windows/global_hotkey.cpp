#include "platform/windows/global_hotkey.h"

#include <QCoreApplication>
#include <QDebug>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace snipate::platform {

GlobalHotkey::GlobalHotkey(QObject *parent)
    : QObject(parent)
{
}

GlobalHotkey::~GlobalHotkey()
{
    unregisterKey();
}

bool GlobalHotkey::registerKey(int id, unsigned int modifiers, unsigned int key)
{
#ifdef Q_OS_WIN
    unregisterKey();
    if (!RegisterHotKey(nullptr, id, modifiers | MOD_NOREPEAT, key)) {
        qWarning() << "failed to register global hotkey" << id << GetLastError();
        return false;
    }

    m_id = id;
    m_modifiers = modifiers;
    m_key = key;
    m_registered = true;
    QCoreApplication::instance()->installNativeEventFilter(this);
    return true;
#else
    Q_UNUSED(id)
    Q_UNUSED(modifiers)
    Q_UNUSED(key)
    return false;
#endif
}

void GlobalHotkey::unregisterKey()
{
#ifdef Q_OS_WIN
    if (m_registered) {
        QCoreApplication::instance()->removeNativeEventFilter(this);
        UnregisterHotKey(nullptr, m_id);
        m_registered = false;
    }
#endif
}

void GlobalHotkey::clearKey()
{
    unregisterKey();
    m_id = 0;
    m_modifiers = 0;
    m_key = 0;
}

void GlobalHotkey::setEnabled(bool enabled)
{
    if (enabled == m_registered)
        return;
    if (!enabled) {
        unregisterKey();
        return;
    }
    if (m_id != 0 && m_key != 0)
        registerKey(m_id, m_modifiers, m_key);
}

bool GlobalHotkey::isEnabled() const
{
    return m_registered;
}

bool GlobalHotkey::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
#ifdef Q_OS_WIN
    if (eventType == "windows_generic_MSG") {
        const auto *msg = static_cast<const MSG *>(message);
        if (msg->message == WM_HOTKEY && static_cast<int>(msg->wParam) == m_id) {
            if (result)
                *result = 0;
            emit triggered(m_id);
            return true;
        }
    }
#else
    Q_UNUSED(eventType)
    Q_UNUSED(message)
    Q_UNUSED(result)
#endif
    return false;
}

} // namespace snipate::platform
