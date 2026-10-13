#include "platform/windows/global_hotkey.h"

#include <Carbon/Carbon.h>

#include <QCoreApplication>
#include <QDebug>
#include <QMetaObject>

namespace snipaste::platform {

namespace {

constexpr OSType kSnipasteHotKeySignature = 'SNPA';

OSStatus hotKeyHandler(EventHandlerCallRef, EventRef event, void *userData)
{
    auto *hotkey = static_cast<GlobalHotkey *>(userData);
    if (!hotkey)
        return eventNotHandledErr;

    EventHotKeyID identifier{};
    if (GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID,
                          nullptr, sizeof(identifier), nullptr, &identifier) != noErr)
        return eventNotHandledErr;
    if (identifier.signature != kSnipasteHotKeySignature)
        return eventNotHandledErr;
    if (identifier.id != static_cast<UInt32>(hotkey->registeredId()))
        return eventNotHandledErr;

    const int id = static_cast<int>(identifier.id);
    // Carbon invokes the handler on the application event thread. Queue the
    // Qt signal so registration and command execution remain event-loop safe.
    QMetaObject::invokeMethod(hotkey, [hotkey, id] { emit hotkey->triggered(id); },
                              Qt::QueuedConnection);
    return noErr;
}

} // namespace

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
    unregisterKey();
    if (id <= 0 || key == 0)
        return false;

    EventTypeSpec eventType{ kEventClassKeyboard, kEventHotKeyPressed };
    EventHandlerRef handler = nullptr;
    if (InstallApplicationEventHandler(&hotKeyHandler, 1, &eventType, this, &handler) != noErr) {
        qWarning() << "failed to install macOS global hotkey handler" << id;
        return false;
    }

    EventHotKeyID identifier{ kSnipasteHotKeySignature, static_cast<UInt32>(id) };
    EventHotKeyRef nativeHotKey = nullptr;
    const OSStatus status = RegisterEventHotKey(static_cast<UInt32>(key),
                                                 static_cast<UInt32>(modifiers),
                                                 identifier,
                                                 GetApplicationEventTarget(),
                                                 0,
                                                 &nativeHotKey);
    if (status != noErr) {
        RemoveEventHandler(handler);
        qWarning() << "failed to register macOS global hotkey" << id << status;
        return false;
    }

    m_id = id;
    m_modifiers = modifiers;
    m_key = key;
    m_nativeHotKey = reinterpret_cast<void *>(nativeHotKey);
    m_nativeEventHandler = reinterpret_cast<void *>(handler);
    m_registered = true;
    return true;
}

void GlobalHotkey::unregisterKey()
{
    if (m_nativeHotKey) {
        UnregisterEventHotKey(reinterpret_cast<EventHotKeyRef>(m_nativeHotKey));
        m_nativeHotKey = nullptr;
    }
    if (m_nativeEventHandler) {
        RemoveEventHandler(reinterpret_cast<EventHandlerRef>(m_nativeEventHandler));
        m_nativeEventHandler = nullptr;
    }
    m_registered = false;
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
    Q_UNUSED(eventType)
    Q_UNUSED(message)
    Q_UNUSED(result)
    return false;
}

} // namespace snipaste::platform
