#include "ui/preferences/preferences_dialog.h"
#include "core/config/config_store.h"
#include "platform/startup_integration.h"
#include "ui/preferences/preference_widgets.h"
#include <QApplication>
#include <QCheckBox>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace snipaste::ui
{
using preferences::compactLayoutTree;
PreferencesDialog::PreferencesDialog(core::ConfigStore &config, QWidget *parent)
    : QDialog(parent), m_config(config)
{
    setWindowTitle(QStringLiteral("Snipaste 首选项"));
    // Global capture hotkeys open independent windows that must still receive input.
    setModal(false);
    setAttribute(Qt::WA_QuitOnClose, false);
    // All pages share a fixed logical width; their height follows the active tab.
    setFixedWidth(508);
    setMinimumHeight(0);
    setMaximumHeight(QWIDGETSIZE_MAX);
    setWindowFlag(Qt::WindowStaysOnTopHint, true);
    QFont compactFont = QApplication::font();
    compactFont.setPointSize(9);
    setFont(compactFont);
    setStyleSheet(
        QStringLiteral("QDialog { background: #f3f2ef; }"
                       "QTabWidget::pane { background: #ffffff; border: 1px solid #d5d1c9; }"
                       "QTabBar::tab { background: #e7e4de; border: 1px solid #d5d1c9; "
                       "padding: 4px 8px; min-width: 32px; }"
                       "QTabBar::tab:selected { background: #ffffff; border-bottom-color: #ffffff; }"
                       "QGroupBox { margin-top: 8px; padding: 5px; border: 1px solid #d8d5cf; }"
                       "QPushButton { min-height: 24px; padding: 3px 10px; }"
                       "QLineEdit, QComboBox, QKeySequenceEdit { min-height: 22px; }"
                       // Keep QSpinBox on the platform style.  Styling the spin box itself
                       // makes Qt replace the Windows up/down glyphs with large, filled
                       // triangles, which is visibly different from the compact reference UI.
                       "QComboBox { padding-left: 3px; padding-right: 3px; }"
                       "QCheckBox { spacing: 5px; }"));

    auto *tabs = new QTabWidget(this);

    tabs->setObjectName(QStringLiteral("preferencesTabs"));
    auto *generalPage = createGeneralPage(tabs);
    tabs->addTab(generalPage, QStringLiteral("常规"));
    auto *interfacePage = createInterfacePage(tabs);
    auto *interfaceTabs = interfacePage->findChild<QTabWidget *>();
    tabs->addTab(interfacePage, QStringLiteral("界面"));

    auto *capturePage = createCapturePage(tabs);
    auto *captureTabs = capturePage->findChild<QTabWidget *>(QStringLiteral("captureTabs"));
    tabs->addTab(capturePage, QStringLiteral("截屏"));

    auto *pastePage = createPastePage(tabs);
    auto *pasteTabs = pastePage->findChild<QTabWidget *>(QStringLiteral("pasteTabs"));
    tabs->addTab(pastePage, QStringLiteral("贴图"));

    auto *outputPage = createOutputPage(tabs);
    auto *outputTabs = outputPage->findChild<QTabWidget *>(QStringLiteral("outputTabs"));
    tabs->addTab(outputPage, QStringLiteral("输出"));

    auto *controlPage = createControlPage(tabs);
    auto *controlTabs = controlPage->findChild<QTabWidget *>(QStringLiteral("controlTabs"));
    tabs->addTab(controlPage, QStringLiteral("控制"));

    // The update page follows the compact professional edition layout.  Keep
    // the old general/* keys as compatibility aliases while storing the
    // complete settings under update/* for future updater integration.
    auto *updatePage = createUpdatePage(tabs);
    tabs->addTab(updatePage, QStringLiteral("更新"));

    auto *aboutPage = createAboutPage(tabs);
    tabs->addTab(aboutPage, QStringLiteral("关于"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->addWidget(tabs);
    compactLayoutTree(layout);

    const auto resizeToCurrentPage = [this, tabs, layout] {
        QWidget *page = tabs->currentWidget();
        if (!page)
            return;

        const int pageReferenceHeight = page->property("referenceHeight").toInt();
        int pageHeight = pageReferenceHeight > 0 ? pageReferenceHeight : page->sizeHint().height();
        if (auto *nested = page->findChild<QTabWidget *>())
        {
            QWidget *current = nested->currentWidget();
            const int referenceHeight = current ? current->property("referenceHeight").toInt() : 0;
            const int currentHeight =
                referenceHeight > 0 ? referenceHeight : (current ? current->sizeHint().height() : 0);
            const int nestedHeight = currentHeight + nested->tabBar()->sizeHint().height() + 8;
            nested->setFixedHeight(nestedHeight);
            pageHeight = nestedHeight + 8;
        }

        const int outerTabHeight = tabs->tabBar()->sizeHint().height() + pageHeight + 8;
        tabs->setFixedHeight(outerTabHeight);
        const QMargins margins = layout->contentsMargins();
        const int totalHeight = margins.top() + margins.bottom() + outerTabHeight;
        setFixedHeight(totalHeight);
    };
    const auto queueResize = [this, resizeToCurrentPage] {
        QTimer::singleShot(0, this, resizeToCurrentPage);
    };
    QObject::connect(tabs, &QTabWidget::currentChanged, this, [queueResize](int) { queueResize(); });
    for (QTabWidget *nested : {interfaceTabs, captureTabs, pasteTabs, outputTabs, controlTabs})
    {
        QObject::connect(nested, &QTabWidget::currentChanged, this, [queueResize](int) { queueResize(); });
    }
    QTimer::singleShot(0, this, resizeToCurrentPage);
}

void PreferencesDialog::accept()
{
    const bool startup = m_startupCheck->isChecked();
    const bool startupChanged = startup != m_config.value(QStringLiteral("general/startup"), false).toBool();
    for (const auto &save : m_pageSavers)
        save();
    m_config.save();
    if (startupChanged && !platform::setLoginStartup(startup))
        qWarning() << "failed to update login startup";
    QDialog::accept();
}
void PreferencesDialog::reject()
{
    // Closing preferences applies changes and refreshes runtime settings.
    accept();
}
} // namespace snipaste::ui
