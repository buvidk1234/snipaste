#include "ui/preferences/preferences_dialog.h"

#include "core/config/config_store.h"
#include "platform/keyboard.h"

#include <QColor>
#include <QDesktopServices>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QUrl>
#include <QVBoxLayout>

#include "core/command/argument_parser.h"
#include "ui/preferences/preference_widgets.h"

namespace snipaste::ui
{
using namespace preferences;
QWidget *PreferencesDialog::createAboutPage(QWidget *parent)
{
    auto *aboutPage = new QWidget(parent);
    aboutPage->setProperty("referenceHeight", 500);
    auto *aboutLayout = new QVBoxLayout(aboutPage);
    aboutLayout->setContentsMargins(10, 10, 10, 8);
    aboutLayout->setSpacing(6);
    auto *aboutTop = new QHBoxLayout;
    aboutTop->setSpacing(12);
    auto *aboutDetails = new QWidget(aboutPage);
    auto *aboutDetailsLayout = new QVBoxLayout(aboutDetails);
    aboutDetailsLayout->setContentsMargins(0, 0, 0, 0);
    aboutDetailsLayout->setSpacing(3);
    auto *aboutBrandRow = new QHBoxLayout;
    aboutBrandRow->setContentsMargins(0, 0, 0, 0);
    auto *aboutLogo = new QLabel(aboutDetails);
    aboutLogo->setObjectName(QStringLiteral("aboutLogo"));
    QPixmap logo(24, 34);
    logo.fill(Qt::transparent);
    {
        QPainter painter(&logo);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(QStringLiteral("#4c9be8")));
        painter.drawRect(QRect(0, 0, 10, 16));
        painter.setBrush(QColor(QStringLiteral("#f3c442")));
        painter.drawRect(QRect(0, 18, 10, 16));
        painter.setBrush(QColor(QStringLiteral("#e96552")));
        painter.drawRect(QRect(13, 0, 10, 16));
        painter.setBrush(QColor(QStringLiteral("#55b766")));
        painter.drawRect(QRect(13, 18, 10, 16));
    }
    aboutLogo->setPixmap(logo);
    aboutBrandRow->addWidget(aboutLogo);
    auto *aboutName = new QLabel(QStringLiteral("Snipaste <small>(64 位)</small>"), aboutDetails);
    aboutName->setObjectName(QStringLiteral("aboutName"));
    aboutName->setTextFormat(Qt::RichText);
    QFont aboutFont = aboutName->font();
    aboutFont.setPointSize(18);
    aboutName->setFont(aboutFont);
    aboutBrandRow->addWidget(aboutName);
    aboutBrandRow->addStretch();
    aboutDetailsLayout->addLayout(aboutBrandRow);
    auto *aboutVersion = new QLabel(QStringLiteral("版本 0.1.0（开源开发版）"), aboutDetails);
    aboutVersion->setObjectName(QStringLiteral("aboutVersion"));
    aboutDetailsLayout->addWidget(aboutVersion);
    auto *aboutCopyright = new QLabel(QStringLiteral("版权所有 © 2016-2026 snipaste.com"), aboutDetails);
    aboutCopyright->setObjectName(QStringLiteral("aboutCopyright"));
    aboutDetailsLayout->addWidget(aboutCopyright);
    aboutTop->addWidget(aboutDetails, 1);

    auto *aboutButtons = new QVBoxLayout;
    aboutButtons->setSpacing(6);
    auto *builtWithQt = new QLabel(aboutPage);
    builtWithQt->setObjectName(QStringLiteral("aboutBuiltWithQt"));
    builtWithQt->setText(QStringLiteral(
        "<span style='color:white;'>Built with </span>"
        "<span style='background:#111;color:white;font-weight:bold;padding:2px 4px;'>Qt</span>"));
    builtWithQt->setTextFormat(Qt::RichText);
    builtWithQt->setAlignment(Qt::AlignCenter);
    builtWithQt->setStyleSheet(QStringLiteral("background:#111111; padding:2px 3px;"));
    aboutButtons->addWidget(builtWithQt);
    auto *aboutOpenSource = new QPushButton(QStringLiteral("开源许可"), aboutPage);
    aboutOpenSource->setObjectName(QStringLiteral("aboutOpenSourceButton"));
    auto *aboutReview = new QPushButton(QStringLiteral("评价"), aboutPage);
    aboutReview->setObjectName(QStringLiteral("aboutReviewButton"));
    auto *aboutPurchase = new QPushButton(QStringLiteral("购买专业版"), aboutPage);
    aboutPurchase->setObjectName(QStringLiteral("aboutActivationButton"));
    aboutButtons->addWidget(aboutOpenSource);
    aboutButtons->addWidget(aboutReview);
    aboutButtons->addWidget(aboutPurchase);
    aboutTop->addLayout(aboutButtons);
    aboutLayout->addLayout(aboutTop);

