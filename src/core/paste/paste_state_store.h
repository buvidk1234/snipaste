#pragma once

#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <optional>

namespace snipaste::core
{

// A disk snapshot contains values only, never widgets or callbacks. Window
// option keys retain compatibility with existing state.json files.
struct PasteWindowState
{
    QImage image;
    QJsonObject options;
    qint64 savedImageKey = 0;
    QString savedImagePath;
};

struct PasteGroupState
{
    QString name;
    QList<PasteWindowState> windows;
};

struct PasteSessionState
{
    QList<PasteGroupState> groups;
    int activeGroup = 0;
};

class PasteStateStore final
{
  public:
    static std::optional<PasteSessionState> load(const QString &directory);
    // Reuse metadata changes only after the JSON and new PNGs commit. Failure
    // leaves both the caller's snapshot and previous on-disk state usable.
    static bool save(PasteSessionState &state, const QString &directory);
};

} // namespace snipaste::core
