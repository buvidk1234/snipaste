#include "ui/preferences/preferences_dialog.h"

#include "core/config/config_store.h"
#include "platform/keyboard.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "core/command/argument_parser.h"
#include "ui/preferences/preference_widgets.h"

namespace snipaste::ui
{
using namespace preferences;
QWidget *PreferencesDialog::createUpdatePage(QWidget *parent)
{
    auto *updatePage = new QWidget(parent);
    updatePage->setProperty("referenceHeight", 326);
    auto *updateLayout = new QVBoxLayout(updatePage);
    updateLayout->setContentsMargins(10, 8, 10, 8);
    updateLayout->setSpacing(5);

    auto *includeBetaRow = new QHBoxLayout;
    auto *includeBeta = new QCheckBox(QStringLiteral("包括 beta 版"), updatePage);
    includeBeta->setObjectName(QStringLiteral("updateIncludeBeta"));
    includeBeta->setChecked(m_config.value(QStringLiteral("update/includeBeta"), false).toBool());
    includeBetaRow->addWidget(includeBeta);
    auto *betaInfo = makeInfoButton(updatePage);
    betaInfo->setObjectName(QStringLiteral("updateBetaInfo"));
    betaInfo->setToolTip(QStringLiteral("包括尚未正式发布的测试版本。"));
    QObject::connect(betaInfo, &QPushButton::clicked, this, [this] {
        QMessageBox::information(this, QStringLiteral("测试版本"),
                                 QStringLiteral("启用后，检查更新时也会列出 beta 测试版本。"));
    });
    includeBetaRow->addWidget(betaInfo);
    includeBetaRow->addStretch();
    updateLayout->addLayout(includeBetaRow);

    auto *checkAtStartup = new QCheckBox(QStringLiteral("启动时检查更新"), updatePage);
    checkAtStartup->setObjectName(QStringLiteral("updateCheckAtStartup"));
    checkAtStartup->setChecked(m_config
                                   .value(QStringLiteral("update/checkAtStartup"),
                                          m_config.value(QStringLiteral("general/autoUpdate"), true))
                                   .toBool());
    updateLayout->addWidget(checkAtStartup);

    auto *periodicGroup = new QGroupBox(QStringLiteral("定期检查"), updatePage);
    periodicGroup->setObjectName(QStringLiteral("updatePeriodicGroup"));
    auto *periodicLayout = new QHBoxLayout(periodicGroup);
    periodicLayout->setContentsMargins(8, 5, 8, 5);
    auto *periodicCheck = new QCheckBox(QStringLiteral("每隔"), periodicGroup);
    periodicCheck->setObjectName(QStringLiteral("updatePeriodicCheck"));
    periodicCheck->setChecked(m_config.value(QStringLiteral("update/checkIntervalEnabled"), true).toBool());
    periodicLayout->addWidget(periodicCheck);
    auto *periodicHours = new QSpinBox(periodicGroup);
    periodicHours->setObjectName(QStringLiteral("updateCheckIntervalHours"));
    periodicHours->setRange(1, 720);
    periodicHours->setFixedWidth(70);
    periodicHours->setValue(m_config.value(QStringLiteral("update/checkIntervalHours"), 24).toInt());
    periodicLayout->addWidget(periodicHours);
    periodicLayout->addWidget(new QLabel(QStringLiteral("小时检查"), periodicGroup));
    periodicLayout->addStretch();
    updateLayout->addWidget(periodicGroup);

    auto *proxyGroup = new QGroupBox(QStringLiteral("代理"), updatePage);
    proxyGroup->setObjectName(QStringLiteral("updateProxyGroup"));
    auto *proxyForm = new QFormLayout(proxyGroup);
    proxyForm->setContentsMargins(8, 5, 8, 5);
    proxyForm->setHorizontalSpacing(8);
    proxyForm->setVerticalSpacing(4);
    auto *proxyType = new QComboBox(proxyGroup);
    proxyType->setObjectName(QStringLiteral("updateProxyType"));
    proxyType->addItems({QStringLiteral("系统代理"), QStringLiteral("不使用代理"),
                         QStringLiteral("HTTP 代理"), QStringLiteral("SOCKS5 代理")});
    proxyType->setCurrentText(
        m_config.value(QStringLiteral("update/proxyType"), QStringLiteral("系统代理")).toString());
    proxyForm->addRow(QStringLiteral("代理类型:"), proxyType);

    auto *proxyServerRow = new QWidget(proxyGroup);
    auto *proxyServerLayout = new QHBoxLayout(proxyServerRow);
    proxyServerLayout->setContentsMargins(0, 0, 0, 0);
    auto *proxyServer = new QLineEdit(proxyServerRow);
    proxyServer->setObjectName(QStringLiteral("updateProxyServer"));
    proxyServer->setPlaceholderText(QStringLiteral("服务器地址"));
    proxyServer->setText(m_config.value(QStringLiteral("update/proxyServer")).toString());
    proxyServerLayout->addWidget(proxyServer, 1);
    auto *proxyPort = new QSpinBox(proxyServerRow);
    proxyPort->setObjectName(QStringLiteral("updateProxyPort"));
    proxyPort->setRange(0, 65535);
    proxyPort->setSpecialValueText(QStringLiteral("0"));
    proxyPort->setFixedWidth(68);
    proxyPort->setValue(m_config.value(QStringLiteral("update/proxyPort"), 0).toInt());
    proxyServerLayout->addWidget(proxyPort);
    proxyForm->addRow(QStringLiteral("服务器:"), proxyServerRow);

    auto *proxyAuth = new QCheckBox(QStringLiteral("代理服务器需要密码"), proxyGroup);
    proxyAuth->setObjectName(QStringLiteral("updateProxyRequiresAuth"));
    proxyAuth->setChecked(m_config.value(QStringLiteral("update/proxyRequiresAuth"), false).toBool());
    proxyForm->addRow(proxyAuth);
    auto *proxyUsername = new QLineEdit(proxyGroup);
    proxyUsername->setObjectName(QStringLiteral("updateProxyUsername"));
    proxyUsername->setText(m_config.value(QStringLiteral("update/proxyUsername")).toString());
    proxyForm->addRow(QStringLiteral("用户名:"), proxyUsername);
    auto *proxyPassword = new QLineEdit(proxyGroup);
    proxyPassword->setObjectName(QStringLiteral("updateProxyPassword"));
    proxyPassword->setEchoMode(QLineEdit::Password);
    proxyPassword->setText(m_config.value(QStringLiteral("update/proxyPassword")).toString());
    proxyForm->addRow(QStringLiteral("密码:"), proxyPassword);

    const auto refreshProxyState = [proxyType, proxyServer, proxyPort, proxyAuth, proxyUsername,
                                    proxyPassword] {
        const bool customProxy = proxyType->currentIndex() >= 2;
        proxyServer->setEnabled(customProxy);
        proxyPort->setEnabled(customProxy);
        proxyAuth->setEnabled(customProxy);
        const bool auth = customProxy && proxyAuth->isChecked();
        proxyUsername->setEnabled(auth);
        proxyPassword->setEnabled(auth);
    };
    QObject::connect(proxyType, &QComboBox::currentTextChanged, this,
                     [refreshProxyState](const QString &) { refreshProxyState(); });
    QObject::connect(proxyAuth, &QCheckBox::toggled, this,
                     [refreshProxyState](bool) { refreshProxyState(); });
    refreshProxyState();
    updateLayout->addWidget(proxyGroup);

    const auto saveUpdateSettings = [this, includeBeta, checkAtStartup, periodicCheck, periodicHours,
                                     proxyType, proxyServer, proxyPort, proxyAuth, proxyUsername,
                                     proxyPassword] {
        m_config.setValue(QStringLiteral("update/includeBeta"), includeBeta->isChecked());
        m_config.setValue(QStringLiteral("update/checkAtStartup"), checkAtStartup->isChecked());
        m_config.setValue(QStringLiteral("update/checkIntervalEnabled"), periodicCheck->isChecked());
        m_config.setValue(QStringLiteral("update/checkIntervalHours"), periodicHours->value());
        m_config.setValue(QStringLiteral("update/proxyType"), proxyType->currentText());
        m_config.setValue(QStringLiteral("update/proxyServer"), proxyServer->text());
        m_config.setValue(QStringLiteral("update/proxyPort"), proxyPort->value());
        m_config.setValue(QStringLiteral("update/proxyRequiresAuth"), proxyAuth->isChecked());
        m_config.setValue(QStringLiteral("update/proxyUsername"), proxyUsername->text());
        m_config.setValue(QStringLiteral("update/proxyPassword"), proxyPassword->text());
        m_config.setValue(QStringLiteral("general/autoUpdate"), checkAtStartup->isChecked());
    };

    auto *updateButtons = new QHBoxLayout;
    auto *checkNow = new QPushButton(QStringLiteral("检查更新"), updatePage);
    checkNow->setObjectName(QStringLiteral("updateCheckNow"));
    auto *updateRestore = new QPushButton(QStringLiteral("恢复默认"), updatePage);
    updateRestore->setObjectName(QStringLiteral("updateRestore"));
    updateButtons->addWidget(checkNow);
    updateButtons->addStretch();
    updateButtons->addWidget(updateRestore);
    updateLayout->addLayout(updateButtons);
    QObject::connect(checkNow, &QPushButton::clicked, this, [this, saveUpdateSettings] {
        saveUpdateSettings();
        m_config.save();
        if (m_updateCheckHandler)
            m_updateCheckHandler(this);
    });
    QObject::connect(updateRestore, &QPushButton::clicked, this,
                     [includeBeta, checkAtStartup, periodicCheck, periodicHours, proxyType, proxyServer,
                      proxyPort, proxyAuth, proxyUsername, proxyPassword, refreshProxyState] {
                         includeBeta->setChecked(false);
                         checkAtStartup->setChecked(true);
                         periodicCheck->setChecked(true);
                         periodicHours->setValue(24);
                         proxyType->setCurrentText(QStringLiteral("系统代理"));
                         proxyServer->clear();
                         proxyPort->setValue(0);
                         proxyAuth->setChecked(false);
                         proxyUsername->clear();
                         proxyPassword->clear();
                         refreshProxyState();
                     });
    m_pageSavers.append(saveUpdateSettings);
    return updatePage;
}
} // namespace snipaste::ui
