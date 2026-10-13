#pragma once

#include <QKeySequence>
#include <QString>
#include <QtGlobal>

namespace snipaste::platform {

inline constexpr Qt::KeyboardModifier primaryModifier()
{
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    return Qt::MetaModifier;
#else
    return Qt::ControlModifier;
#endif
}

inline bool hasPrimaryModifier(Qt::KeyboardModifiers modifiers)
{
    return modifiers.testFlag(primaryModifier());
}

inline QString primaryModifierText()
{
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    return QStringLiteral("Cmd");
#else
    return QStringLiteral("Ctrl");
#endif
}

inline QString primaryPortableModifierText()
{
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    return QStringLiteral("Meta");
#else
    return QStringLiteral("Ctrl");
#endif
}

inline QString primaryShortcutText(const QString &key, bool shift = false)
{
    return primaryModifierText() + (shift ? QStringLiteral("+Shift+") : QStringLiteral("+")) + key;
}

inline QString primaryPortableShortcutText(const QString &key, bool shift = false)
{
    return primaryPortableModifierText()
        + (shift ? QStringLiteral("+Shift+") : QStringLiteral("+")) + key;
}

inline QKeySequence primaryKeySequence(Qt::Key key, bool shift = false)
{
    const int modifiers = static_cast<int>(primaryModifier())
        | (shift ? static_cast<int>(Qt::ShiftModifier) : 0);
    return QKeySequence(modifiers | static_cast<int>(key));
}

} // namespace snipaste::platform
