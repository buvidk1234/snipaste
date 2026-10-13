#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>

namespace snipate::platform {

class GlobalHotkey final : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT

public:
    explicit GlobalHotkey(QObject *parent = nullptr);
    ~GlobalHotkey() override;

    bool registerKey(int id, unsigned int modifiers, unsigned int key);
    void unregisterKey();
    void clearKey();
    void setEnabled(bool enabled);
    bool isEnabled() const;
    int registeredId() const { return m_id; }

signals:
    void triggered(int id);

protected:
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    int m_id = 0;
    unsigned int m_modifiers = 0;
    unsigned int m_key = 0;
    bool m_registered = false;
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    // Opaque Carbon handles keep the public header free of platform headers.
    void *m_nativeHotKey = nullptr;
    void *m_nativeEventHandler = nullptr;
#endif
};

} // namespace snipate::platform
