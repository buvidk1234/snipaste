#pragma once
#include <QImage>
#include <QObject>
namespace snipaste::core
{
class ConfigStore;
}
namespace snipaste::ui
{
class PasteManager;
}
namespace snipaste::app
{
class ScreenshotOutput final : public QObject
{
  public:
    ScreenshotOutput(core::ConfigStore &config, ui::PasteManager &pastes, QObject *parent = nullptr);
    void autoSave(const QImage &image);
    void execute(const QImage &image, const QString &spec);
    void showPaste(const QImage &image);

  private:
    int imageQuality() const;
    static bool saveImage(const QImage &image, const QString &path, int quality);
    core::ConfigStore &m_config;
    ui::PasteManager &m_pastes;
};
} // namespace snipaste::app
