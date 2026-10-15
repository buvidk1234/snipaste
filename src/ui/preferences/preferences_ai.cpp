#include "ui/preferences/preferences_dialog.h"
#include "core/ai/ai_client.h"
#include "core/config/config_store.h"
#include "ui/preferences/preference_widgets.h"

#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

namespace snipaste::ui
{
QWidget *PreferencesDialog::createAiPage(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 12, 10, 12);
    layout->setSpacing(12);
    auto *hint = preferences::makeHint(QStringLiteral("连接支持图片输入的 OpenAI 兼容服务。"), page);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    const auto settings = core::AiSettings::fromConfig(m_config);
    auto *form = new QFormLayout;
    auto *key = new QLineEdit(settings.apiKey, page);
    key->setObjectName(QStringLiteral("aiApiKey"));
    key->setEchoMode(QLineEdit::Password);
    key->setPlaceholderText(QStringLiteral("填写 API Key"));
    key->setToolTip(QStringLiteral("密钥保存在本机配置文件中。"));
    form->addRow(QStringLiteral("API Key:"), key);
    auto *baseUrl = new QLineEdit(settings.baseUrl, page);
    baseUrl->setObjectName(QStringLiteral("aiBaseUrl"));
    baseUrl->setPlaceholderText(QStringLiteral("https://api.openai.com/v1"));
    form->addRow(QStringLiteral("接口地址:"), baseUrl);
    auto *model = new QLineEdit(settings.model, page);
    model->setObjectName(QStringLiteral("aiModel"));
    model->setPlaceholderText(QStringLiteral("支持图片输入的模型名称"));
    form->addRow(QStringLiteral("模型:"), model);
    layout->addLayout(form);
    auto *protocol = preferences::makeHint(QStringLiteral("使用 Responses API，自动在地址后添加 /responses。\n生图需要接口和模型支持图片生成工具。"), page);
    protocol->setWordWrap(true);
    layout->addWidget(protocol);
    auto *usage = preferences::makeHint(QStringLiteral("框选截图后，点击下方工具栏的 AI 图标开始对话。\n仅在点击发送时提交当前选区图片。"), page);
    usage->setWordWrap(true);
    layout->addWidget(usage);
    layout->addStretch();
    m_pageSavers.append([this, key, baseUrl, model] {
        m_config.setValue(QStringLiteral("ai/apiKey"), key->text().trimmed());
        m_config.setValue(QStringLiteral("ai/baseUrl"), baseUrl->text().trimmed());
        m_config.setValue(QStringLiteral("ai/model"), model->text().trimmed());
    });
    return page;
}
} // namespace snipaste::ui
