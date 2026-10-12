#include "core/command/command_registry.h"

#include <QDebug>

namespace snipaste::core {

QString CommandRegistry::canonicalId(const QString &command)
{
    static const QHash<QString, QString> aliases = {
        {QStringLiteral("snip"), QStringLiteral("snip.start")},
        {QStringLiteral("paste"), QStringLiteral("paste.clipboard")},
        {QStringLiteral("show-images"), QStringLiteral("paste.showAll")},
        {QStringLiteral("hide-images"), QStringLiteral("paste.hideAll")},
        {QStringLiteral("toggle-images"), QStringLiteral("paste.toggleAll")},
        {QStringLiteral("create-group"), QStringLiteral("paste.createGroup")},
        {QStringLiteral("switch-group"), QStringLiteral("paste.switchGroup")},
        {QStringLiteral("switch-groups"), QStringLiteral("paste.groupManager")},
        {QStringLiteral("show-group-manager"), QStringLiteral("paste.groupManager")},
        {QStringLiteral("empty-group"), QStringLiteral("paste.emptyGroup")},
        {QStringLiteral("no-click-through"), QStringLiteral("paste.noClickThrough")},
        {QStringLiteral("toggle-mouse-through"), QStringLiteral("paste.toggleClickThrough")},
        {QStringLiteral("toggle-click-through"), QStringLiteral("paste.toggleClickThrough")},
        {QStringLiteral("open-preferences"), QStringLiteral("preferences.open")},
        {QStringLiteral("show-tray-menu"), QStringLiteral("tray.showMenu")},
        {QStringLiteral("show-purchase-dialog"), QStringLiteral("app.purchase")},
        {QStringLiteral("check-update"), QStringLiteral("update.checkNow")},
        {QStringLiteral("disable-hot-corners"), QStringLiteral("corners.disable")},
        {QStringLiteral("enable-hot-corners"), QStringLiteral("corners.enable")},
        {QStringLiteral("toggle-hot-corners"), QStringLiteral("corners.toggle")},
        {QStringLiteral("barcode-scan"), QStringLiteral("barcodeDecode")},
        {QStringLiteral("barcode-decode"), QStringLiteral("barcodeDecode")},
        {QStringLiteral("paste-as-image"), QStringLiteral("pasteAsImage")},
        {QStringLiteral("paste-as-plain-text"), QStringLiteral("pasteAsPlainText")},
        {QStringLiteral("switch-group-prev"), QStringLiteral("paste.switchGroupPrev")},
        {QStringLiteral("disable-hotkeys"), QStringLiteral("hotkeys.disable")},
        {QStringLiteral("enable-hotkeys"), QStringLiteral("hotkeys.enable")},
        {QStringLiteral("toggle-hotkeys"), QStringLiteral("hotkeys.toggle")},
        {QStringLiteral("clear-snip-history"), QStringLiteral("history.clear")},
        {QStringLiteral("docs"), QStringLiteral("help.open")},
        {QStringLiteral("exit"), QStringLiteral("app.quit")},
    };
    const QString id = command.trimmed();
    return aliases.value(id, id);
}

bool CommandRegistry::registerCommand(Command command)
{
    if (command.id.isEmpty() || !command.execute || m_commands.contains(command.id))
        return false;

    m_commands.insert(command.id, std::move(command));
    return true;
}

bool CommandRegistry::contains(const QString &id) const
{
    return m_commands.contains(canonicalId(id));
}

bool CommandRegistry::execute(const QString &id) const
{
    const auto it = m_commands.constFind(canonicalId(id));
    if (it == m_commands.constEnd()) {
        qWarning() << "unknown command:" << id;
        return false;
    }

    it->execute();
    return true;
}

QStringList CommandRegistry::ids() const
{
    return m_commands.keys();
}

} // namespace snipaste::core
