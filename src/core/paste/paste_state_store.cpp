#include "core/paste/paste_state_store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

namespace snipaste::core
{

std::optional<PasteSessionState> PasteStateStore::load(const QString &directory)
{
    QFile file(QDir(directory).filePath(QStringLiteral("state.json")));
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    const auto document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject())
        return std::nullopt;
    const auto root = document.object();
    const auto groups = root.value(QStringLiteral("groups")).toArray();
    if (groups.isEmpty())
        return std::nullopt;
    PasteSessionState state;
    // Several windows may reference one saved PNG. Decode each file once
    // and share its source pixels; per-window edits still detach via QImage.
    QHash<QString, QImage> decodedImages;
    for (const auto &value : groups)
    {
        const auto object = value.toObject();
        PasteGroupState group;
        group.name = object.value(QStringLiteral("name")).toString();
        if (group.name.isEmpty())
            group.name = QStringLiteral("组 %1").arg(state.groups.size() + 1);
        const auto windows =
            value.isObject() ? object.value(QStringLiteral("windows")).toArray() : value.toArray();
        for (const auto &window : windows)
        {
            PasteWindowState pin;
            pin.options = window.toObject();
            pin.savedImagePath =
                QFileInfo(QDir(directory).filePath(pin.options.value(QStringLiteral("image")).toString()))
                    .absoluteFilePath();
            const auto cached = decodedImages.constFind(pin.savedImagePath);
            if (cached != decodedImages.cend())
                pin.image = cached.value();
            else
            {
                pin.image.load(pin.savedImagePath);
                decodedImages.insert(pin.savedImagePath, pin.image);
            }
            if (pin.image.isNull())
                continue;
            pin.savedImageKey = pin.image.cacheKey();
            group.windows.append(std::move(pin));
        }
        state.groups.append(std::move(group));
    }
    state.activeGroup = qBound(0, root.value(QStringLiteral("activeGroup")).toInt(), state.groups.size() - 1);
    return state;
}

bool PasteStateStore::save(PasteSessionState &state, const QString &directory)
{
    QDir dir(directory);
    if (!dir.mkpath(QStringLiteral(".")))
        return false;
    struct SavedImage
    {
        PasteWindowState *pin;
        qint64 key;
        QString path;
    };
    QList<SavedImage> saved;
    QHash<qint64, QString> imageNames;
    QSet<QString> expectedImages;
    QStringList createdImages;
    const auto discardNewImages = [&] {
        for (const QString &name : createdImages)
            dir.remove(name);
    };
    QJsonArray groups;
    for (auto &group : state.groups)
    {
        QJsonArray windows;
        for (auto &pin : group.windows)
        {
            if (pin.image.isNull())
                continue;
            const auto key = pin.image.cacheKey();
            QString name = imageNames.value(key);
            if (name.isEmpty())
            {
                const QFileInfo previous(pin.savedImagePath);
                if (pin.savedImageKey == key && previous.isFile() &&
                    previous.absolutePath() == dir.absolutePath())
                {
                    name = previous.fileName();
                }
                else
                {
                    name = QStringLiteral("paste-%1.png").arg(QUuid::createUuid().toString(QUuid::Id128));
                    QSaveFile imageFile(dir.filePath(name));
                    if (!imageFile.open(QIODevice::WriteOnly) || !pin.image.save(&imageFile, "PNG") ||
                        !imageFile.commit())
                    {
                        discardNewImages();
                        return false;
                    }
                    createdImages.append(name);
                }
                imageNames.insert(key, name);
            }
            expectedImages.insert(name);
            saved.append({&pin, key, QFileInfo(dir.filePath(name)).absoluteFilePath()});
            auto options = pin.options;
            options.insert(QStringLiteral("image"), name);
            windows.append(options);
        }
        QJsonObject object;
        object.insert(QStringLiteral("name"), group.name);
        object.insert(QStringLiteral("windows"), windows);
        groups.append(object);
    }
    QJsonObject root;
    root.insert(QStringLiteral("groups"), groups);
    root.insert(QStringLiteral("activeGroup"), state.activeGroup);
    QSaveFile file(dir.filePath(QStringLiteral("state.json")));
    const auto json = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size() || !file.commit())
    {
        discardNewImages();
        return false;
    }
    for (const auto &image : saved)
    {
        image.pin->savedImageKey = image.key;
        image.pin->savedImagePath = image.path;
    }
    for (const QString &name : dir.entryList({QStringLiteral("paste-*.png")}, QDir::Files))
        if (!expectedImages.contains(name))
            dir.remove(name);
    return true;
}

} // namespace snipaste::core
