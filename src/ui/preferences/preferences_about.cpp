#include "ui/preferences/preferences_dialog.h"

#include <QCoreApplication>
#include <QFont>
#include <QLabel>
#include <QVBoxLayout>

namespace snipaste::ui
{
QWidget *PreferencesDialog::createAboutPage(QWidget *parent)
{
    auto *aboutPage = new QWidget(parent);
    aboutPage->setProperty("referenceHeight", 120);
    auto *aboutLayout = new QVBoxLayout(aboutPage);
    aboutLayout->setContentsMargins(10, 10, 10, 8);
    aboutLayout->setSpacing(6);
    auto *aboutName = new QLabel(QStringLiteral("Snipaste <small>(64 位)</small>"), aboutPage);
    aboutName->setObjectName(QStringLiteral("aboutName"));
    aboutName->setTextFormat(Qt::RichText);
    QFont aboutFont = aboutName->font();
    aboutFont.setPointSize(18);
    aboutName->setFont(aboutFont);
    aboutLayout->addWidget(aboutName);
    auto *aboutVersion = new QLabel(QStringLiteral("版本 %1（开源开发版）")
                                       .arg(QCoreApplication::applicationVersion()), aboutPage);
    aboutVersion->setObjectName(QStringLiteral("aboutVersion"));
    aboutLayout->addWidget(aboutVersion);
    aboutLayout->addStretch();
    return aboutPage;
}
} // namespace snipaste::ui
