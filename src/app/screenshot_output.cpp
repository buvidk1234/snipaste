#include "app/screenshot_output.h"
#include "app/external_command.h"
#include "core/command/argument_parser.h"
#include "core/config/config_store.h"
#include "core/recognition/ocr_service.h"
#include "ui/clipboard_output.h"
#include "ui/output/image_output.h"
#include "ui/paste/paste_manager.h"
#include "ui/paste/paste_window.h"
#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QStandardPaths>
#include <QThreadPool>
#include <QTimer>
#include <QToolTip>
#include <QUuid>

namespace snipaste::app
{

using snipaste::ui::addDropShadow;
using snipaste::ui::addSoftwareMetadata;
using snipaste::ui::outputFilePath;
using snipaste::ui::printImage;
using snipaste::ui::removeRedundantAlpha;

using snipaste::core::expandOutputFilePattern;
using snipaste::core::splitOutputSpec;

ScreenshotOutput::ScreenshotOutput(core::ConfigStore &config, ui::PasteManager &pastes, QObject *parent)
    : QObject(parent), m_config(config), m_pastes(pastes)
{
}
int ScreenshotOutput::imageQuality() const
{
    return qBound(-1, m_config.value(QStringLiteral("output/imageQuality"), -1).toInt(), 100);
}
bool ScreenshotOutput::saveImage(const QImage &image, const QString &path, int quality)
{
    if (image.isNull() || path.isEmpty())
        return false;
    return image.save(path, nullptr, quality);
}
void ScreenshotOutput::autoSave(const QImage &image)
{
    if (!m_config.value(QStringLiteral("capture/autoSave"), false).toBool() || image.isNull())
        return;
    QString directory = m_config
                            .value(QStringLiteral("output/autoSaveDirectory"),
                                   m_config.value(QStringLiteral("output/saveDirectory")))
                            .toString();
    if (directory.isEmpty())
        directory = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString pattern = m_config
                                .value(QStringLiteral("output/fileNamePattern"),
                                       QStringLiteral("Snipaste_$yyyy-MM-dd_HH-mm-ss$.png"))
                                .toString();
    QString fileName = expandOutputFilePattern(pattern, QDateTime::currentDateTime());
    if (QFileInfo(fileName).suffix().isEmpty())
        fileName += QStringLiteral(".png");
    const int quality = imageQuality();
    const bool removeAlpha = m_config
                                 .value(QStringLiteral("output/auto_alpha_channel_removal"),
                                        m_config.value(QStringLiteral("auto_alpha_channel_removal"), true))
                                 .toBool();
    const bool softwareMetadata = m_config
                                      .value(QStringLiteral("misc/software_metadata"),
                                             m_config.value(QStringLiteral("software_metadata"), true))
                                      .toBool();
    const QImage source = image;
    QThreadPool::globalInstance()->start(
        [source, directory, fileName, removeAlpha, softwareMetadata, quality] {
            if (!QDir().mkpath(directory))
                return;
            QImage output = addSoftwareMetadata(removeRedundantAlpha(source, removeAlpha), softwareMetadata);
            if (!saveImage(output, QDir(directory).filePath(fileName), quality))
                qWarning() << "failed to auto-save screenshot" << directory;
        });
}
void ScreenshotOutput::showPaste(const QImage &image, const QImage &source, const core::AnnotationDocument &annotations)
{
    // Let the capture overlay process its close/repaint events first.  A
    // top-level paste window performs native window setup and image
    // scaling; doing that directly inside the toolbar click makes the
    // click visibly pause and leaves the selection overlay on screen.
    const QImage copy = image;
    QTimer::singleShot(0, this, [this, copy, source, annotations] {
        if (!copy.isNull())
        {
            auto *window = m_pastes.createPaste(copy);
            if (window && !source.isNull() && !annotations.shapes().isEmpty())
                window->setAnnotations(annotations, source);
        }
    });
}
void ScreenshotOutput::execute(const QImage &image, const QString &spec)
{
    if (image.isNull())
        return;
    const QStringList outputs = splitOutputSpec(spec);
    const bool shadow = outputs.contains(QStringLiteral("shadow"));
    const bool noShadow = outputs.contains(QStringLiteral("no-shadow"));
    QImage normalizedImage = removeRedundantAlpha(
        image, m_config
                   .value(QStringLiteral("output/auto_alpha_channel_removal"),
                          m_config.value(QStringLiteral("auto_alpha_channel_removal"), true))
                   .toBool());
    QImage outputImage = shadow && !noShadow ? addDropShadow(normalizedImage) : normalizedImage;
    outputImage =
        addSoftwareMetadata(outputImage, m_config
                                             .value(QStringLiteral("misc/software_metadata"),
                                                    m_config.value(QStringLiteral("software_metadata"), true))
                                             .toBool());
    const auto runOutput = [&](const QString &output) {
        if (output == QStringLiteral("clipboard"))
        {
            snipaste::ui::copyImageToClipboard(outputImage, false,
                                               snipaste::ui::ClipboardImageSource::Screenshot);
        }
        else if (output == QStringLiteral("pin"))
        {
            m_pastes.createPaste(outputImage);
        }
        else if (output == QStringLiteral("ocr_clipboard"))
        {
            const auto result = snipaste::core::OcrService::recognize(
                outputImage,
                m_config.value(QStringLiteral("output/ocrEngine"), QStringLiteral("系统自带")).toString(),
                m_config.value(QStringLiteral("output/ocrLanguage"), QStringLiteral("系统默认")).toString());
            QApplication::clipboard()->setText(result.success ? result.text : QString());
            if (result.success && m_config.value(QStringLiteral("output/ocrSound"), true).toBool())
            {
                playConfiguredSound(
                    m_config.value(QStringLiteral("capture/soundFile"), QStringLiteral("sound/snip.wav"))
                        .toString());
            }
        }
        else if (output == QStringLiteral("quick-save"))
        {
            QString directory = m_config
                                    .value(QStringLiteral("output/saveDirectory"),
                                           QStandardPaths::writableLocation(QStandardPaths::PicturesLocation))
                                    .toString();
            if (directory.isEmpty())
                directory = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
            QDir().mkpath(directory);
            const QString pattern = m_config
                                        .value(QStringLiteral("output/fileNamePattern"),
                                               QStringLiteral("Snipaste_$yyyy-MM-dd_HH-mm-ss$.png"))
                                        .toString();
            QString fileName = expandOutputFilePattern(pattern, QDateTime::currentDateTime());
            if (QFileInfo(fileName).suffix().isEmpty())
                fileName += QStringLiteral(".png");
            const QString savedPath = QDir(directory).filePath(fileName);
            if (saveImage(outputImage, savedPath, imageQuality()) &&
                m_config.value(QStringLiteral("output/quickSaveNotification"), true).toBool())
                QToolTip::showText(QCursor::pos(), QStringLiteral("已保存截图：%1").arg(savedPath), nullptr,
                                   QRect(), 3000);
        }
        else if (output == QStringLiteral("file-dialog"))
        {
            const bool hideForDialog =
                m_config.value(QStringLiteral("paste/hideOnFileDialog"), false).toBool();
            if (hideForDialog)
                m_pastes.setHiddenForFileDialog(true);
            QString path = QFileDialog::getSaveFileName(
                nullptr, QStringLiteral("保存截图"),
                expandOutputFilePattern(m_config
                                            .value(QStringLiteral("output/fileNamePattern"),
                                                   QStringLiteral("Snipaste_$yyyy-MM-dd_HH-mm-ss$.png"))
                                            .toString(),
                                        QDateTime::currentDateTime()),
                QStringLiteral("PNG 图片 (*.png);;JPEG 图片 (*.jpg *.jpeg);;所有文件 (*.*)"));
            if (!path.isEmpty())
            {
                if (QFileInfo(path).suffix().isEmpty())
                    path += QStringLiteral(".png");
                saveImage(outputImage, path, imageQuality());
            }
            if (hideForDialog)
                m_pastes.setHiddenForFileDialog(false);
        }
        else if (output == QStringLiteral("printer"))
        {
            if (!printImage(outputImage))
                qInfo() << "printer output cancelled or unavailable";
        }
        else if (output.startsWith(QStringLiteral("exec(")) && output.endsWith(QChar(')')))
        {
            QString command = output.mid(5, output.size() - 6);
            const QString tempPath =
                QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                    .filePath(
                        QStringLiteral("snipaste-%1.png").arg(QUuid::createUuid().toString(QUuid::Id128)));
            if (outputImage.save(tempPath, "PNG"))
            {
                command.replace(QStringLiteral("%file%"), QDir::toNativeSeparators(tempPath));
                command.replace(QStringLiteral("%FILE%"), QDir::toNativeSeparators(tempPath));
#ifdef Q_OS_WIN
                QProcess::startDetached(QStringLiteral("cmd.exe"), {QStringLiteral("/c"), command});
#else
                QProcess::startDetached(QStringLiteral("sh"), {QStringLiteral("-c"), command});
#endif
            }
        }
        else
        {
            const QString path = outputFilePath(output, m_config);
            if (!path.isEmpty())
            {
                QDir().mkpath(QFileInfo(path).absolutePath());
                QString outputPath = path;
                if (QFileInfo(outputPath).suffix().isEmpty())
                    outputPath += QStringLiteral(".png");
                saveImage(outputImage, outputPath, imageQuality());
            }
        }
    };
    const auto isExec = [](const QString &value) {
        return value.startsWith(QStringLiteral("exec(")) && value.endsWith(QChar(')'));
    };
    const auto isFile = [&](const QString &value) {
        return !value.isEmpty() && value != QStringLiteral("clipboard") && value != QStringLiteral("pin") &&
               value != QStringLiteral("ocr_clipboard") && value != QStringLiteral("quick-save") &&
               value != QStringLiteral("file-dialog") && value != QStringLiteral("printer") &&
               value != QStringLiteral("success") && value != QStringLiteral("silent") &&
               value != QStringLiteral("no-auto-save") && value != QStringLiteral("shadow") &&
               value != QStringLiteral("no-shadow") && !isExec(value);
    };
    for (const QString &raw : outputs)
    {
        const QString output = raw.trimmed();
        if (isFile(output))
            runOutput(output);
    }
    const QStringList orderedTokens = {QStringLiteral("file-dialog"), QStringLiteral("printer"),
                                       QStringLiteral("quick-save"),  QStringLiteral("clipboard"),
                                       QStringLiteral("pin"),         QStringLiteral("ocr_clipboard")};
    for (const QString &token : orderedTokens)
        for (const QString &raw : outputs)
            if (raw.trimmed() == token)
                runOutput(token);
    for (const QString &raw : outputs)
        if (isExec(raw.trimmed()))
            runOutput(raw.trimmed());
}
} // namespace snipaste::app
