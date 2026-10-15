#pragma once

#include <QString>
#include <QStringList>

namespace snipaste::platform
{

struct HotkeySpec
{
    unsigned int modifiers = 0;
    unsigned int key = 0;
    bool valid = false;
    bool simple = false;
};

HotkeySpec parseHotkey(const QString &text);
void pasteClipboardAsKeyboardInput();
void trimIdleProcessMemory();
bool foregroundApplicationMatches(const QStringList &ignored);
void setProcessPriority(int configured);
void setWindowPinned(quintptr handle, bool enabled);

} // namespace snipaste::platform
