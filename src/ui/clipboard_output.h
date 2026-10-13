#pragma once

#include <QClipboard>
#include <QBuffer>
#include <QByteArray>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QIODevice>
#include <QMimeData>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>

namespace snipaste::ui {

// Values are persisted by the preferences dialog.  Keep these values stable:
// 0 = copy as a file for every image-copy action, 1 = only the dedicated
// shortcut copies as a file, and 2 = always copy image data.
enum class CopyAsFileMode : int {
    Always = 0,
    Shortcut = 1,
    Disabled = 2,
};

// The output page controls the Windows Bitmap compatibility format
// independently for screenshots and pasted images.  Keep the policy in this
// small process-wide object so controls created by the overlay and paste
// windows use the same settings without depending on a particular ConfigStore
// instance (the application also supports --config with an arbitrary path).
enum class ClipboardImageSource : int {
    Screenshot,
    Paste,
};

struct ClipboardOutputOptions {
    bool windowsBitmap = true;
    bool screenshot = true;
    bool paste = false;
};

inline ClipboardOutputOptions &clipboardOutputOptions()
{
    static ClipboardOutputOptions options;
    return options;
}

inline void setClipboardOutputOptions(bool windowsBitmap, bool screenshot, bool paste)
{
    auto &options = clipboardOutputOptions();
    options.windowsBitmap = windowsBitmap;
    options.screenshot = screenshot;
    options.paste = paste;
}

inline bool shouldCopyWindowsBitmap(ClipboardImageSource source)
{
    const auto &options = clipboardOutputOptions();
    return options.windowsBitmap
        && (source == ClipboardImageSource::Screenshot ? options.screenshot : options.paste);
}

inline bool addWindowsBitmapFormat(QMimeData *mime, const QImage &image)
{
    if (!mime || image.isNull())
        return false;
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "BMP"))
        return false;
    mime->setData(QStringLiteral("image/bmp"), bytes);
    return true;
}

inline CopyAsFileMode copyAsFileModeFromInt(int value)
{
    switch (value) {
    case static_cast<int>(CopyAsFileMode::Always):
        return CopyAsFileMode::Always;
    case static_cast<int>(CopyAsFileMode::Shortcut):
        return CopyAsFileMode::Shortcut;
    default:
        return CopyAsFileMode::Disabled;
    }
}

inline bool copyImageToClipboard(const QImage &image,
                                 bool asFile,
                                 ClipboardImageSource source = ClipboardImageSource::Screenshot)
{
    if (image.isNull() || !QGuiApplication::clipboard())
        return false;

    const bool includeWindowsBitmap = shouldCopyWindowsBitmap(source);
    if (!asFile && !includeWindowsBitmap) {
        QGuiApplication::clipboard()->setImage(image);
        return true;
    }

    QString path;
    if (asFile) {
        const QString directory = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        if (directory.isEmpty() || !QDir().mkpath(directory))
            return false;
        path = QDir(directory).filePath(
            QStringLiteral("snipaste-%1.png").arg(QUuid::createUuid().toString(QUuid::Id128)));
        if (!image.save(path, "PNG"))
            return false;
    }

    auto *mime = new QMimeData;
    mime->setImageData(image);
    if (includeWindowsBitmap)
        addWindowsBitmapFormat(mime, image);
    if (asFile)
        mime->setUrls({QUrl::fromLocalFile(path)});
    QGuiApplication::clipboard()->setMimeData(mime);
    return true;
}

inline bool copyImageToClipboard(const QImage &image,
                                 CopyAsFileMode mode,
                                 bool shortcut = false,
                                 ClipboardImageSource source = ClipboardImageSource::Screenshot)
{
    const bool asFile = mode == CopyAsFileMode::Always
        || (mode == CopyAsFileMode::Shortcut && shortcut);
    return copyImageToClipboard(image, asFile, source);
}

inline bool copyImageToClipboard(const QImage &image,
                                 int mode,
                                 bool shortcut = false,
                                 ClipboardImageSource source = ClipboardImageSource::Screenshot)
{
    return copyImageToClipboard(image, copyAsFileModeFromInt(mode), shortcut, source);
}

} // namespace snipaste::ui
