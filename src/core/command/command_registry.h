#pragma once

#include <functional>
#include <QString>
#include <QHash>
#include <QStringList>

namespace snipate::core {

struct Command {
    QString id;
    QString label;
    std::function<void()> execute;
};

class CommandRegistry final
{
public:
    static QString canonicalId(const QString &command);
    bool registerCommand(Command command);
    bool contains(const QString &id) const;
    bool execute(const QString &id) const;
    QStringList ids() const;

private:
    QHash<QString, Command> m_commands;
};

} // namespace snipate::core
