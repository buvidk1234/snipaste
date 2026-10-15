#include "ui/paste/paste_group_dialog.h"
#include "ui/paste/paste_manager.h"
#include "ui/paste/paste_window.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

namespace snipaste::ui
{
void showPasteGroupDialog(PasteManager &pasteManager)
{
    QDialog dialog;
    dialog.setWindowTitle(QStringLiteral("管理贴图组"));
    dialog.resize(420, 500);
    auto *list = new QListWidget(&dialog);
    list->addItems(pasteManager.groupNames());
    list->setCurrentRow(pasteManager.activeGroup());
    auto *pins = new QListWidget(&dialog);
    pins->setObjectName(QStringLiteral("pasteGroupThumbnails"));
    pins->setIconSize(QSize(64, 48));
    pins->setMaximumHeight(170);
    pins->setViewMode(QListView::IconMode);
    pins->setResizeMode(QListView::Adjust);
    const auto refreshPins = [&] {
        pins->clear();
        const int groupIndex = list->currentRow();
        const auto windows = pasteManager.windowsInGroup(groupIndex);
        int number = 1;
        for (snipaste::ui::PasteWindow *window : windows)
        {
            if (!window)
                continue;
            QPixmap thumbnail = QPixmap::fromImage(window->displayedImage());
            thumbnail = thumbnail.scaled(QSize(64, 48), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            auto *item = new QListWidgetItem(QIcon(thumbnail), QStringLiteral("贴图 %1").arg(number++), pins);
            item->setToolTip(window->sourceText().isEmpty() ? QStringLiteral("图像贴图")
                                                            : window->sourceText());
        }
    };
    refreshPins();
    auto *newButton = new QPushButton(QStringLiteral("新建"), &dialog);
    auto *renameButton = new QPushButton(QStringLiteral("重命名"), &dialog);
    auto *upButton = new QPushButton(QStringLiteral("上移"), &dialog);
    auto *downButton = new QPushButton(QStringLiteral("下移"), &dialog);
    auto *pinUpButton = new QPushButton(QStringLiteral("贴图上移"), &dialog);
    auto *pinDownButton = new QPushButton(QStringLiteral("贴图下移"), &dialog);
    auto *showButton = new QPushButton(QStringLiteral("显示此组"), &dialog);
    auto *emptyButton = new QPushButton(QStringLiteral("清空此组"), &dialog);
    auto *buttonsRow = new QHBoxLayout;
    buttonsRow->addWidget(newButton);
    buttonsRow->addWidget(renameButton);
    buttonsRow->addWidget(upButton);
    buttonsRow->addWidget(downButton);
    buttonsRow->addWidget(pinUpButton);
    buttonsRow->addWidget(pinDownButton);
    buttonsRow->addWidget(showButton);
    buttonsRow->addWidget(emptyButton);
    auto *dialogButtons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(list);
    layout->addWidget(new QLabel(QStringLiteral("当前组贴图顺序"), &dialog));
    layout->addWidget(pins);
    layout->addLayout(buttonsRow);
    layout->addWidget(dialogButtons);
    QObject::connect(list, &QListWidget::currentRowChanged, &dialog,
                     [&, refreshPins](int) { refreshPins(); });
    QObject::connect(newButton, &QPushButton::clicked, &dialog, [&] {
        bool ok = false;
        const QString name =
            QInputDialog::getText(&dialog, QStringLiteral("新建贴图组"), QStringLiteral("组名:"),
                                  QLineEdit::Normal, QString(), &ok);
        if (!ok)
            return;
        const int index = pasteManager.createGroup(name);
        list->addItem(pasteManager.groupNames().value(index));
        list->setCurrentRow(index);
        refreshPins();
    });
    QObject::connect(renameButton, &QPushButton::clicked, &dialog, [&] {
        const int index = list->currentRow();
        if (index < 0)
            return;
        bool ok = false;
        const QString name =
            QInputDialog::getText(&dialog, QStringLiteral("重命名贴图组"), QStringLiteral("组名:"),
                                  QLineEdit::Normal, list->currentItem()->text(), &ok);
        if (ok)
        {
            pasteManager.renameGroup(index, name);
            list->currentItem()->setText(pasteManager.groupNames().value(index));
        }
    });
    QObject::connect(upButton, &QPushButton::clicked, &dialog, [&] {
        const int from = list->currentRow();
        if (from <= 0)
            return;
        pasteManager.moveGroup(from, from - 1);
        QListWidgetItem *item = list->takeItem(from);
        list->insertItem(from - 1, item);
        list->setCurrentRow(from - 1);
        refreshPins();
    });
    QObject::connect(downButton, &QPushButton::clicked, &dialog, [&] {
        const int from = list->currentRow();
        if (from < 0 || from + 1 >= list->count())
            return;
        pasteManager.moveGroup(from, from + 1);
        QListWidgetItem *item = list->takeItem(from);
        list->insertItem(from + 1, item);
        list->setCurrentRow(from + 1);
        refreshPins();
    });
    QObject::connect(pinUpButton, &QPushButton::clicked, &dialog, [&] {
        const int groupIndex = list->currentRow();
        const int from = pins->currentRow();
        if (groupIndex < 0 || from <= 0)
            return;
        pasteManager.moveWindowInGroup(groupIndex, from, from - 1);
        refreshPins();
        pins->setCurrentRow(from - 1);
    });
    QObject::connect(pinDownButton, &QPushButton::clicked, &dialog, [&] {
        const int groupIndex = list->currentRow();
        const int from = pins->currentRow();
        const int count = pasteManager.windowsInGroup(groupIndex).size();
        if (groupIndex < 0 || from < 0 || from + 1 >= count)
            return;
        pasteManager.moveWindowInGroup(groupIndex, from, from + 1);
        refreshPins();
        pins->setCurrentRow(from + 1);
    });
    QObject::connect(showButton, &QPushButton::clicked, &dialog,
                     [&] { pasteManager.showGroup(list->currentRow()); });
    QObject::connect(emptyButton, &QPushButton::clicked, &dialog, [&] {
        if (list->currentRow() == pasteManager.activeGroup())
            pasteManager.emptyCurrentGroup();
        refreshPins();
    });
    QObject::connect(dialogButtons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}
} // namespace snipaste::ui
