#pragma once

#include <QKeySequence>
#include <QString>
#include <QtGlobal>

namespace snipaste::platform {

inline constexpr Qt::KeyboardModifier primaryModifier()
{
    // Qt maps the Command key to ControlModifier on macOS by default.
    return Qt::ControlModifier;
}

inline constexpr Qt::KeyboardModifier physicalControlModifier()
{
#if defined(Q_OS_MACOS)
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
#if defined(Q_OS_MACOS)
    return QStringLiteral("Cmd");
#else
    return QStringLiteral("Ctrl");
#endif
}

inline QString primaryPortableModifierText()
{
    return QStringLiteral("Ctrl");
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
