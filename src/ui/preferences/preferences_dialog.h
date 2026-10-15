#pragma once

#include <QDialog>
#include <QVector>
#include <functional>

class QCheckBox;

namespace snipaste::core
{
class ConfigStore;
}

namespace snipaste::ui
{

class PreferencesDialog final : public QDialog
{
  public:
    explicit PreferencesDialog(core::ConfigStore &config, QWidget *parent = nullptr);
    void setUpdateCheckHandler(std::function<void(QWidget *)> handler) { m_updateCheckHandler = handler; }

  protected:
    void accept() override;
    void reject() override;

  private:
    QWidget *createGeneralPage(QWidget *parent);
    QWidget *createInterfacePage(QWidget *parent);
    QWidget *createCapturePage(QWidget *parent);
    QWidget *createPastePage(QWidget *parent);
    QWidget *createOutputPage(QWidget *parent);
    QWidget *createControlPage(QWidget *parent);
    QWidget *createUpdatePage(QWidget *parent);
    QWidget *createAboutPage(QWidget *parent);
    int px(int physicalPixels) const;
    QVector<std::function<void()>> m_pageSavers;
    std::function<void(QWidget *)> m_updateCheckHandler;
    core::ConfigStore &m_config;
    QCheckBox *m_startupCheck = nullptr;
};

} // namespace snipaste::ui