    auto *supportBrowser = new QScrollArea(aboutPage);
    supportBrowser->setObjectName(QStringLiteral("aboutSupportBrowser"));
    supportBrowser->setWidgetResizable(true);
    supportBrowser->setFrameShape(QFrame::StyledPanel);
    supportBrowser->setFixedHeight(287);
    auto *supportContent = new QWidget;
    supportContent->setMinimumHeight(350);
    auto *supportLayout = new QVBoxLayout(supportContent);
    supportLayout->setContentsMargins(6, 4, 8, 6);
    supportLayout->setSpacing(2);
    auto *supportText = new QLabel(
        QStringLiteral(
            "<b>支持</b><hr>"
            "官网首页 - <a href='https://zh.snipaste.com'>https://zh.snipaste.com</a><br>"
            "使用手册 - <a href='https://docs.snipaste.com/zh-cn'>https://docs.snipaste.com/zh-cn</a><br>"
            "问题反馈 - <a "
            "href='https://github.com/Snipaste/feedback/issues'>https://snipaste.com/issues</a><br>"
            "社交网站 - <a href='https://weibo.com'>微博</a> | <a href='https://twitter.com'>Twitter</a><br>"
            "相关链接 - <a href='https://snipaste.com'>最终用户许可协议</a> | <a "
            "href='https://snipaste.com'>隐私政策</a><br><br>"
            "<b>译者</b> +<hr>"
            "简体中文　　　　 - <a href='https://github.com/liulex'>liulex</a><br>"
            "繁体中文　　　　 - <a href='https://github.com/Brownsugar'>zhtw, Brownsugar</a><br>"
            "德语　　　　　　 - <a href='https://github.com'>Samuel Marcius</a>"),
        supportContent);
    supportText->setObjectName(QStringLiteral("aboutSupportText"));
    supportText->setTextFormat(Qt::RichText);
    supportText->setOpenExternalLinks(false);
    supportText->setWordWrap(true);
    QObject::connect(supportText, &QLabel::linkActivated, this,
                     [](const QString &url) { QDesktopServices::openUrl(QUrl(url)); });
    supportLayout->addWidget(supportText);
    supportLayout->addStretch();
    supportBrowser->setWidget(supportContent);
    aboutLayout->addWidget(supportBrowser);
    auto *aboutFooter = new QHBoxLayout;
    auto *heart = new QLabel(QStringLiteral("♥"), aboutPage);
    heart->setStyleSheet(QStringLiteral("color:#e05252; font-size:16px;"));
    aboutFooter->addWidget(heart);
    aboutFooter->addStretch();
    auto *contact = new QLabel(
        QStringLiteral("联系:<a href='mailto:snipaste.app@gmail.com'>snipaste.app@gmail.com</a>"), aboutPage);
    contact->setObjectName(QStringLiteral("aboutContact"));
    contact->setTextFormat(Qt::RichText);
    contact->setOpenExternalLinks(true);
    aboutFooter->addWidget(contact);
    aboutLayout->addLayout(aboutFooter);
    QObject::connect(aboutOpenSource, &QPushButton::clicked, this, [this] {
        QMessageBox::information(
            this, QStringLiteral("开源许可"),
            QStringLiteral("snipaste 使用 Qt 开源组件，项目源代码和许可说明位于 docs/ 目录。"));
    });
    QObject::connect(aboutReview, &QPushButton::clicked, this,
                     [] { QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com"))); });
    QObject::connect(aboutPurchase, &QPushButton::clicked, this, [this] {
        QMessageBox::information(this, QStringLiteral("专业版"),
                                 QStringLiteral("这是开源版，专业版购买入口暂未接入。"));
    });
    return aboutPage;
}
} // namespace snipaste::ui
