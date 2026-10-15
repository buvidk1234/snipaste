#pragma once
#include "core/history/screenshot_history.h"
#include "core/history/screenshot_history_writer.h"
#include <QObject>
namespace snipaste::core
{
class ConfigStore;
}
namespace snipaste::app
{
// Owns lazy loading and the coalescing disk writer. UI consumers never
// manage decoded-image release or writer lifetime themselves.
class ScreenshotHistoryService final : public QObject
{
  public:
    explicit ScreenshotHistoryService(const core::ConfigStore &config, QObject *parent = nullptr);
    core::ScreenshotHistory &history();
    void record(const QImage &image, const QImage &background = {}, core::CaptureGeometry geometry = {});
    void persist();
    void clear();
    void setMaximumCount(int count);

  private:
    core::ScreenshotHistory m_history;
    core::ScreenshotHistoryWriter m_writer;
    QString m_directory;
    bool m_loaded = false;
};
} // namespace snipaste::app
