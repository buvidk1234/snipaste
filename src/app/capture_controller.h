#pragma once
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QSize>
#include <memory>
namespace snipaste::core
{
class ConfigStore;
}
namespace snipaste::ui
{
class SnipOverlay;
class WindowCaptureOverlay;
class PasteManager;
} // namespace snipaste::ui
namespace snipaste::app
{
class ScreenshotHistoryService;
class ScreenshotOutput;
struct CaptureRequest
{
    bool autoCopy = false;
    bool reuseLast = false;
    QSize fixedSize;
    QRect selection;
    bool pin = false;
    QString output;
    bool hold = false;
    bool noAutoSave = false;
    bool keepAspect = false;
    bool superCapture = false;
};
// One owner for prepared controls, active sessions and recent selections.
class CaptureController final : public QObject
{
  public:
    CaptureController(core::ConfigStore &config, ScreenshotHistoryService &history, ScreenshotOutput &output,
                      ui::PasteManager &pastes, QObject *parent = nullptr);
    ~CaptureController() override;
    void schedule(CaptureRequest request, int delayMs);
    void start(const CaptureRequest &request = {});
    void startWindow(bool elementMode, QString outputSpec = {}, bool noAutoSave = false);
    void applySettings();
    bool active() const;
    bool pasteSelection();
    ui::SnipOverlay *overlay() const;
    ui::WindowCaptureOverlay *windowOverlay() const;

  private:
    void configureOverlay(bool superCaptureMode);
    core::ConfigStore &m_config;
    ScreenshotHistoryService &m_history;
    ScreenshotOutput &m_output;
    ui::PasteManager &m_pastes;
    QPointer<ui::SnipOverlay> m_activeOverlay;
    std::unique_ptr<ui::SnipOverlay> m_preparedOverlay;
    QPointer<ui::WindowCaptureOverlay> m_windowOverlay;
    QRect m_lastSelection;
    QList<QRect> m_recentSelections;
    int m_maxHistoryRegions = 8;
    int m_pendingCaptures = 0;
};
} // namespace snipaste::app
