#include "app/screenshot_history_service.h"
#include "app/external_command.h"
#include "core/config/config_store.h"
#include "platform/desktop_integration.h"
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace snipaste::app
{

using snipaste::platform::trimIdleProcessMemory;

ScreenshotHistoryService::ScreenshotHistoryService(const core::ConfigStore &config, QObject *parent)
    : QObject(parent),
      m_history(qBound(1, config.value(QStringLiteral("capture/maxHistory"), 20).toInt(), 999))
{
    QObject::connect(&m_writer, &snipaste::core::ScreenshotHistoryWriter::saved, this,
                     [this](quint64 revision, const QString &directory) {
                         if (m_history.revision() != revision)
                             return;
                         m_history.rebaseBackgroundPaths(directory);
                         m_history.releaseDecodedImages();
                         m_history.releaseDecodedBackgrounds();
                         trimIdleProcessMemory();
                     });
    QObject::connect(
        &m_writer, &snipaste::core::ScreenshotHistoryWriter::failed, this,
        [](const QString &directory) { qWarning() << "failed to save screenshot history" << directory; });
    m_directory =
        config.value(QStringLiteral("general/history_dir"), config.value(QStringLiteral("history_dir")))
            .toString()
            .trimmed();
    if (!m_directory.isEmpty())
    {
        m_directory = expandEnvironmentVariables(m_directory);
        if (QFileInfo(m_directory).isRelative())
            m_directory = QDir(QFileInfo(config.filePath()).absolutePath()).filePath(m_directory);
        m_directory = QDir::cleanPath(m_directory);
    }
    else
    {
        m_directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
                      QStringLiteral("/history");
    }
}
core::ScreenshotHistory &ScreenshotHistoryService::history()
{
    if (!m_loaded)
    {
        m_history.loadFromDirectory(m_directory);
        m_loaded = true;
    }
    return m_history;
}
void ScreenshotHistoryService::record(const QImage &image, const QImage &background,
                                      core::CaptureGeometry geometry)
{
    history().add(image, background, geometry);
    persist();
}
void ScreenshotHistoryService::persist()
{
    m_writer.save(m_history, m_directory);
}
void ScreenshotHistoryService::clear()
{
    // Clearing before first use must also suppress loading the old disk state.
    m_loaded = true;
    m_history.clear();
    persist();
}
void ScreenshotHistoryService::setMaximumCount(int count)
{
    m_history.setMaximumCount(count);
}

} // namespace snipaste::app
