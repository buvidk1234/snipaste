#include "ui/preferences/preferences_dialog.h"

#include "core/config/config_store.h"
#include "platform/keyboard.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSizePolicy>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <memory>

#include "core/command/argument_parser.h"
#include "ui/preferences/preference_widgets.h"

namespace snipaste::ui
{
using namespace preferences;
QWidget *PreferencesDialog::createControlPage(QWidget *parent)
{
    struct PageControls
    {
        QLineEdit *ignoreApplicationsEdit = nullptr;
        QKeySequenceEdit *snipHotkeyEdit = nullptr;
        QKeySequenceEdit *copyHotkeyEdit = nullptr;
        QKeySequenceEdit *pasteHotkeyEdit = nullptr;
        QVector<QComboBox *> controlMouseCombos;
        QVector<QLineEdit *> controlTrayCommands;
        QVector<QLineEdit *> controlCornerCommands;
    };
    auto controls = std::make_shared<PageControls>();
    auto *controlPage = new QWidget(parent);
    auto *controlLayout = new QVBoxLayout(controlPage);
    auto *controlTabs = new QTabWidget(controlPage);
    controlTabs->setObjectName(QStringLiteral("controlTabs"));

    auto *globalKeys = new QWidget(controlTabs);
    auto *globalKeysLayout = new QVBoxLayout(globalKeys);
    globalKeysLayout->setContentsMargins(8, 8, 8, 8);
    globalKeysLayout->setSpacing(2);
    controls->snipHotkeyEdit = makeEditableHotkeyRow(
        QStringLiteral("截屏:"),
        QKeySequence(m_config.value(QStringLiteral("hotkeys/snip"), QStringLiteral("F1")).toString()),
        globalKeys);
    globalKeysLayout->addWidget(controls->snipHotkeyEdit->parentWidget());
    controls->copyHotkeyEdit = makeEditableHotkeyRow(
        QStringLiteral("截屏并自动复制:"),
        QKeySequence(m_config
                         .value(QStringLiteral("hotkeys/copy"),
                                snipaste::platform::primaryPortableShortcutText(QStringLiteral("F1")))
                         .toString()),
        globalKeys);
    globalKeysLayout->addWidget(controls->copyHotkeyEdit->parentWidget());
    const auto addGlobalHotkey = [=, this](const QString &label, const QString &key,
                                           const QString &fallback) {
        auto *edit =
            makeEditableHotkeyRow(label, QKeySequence(m_config.value(key, fallback).toString()), globalKeys);
        edit->setObjectName(key);
        edit->setProperty("defaultShortcut", fallback);
        globalKeysLayout->addWidget(edit->parentWidget());
        m_pageSavers.append(
            [=, this] { m_config.setValue(key, edit->keySequence().toString(QKeySequence::PortableText)); });
    };
    addGlobalHotkey(QStringLiteral("自定义截屏:"), QStringLiteral("hotkeys/custom"),
                    QStringLiteral("Shift+F1"));
    controls->pasteHotkeyEdit = makeEditableHotkeyRow(
        QStringLiteral("贴图:"),
        QKeySequence(m_config.value(QStringLiteral("hotkeys/paste"), QStringLiteral("F3")).toString()),
        globalKeys);
    globalKeysLayout->addWidget(controls->pasteHotkeyEdit->parentWidget());
    addGlobalHotkey(QStringLiteral("隐藏/显示所有贴图:"), QStringLiteral("hotkeys/toggleImages"),
                    QStringLiteral("Shift+F3"));
    addGlobalHotkey(QStringLiteral("切换到另一贴图组:"), QStringLiteral("hotkeys/switchGroup"),
                    snipaste::platform::primaryPortableShortcutText(QStringLiteral("F3")));
    addGlobalHotkey(QStringLiteral("开/关鼠标穿透:"), QStringLiteral("hotkeys/clickThrough"), QString());
    controls->snipHotkeyEdit->setObjectName(QStringLiteral("hotkeys/snip"));
    controls->copyHotkeyEdit->setObjectName(QStringLiteral("hotkeys/copy"));
    controls->pasteHotkeyEdit->setObjectName(QStringLiteral("hotkeys/paste"));

    auto *addGlobalCommand = new QPushButton(QStringLiteral("＋ 添加新命令"), globalKeys);
    addGlobalCommand->setObjectName(QStringLiteral("addGlobalCommand"));
    addGlobalCommand->setToolTip(QStringLiteral("为任意命令绑定全局快捷键，保存后立即生效。"));
    addGlobalCommand->setFlat(true);
    addGlobalCommand->setStyleSheet(
        QStringLiteral("QPushButton { color: #3b6ea8; text-align: left; padding: 2px 0; }"));
    QObject::connect(addGlobalCommand, &QPushButton::clicked, this, [=, this] {
        QDialog editor(this);
        editor.setWindowTitle(QStringLiteral("编辑命令快捷键"));
        editor.setModal(true);
        editor.resize(520, 300);
        auto *layout = new QVBoxLayout(&editor);
        auto *help =
            new QLabel(QStringLiteral("选择命令并设置一个快捷键；命令也可以直接填写命令行别名。"), &editor);
        help->setWordWrap(true);
        layout->addWidget(help);
        auto *table = new QTableWidget(&editor);
        table->setColumnCount(3);
        table->setHorizontalHeaderLabels(
            {QStringLiteral("命令"), QStringLiteral("快捷键"), QStringLiteral("操作")});
        table->setSelectionMode(QAbstractItemView::NoSelection);
        table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
        table->horizontalHeader()->setStretchLastSection(false);
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        const QStringList builtIns = {
            QStringLiteral("snip.start"),        QStringLiteral("snip.copy"),
            QStringLiteral("snip.fullScreen"),   QStringLiteral("snip.activeScreen"),
            QStringLiteral("snip.window"),       QStringLiteral("snip.element"),
            QStringLiteral("snip.last"),         QStringLiteral("snip.custom"),
            QStringLiteral("paste.clipboard"),   QStringLiteral("paste.toggleAll"),
            QStringLiteral("paste.switchGroup"), QStringLiteral("paste.switchGroupPrev"),
            QStringLiteral("paste.createGroup"), QStringLiteral("paste.emptyGroup"),
            QStringLiteral("paste.solo"),        QStringLiteral("paste.destroySelected"),
            QStringLiteral("paste.destroyAll"),  QStringLiteral("history.open"),
            QStringLiteral("history.clear"),     QStringLiteral("preferences.open"),
            QStringLiteral("hotkeys.toggle"),    QStringLiteral("pickColor"),
            QStringLiteral("barcodeDecode"),     QStringLiteral("app.about"),
            QStringLiteral("app.quit")};
        const auto addRow = [table, builtIns](const QString &command, const QString &shortcut) {
            const int row = table->rowCount();
            table->insertRow(row);
            auto *commandEdit = new QComboBox(table);
            commandEdit->setEditable(true);
            commandEdit->addItems(builtIns);
            commandEdit->setCurrentText(command);
            table->setCellWidget(row, 0, commandEdit);
            auto *keyEdit = new QKeySequenceEdit(QKeySequence(shortcut), table);
            keyEdit->setMaximumSequenceLength(1);
            table->setCellWidget(row, 1, keyEdit);
            auto *remove = new QPushButton(QStringLiteral("删除"), table);
            remove->setFixedWidth(54);
            table->setCellWidget(row, 2, remove);
            QObject::connect(remove, &QPushButton::clicked, table, [table, remove] {
                for (int index = 0; index < table->rowCount(); ++index)
                {
                    if (table->cellWidget(index, 2) == remove)
                    {
                        table->removeRow(index);
                        break;
                    }
                }
            });
        };
        QStringList bindings = m_config.value(QStringLiteral("hotkeys/commands")).toStringList();
        if (bindings.isEmpty())
            bindings = m_config.value(QStringLiteral("hotkeys/customCommands")).toStringList();
        for (const QString &binding : bindings)
        {
            const int separator = binding.indexOf(QChar('='));
            if (separator <= 0)
                continue;
            QString command = binding.left(separator).trimmed();
            QString shortcut = binding.mid(separator + 1).trimmed();
            const QKeySequence parsed(shortcut);
            if (parsed.isEmpty())
                std::swap(command, shortcut);
            addRow(command, shortcut);
        }
        if (table->rowCount() == 0)
            addRow(QStringLiteral("snip.start"), QString());
        layout->addWidget(table, 1);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &editor);
        auto *add = buttons->addButton(QStringLiteral("添加"), QDialogButtonBox::ActionRole);
        QObject::connect(add, &QPushButton::clicked, &editor,
                         [addRow] { addRow(QStringLiteral("snip.start"), QString()); });
        QObject::connect(buttons, &QDialogButtonBox::accepted, &editor, &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &editor, &QDialog::reject);
        layout->addWidget(buttons);
        if (editor.exec() != QDialog::Accepted)
            return;
        QStringList result;
        for (int row = 0; row < table->rowCount(); ++row)
        {
            auto *commandEdit = qobject_cast<QComboBox *>(table->cellWidget(row, 0));
            auto *keyEdit = qobject_cast<QKeySequenceEdit *>(table->cellWidget(row, 1));
            if (!commandEdit || !keyEdit || commandEdit->currentText().trimmed().isEmpty() ||
                keyEdit->keySequence().isEmpty())
                continue;
            result.append(commandEdit->currentText().trimmed() + QStringLiteral("=") +
                          keyEdit->keySequence().toString(QKeySequence::PortableText));
        }
        m_config.setValue(QStringLiteral("hotkeys/commands"), result);
        m_config.setValue(QStringLiteral("hotkeys/customCommands"), result);
    });
    globalKeysLayout->addWidget(addGlobalCommand, 0, Qt::AlignLeft);

    auto *superGroup = new QGroupBox(QStringLiteral("超级截屏"), globalKeys);
    superGroup->setToolTip(
        QStringLiteral("%1 + 鼠标拖拽触发截图").arg(snipaste::platform::primaryModifierText()));
    auto *superLayout = new QGridLayout(superGroup);
    superLayout->setContentsMargins(8, 5, 8, 6);
    superLayout->setHorizontalSpacing(4);
    superLayout->setVerticalSpacing(4);
    auto *superModifier = new QComboBox(superGroup);
    superModifier->addItems({QStringLiteral("无"), QStringLiteral("Ctrl"), QStringLiteral("Shift"),
                             QStringLiteral("Alt")
#if defined(Q_OS_MACOS)
                                 ,
                             QStringLiteral("Command")
#endif
    });
    superModifier->setCurrentText(
        m_config.value(QStringLiteral("control/superCaptureModifier"), QStringLiteral("无")).toString());
    auto *superMouse = new QComboBox(superGroup);
    superMouse->addItems({QStringLiteral("鼠标左键拖拽"), QStringLiteral("鼠标右键拖拽")});
    superMouse->setCurrentText(
        m_config.value(QStringLiteral("control/superCaptureMouse"), QStringLiteral("鼠标左键拖拽"))
            .toString());
    superLayout->addWidget(new QLabel(
#if defined(Q_OS_MACOS)
                               QStringLiteral("修饰键")
#else
                               QStringLiteral("Win")
#endif
                                   ,
                               superGroup),
                           0, 0);
    superLayout->addWidget(new QLabel(QStringLiteral("+"), superGroup), 0, 1);
    superLayout->addWidget(superModifier, 0, 2);
    superLayout->addWidget(new QLabel(QStringLiteral("+"), superGroup), 0, 3);
    superLayout->addWidget(superMouse, 0, 4);
    superLayout->addWidget(new QLabel(QStringLiteral("→ 截屏并贴图"), superGroup), 0, 5);
    superLayout->setColumnStretch(4, 1);
    globalKeysLayout->addWidget(superGroup);

    auto *globalRestore = new QPushButton(QStringLiteral("恢复默认"), globalKeys);
    globalRestore->setFixedWidth(86);
    globalRestore->setObjectName(QStringLiteral("controlGlobalRestore"));
    globalKeysLayout->addWidget(globalRestore, 0, Qt::AlignRight);
    controls->ignoreApplicationsEdit = new QLineEdit(globalKeys);
    controls->ignoreApplicationsEdit->setText(m_config.value(QStringLiteral("hotkeys/ignoreApplications"))
                                                  .toStringList()
                                                  .join(QStringLiteral(", ")));
    controls->ignoreApplicationsEdit->hide();
    QObject::connect(globalRestore, &QPushButton::clicked, this, [=] {
        for (auto *edit : globalKeys->findChildren<QKeySequenceEdit *>())
            if (edit->property("defaultShortcut").isValid())
                edit->setKeySequence(QKeySequence(edit->property("defaultShortcut").toString()));
        controls->snipHotkeyEdit->setKeySequence(QKeySequence(QStringLiteral("F1")));
        controls->copyHotkeyEdit->setKeySequence(
            QKeySequence(snipaste::platform::primaryPortableShortcutText(QStringLiteral("F1"))));
        controls->pasteHotkeyEdit->setKeySequence(QKeySequence(QStringLiteral("F3")));
        superModifier->setCurrentText(QStringLiteral("无"));
        superMouse->setCurrentText(QStringLiteral("鼠标左键拖拽"));
    });
    m_pageSavers.append([=, this] {
        m_config.setValue(QStringLiteral("control/superCaptureModifier"), superModifier->currentText());
        m_config.setValue(QStringLiteral("control/superCaptureMouse"), superMouse->currentText());
    });
    controlTabs->addTab(globalKeys, QStringLiteral("全局快捷键"));

    auto *applicationKeys = new QWidget(controlTabs);
    auto *applicationLayout = new QVBoxLayout(applicationKeys);
    applicationLayout->setContentsMargins(10, 8, 10, 8);
    applicationLayout->setSpacing(5);
    auto *printHotkey = makeEditableHotkeyRow(
        QStringLiteral("打印:"),
        QKeySequence(snipaste::platform::primaryPortableShortcutText(QStringLiteral("P"))), applicationKeys);
    applicationLayout->addWidget(printHotkey->parentWidget());
    auto *addApplicationCommand = new QPushButton(QStringLiteral("＋ 添加新命令"), applicationKeys);
    addApplicationCommand->setToolTip(
        QStringLiteral("打开命令快捷键编辑器；应用内命令和全局命令使用同一套配置。"));
    addApplicationCommand->setFlat(true);
    addApplicationCommand->setStyleSheet(
        QStringLiteral("QPushButton { color: #3b6ea8; text-align: left; padding: 2px 0; }"));
    QObject::connect(addApplicationCommand, &QPushButton::clicked, addGlobalCommand, &QPushButton::click);
    applicationLayout->addWidget(addApplicationCommand, 0, Qt::AlignLeft);
    auto *internalKeysButton = new QPushButton(QStringLiteral("内部快捷键"), applicationKeys);
    internalKeysButton->setFixedWidth(92);
    internalKeysButton->setToolTip(QStringLiteral("截图和贴图窗口中的快捷键"));
    QObject::connect(internalKeysButton, &QPushButton::clicked, this, [=, this] {
        QMessageBox::information(
            this, QStringLiteral("内部快捷键"),
            QStringLiteral("截图：Enter 复制，%1 贴图，%2 保存，%3/%4 撤销/重做；\n"
                           "贴图：1/2 旋转，3/4 镜像，5 灰度，6 反色，Esc 关闭，Shift+Esc 销毁。")
                .arg(snipaste::platform::primaryShortcutText(QStringLiteral("T")))
                .arg(snipaste::platform::primaryShortcutText(QStringLiteral("S")))
                .arg(snipaste::platform::primaryShortcutText(QStringLiteral("Z")))
                .arg(snipaste::platform::primaryShortcutText(QStringLiteral("Y"))));
    });
    applicationLayout->addWidget(internalKeysButton, 0, Qt::AlignLeft);
    applicationLayout->addStretch();
    auto *applicationRestore = new QPushButton(QStringLiteral("恢复默认"), applicationKeys);
    applicationRestore->setFixedWidth(86);
    QObject::connect(applicationRestore, &QPushButton::clicked, printHotkey, [printHotkey] {
        printHotkey->setKeySequence(
            QKeySequence(snipaste::platform::primaryPortableShortcutText(QStringLiteral("P"))));
    });
    applicationLayout->addWidget(applicationRestore, 0, Qt::AlignRight);
    controlTabs->addTab(applicationKeys, QStringLiteral("应用内快捷键"));

    auto *mouseKeys = new QWidget(controlTabs);
    auto *mouseLayout = new QVBoxLayout(mouseKeys);
    mouseLayout->setContentsMargins(8, 8, 8, 8);
    auto *pasteMouseGroup = new QGroupBox(QStringLiteral("贴图操作"), mouseKeys);
    auto *mouseGrid = new QGridLayout(pasteMouseGroup);
    mouseGrid->setContentsMargins(8, 8, 8, 8);
    mouseGrid->setHorizontalSpacing(6);
    mouseGrid->setVerticalSpacing(6);

    const auto addMouseRow = [=, this](int row, const QString &label, const QString &first,
                                       const QString &second, const QString &third, const QString &fourth,
                                       bool showInfo = false) {
        auto *labelWidget = new QLabel(label, pasteMouseGroup);
        auto *firstBox = new QComboBox(pasteMouseGroup);
        auto *secondBox = new QComboBox(pasteMouseGroup);
        firstBox->addItems(
            {QStringLiteral("无"), QStringLiteral("Ctrl"), QStringLiteral("Shift"), QStringLiteral("Alt")});
#if defined(Q_OS_MACOS)
        firstBox->addItem(QStringLiteral("Cmd"));
#endif
        secondBox->addItems({QStringLiteral("无"), QStringLiteral("左键双击"), QStringLiteral("中键单击"),
                             QStringLiteral("右键单击"), QStringLiteral("鼠标右键拖拽")});
        firstBox->setProperty("defaultText", first);
        secondBox->setProperty("defaultText", second);
        firstBox->setCurrentText(first);
        secondBox->setCurrentText(second);
        const auto registerMouseCombo = [=, this](QComboBox *box) {
            const int index = controls->controlMouseCombos.size();
            controls->controlMouseCombos.append(box);
            const QString saved =
                m_config.value(QStringLiteral("control/mouseAction%1").arg(index)).toString();
            if (!saved.isEmpty())
                box->setCurrentText(saved);
        };
        registerMouseCombo(firstBox);
        registerMouseCombo(secondBox);
        firstBox->setFixedWidth(64);
        secondBox->setFixedWidth(84);
        mouseGrid->addWidget(labelWidget, row, 0);
        mouseGrid->addWidget(firstBox, row, 1);
        mouseGrid->addWidget(new QLabel(QStringLiteral("+"), pasteMouseGroup), row, 2);
        mouseGrid->addWidget(secondBox, row, 3);
        if (!third.isEmpty())
        {
            auto *thirdBox = new QComboBox(pasteMouseGroup);
            auto *fourthBox = new QComboBox(pasteMouseGroup);
            thirdBox->addItems({QStringLiteral("鼠标滚轮"), QStringLiteral("无")});
            fourthBox->addItems({QStringLiteral("+/-"), QStringLiteral("无")});
            thirdBox->setProperty("defaultText", third);
            fourthBox->setProperty("defaultText", fourth);
            thirdBox->setCurrentText(third);
            fourthBox->setCurrentText(fourth);
            registerMouseCombo(thirdBox);
            registerMouseCombo(fourthBox);
            thirdBox->setFixedWidth(92);
            fourthBox->setFixedWidth(58);
            mouseGrid->addWidget(new QLabel(QStringLiteral("+"), pasteMouseGroup), row, 4);
            mouseGrid->addWidget(thirdBox, row, 5);
            mouseGrid->addWidget(new QLabel(QStringLiteral("或"), pasteMouseGroup), row, 6);
            mouseGrid->addWidget(fourthBox, row, 7);
        }
        if (showInfo)
            mouseGrid->addWidget(makeInfoButton(pasteMouseGroup), row, 8);
    };
    addMouseRow(0, QStringLiteral("贴图缩放:"), QStringLiteral("无"), QStringLiteral("无"),
                QStringLiteral("鼠标滚轮"), QStringLiteral("+/-"));
    addMouseRow(1, QStringLiteral("调整透明度:"), snipaste::platform::primaryModifierText(),
                QStringLiteral("无"), QStringLiteral("鼠标滚轮"), QStringLiteral("+/-"));
    addMouseRow(2, QStringLiteral("关闭贴图:"), QStringLiteral("无"), QStringLiteral("左键双击"), QString(),
                QString());
    addMouseRow(3, QStringLiteral("重置贴图:"), QStringLiteral("无"), QStringLiteral("中键单击"), QString(),
                QString());
    addMouseRow(4, QStringLiteral("快速缩略图:"), QStringLiteral("Shift"), QStringLiteral("左键双击"),
                QString(), QString());
    addMouseRow(5, QStringLiteral("选区缩略图:"), QStringLiteral("无"), QStringLiteral("鼠标右键拖拽"),
                QString(), QString());
    addMouseRow(6, QStringLiteral("复制文本:"), QStringLiteral("Shift"), QStringLiteral("右键单击"),
                QString(), QString(), true);
    mouseGrid->setColumnStretch(3, 1);
    pasteMouseGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    mouseLayout->addWidget(pasteMouseGroup);

    auto *trayGroup = new QGroupBox(QStringLiteral("托盘图标"), mouseKeys);
    auto *trayGrid = new QGridLayout(trayGroup);
    trayGrid->setContentsMargins(8, 6, 8, 6);
    trayGrid->setHorizontalSpacing(6);
    trayGrid->setVerticalSpacing(5);
    const QStringList trayLabels = {QStringLiteral("左键点击:"), QStringLiteral("中键点击:"),
                                    QStringLiteral("右键点击:")};
    const QStringList trayKeys = {QStringLiteral("control/trayLeft"), QStringLiteral("control/trayMiddle"),
                                  QStringLiteral("control/trayRight")};
    const QStringList trayDefaults = {QStringLiteral("snip"), QStringLiteral("paste"),
                                      QStringLiteral("show-tray-menu")};
    for (int row = 0; row < trayLabels.size(); ++row)
    {
        trayGrid->addWidget(new QLabel(trayLabels.at(row), trayGroup), row, 0);
        auto *command = new QLineEdit(trayGroup);
        command->setText(m_config.value(trayKeys.at(row), trayDefaults.at(row)).toString());
        command->setObjectName(QStringLiteral("trayCommand%1").arg(row));
        command->setMinimumWidth(154);
        auto *preset = makeCommandPreset(command, trayGroup);
        preset->setObjectName(QStringLiteral("trayPreset%1").arg(row));
        trayGrid->addWidget(command, row, 1);
        trayGrid->addWidget(preset, row, 2);
        controls->controlTrayCommands.append(command);
    }
    mouseLayout->addWidget(trayGroup);
    mouseLayout->addWidget(makeHint(QStringLiteral("* 如有冲突，由上至下优先匹配"), mouseKeys), 0,
                           Qt::AlignRight);
    auto *mouseRestore = new QPushButton(QStringLiteral("恢复默认"), mouseKeys);
    mouseRestore->setFixedWidth(86);
    mouseRestore->setObjectName(QStringLiteral("controlMouseRestore"));
    mouseLayout->addWidget(mouseRestore, 0, Qt::AlignRight);
    QObject::connect(mouseRestore, &QPushButton::clicked, this, [=] {
        for (QComboBox *box : controls->controlMouseCombos)
            box->setCurrentText(box->property("defaultText").toString());
        const QStringList defaults = {QStringLiteral("snip"), QStringLiteral("paste"),
                                      QStringLiteral("show-tray-menu")};
        for (int i = 0; i < controls->controlTrayCommands.size() && i < defaults.size(); ++i)
            controls->controlTrayCommands.at(i)->setText(defaults.at(i));
    });
    m_pageSavers.append([=, this] {
        const QStringList keys = {QStringLiteral("control/trayLeft"), QStringLiteral("control/trayMiddle"),
                                  QStringLiteral("control/trayRight")};
        for (int index = 0; index < controls->controlTrayCommands.size() && index < keys.size(); ++index)
            m_config.setValue(keys.at(index), controls->controlTrayCommands.at(index)->text().trimmed());
    });
    controlTabs->addTab(mouseKeys, QStringLiteral("鼠标"));

    auto *corners = new QWidget(controlTabs);
    auto *cornerLayout = new QVBoxLayout(corners);
    cornerLayout->setContentsMargins(8, 8, 8, 8);
    cornerLayout->setSpacing(5);
    auto *cornerEnabled = new QCheckBox(QStringLiteral("启用"), corners);
    cornerEnabled->setObjectName(QStringLiteral("controlCornerEnabled"));
    cornerEnabled->setChecked(m_config.value(QStringLiteral("control/cornerEnabled"), false).toBool());
    cornerLayout->addWidget(cornerEnabled);
    auto *cornerOptions = new QFormLayout;
    cornerOptions->setContentsMargins(0, 0, 0, 0);
    cornerOptions->setHorizontalSpacing(12);
    auto *cornerSize = new QSpinBox(corners);
    cornerSize->setObjectName(QStringLiteral("controlCornerSize"));
    cornerSize->setRange(1, 200);
    cornerSize->setSuffix(QStringLiteral(" 像素"));
    cornerSize->setValue(m_config.value(QStringLiteral("control/cornerSize"), 10).toInt());
    auto *cornerDelay = new QSpinBox(corners);
    cornerDelay->setObjectName(QStringLiteral("controlCornerDelay"));
    cornerDelay->setRange(0, 5000);
    cornerDelay->setSuffix(QStringLiteral(" 毫秒"));
    cornerDelay->setValue(m_config.value(QStringLiteral("control/cornerDelay"), 300).toInt());
    auto *cornerInterval = new QSpinBox(corners);
    cornerInterval->setObjectName(QStringLiteral("controlCornerInterval"));
    cornerInterval->setRange(10, 2000);
    cornerInterval->setSuffix(QStringLiteral(" 毫秒"));
    cornerInterval->setValue(m_config.value(QStringLiteral("control/cornerInterval"), 125).toInt());
    cornerSize->setFixedWidth(112);
    cornerDelay->setFixedWidth(112);
    cornerInterval->setFixedWidth(112);
    cornerOptions->addRow(QStringLiteral("区域大小:"), cornerSize);
    cornerOptions->addRow(QStringLiteral("触发延迟:"), cornerDelay);
    cornerOptions->addRow(QStringLiteral("检查间隔:"), cornerInterval);
    cornerLayout->addLayout(cornerOptions);
    auto *ignoredCornerRow = new QHBoxLayout;
    auto *ignoredCorner = new QLineEdit(corners);
    ignoredCorner->setObjectName(QStringLiteral("controlCornerIgnoredApplications"));
    ignoredCorner->setPlaceholderText(QStringLiteral("例如：explorer.exe, devenv.exe"));
    ignoredCorner->setText(m_config.value(QStringLiteral("control/cornerIgnoredApplications"))
                               .toStringList()
                               .join(QStringLiteral(", ")));
    ignoredCornerRow->addWidget(new QLabel(QStringLiteral("忽略程序:"), corners));
    ignoredCornerRow->addWidget(ignoredCorner, 1);
    auto *cornerInfo = makeInfoButton(corners);
    const QString ignoreHelp = QStringLiteral(
        "多个程序名用逗号或分号分隔，不区分大小写。当前前台程序名包含其中任意一项时，不触发屏幕角命令。");
    cornerInfo->setToolTip(ignoreHelp);
    QObject::connect(cornerInfo, &QPushButton::clicked, this, [=, this] {
        QMessageBox::information(this, QStringLiteral("忽略程序列表"), ignoreHelp);
    });
    ignoredCornerRow->addWidget(cornerInfo);
    cornerLayout->addLayout(ignoredCornerRow);

    auto *cornerFunctions = new QGroupBox(QStringLiteral("功能"), corners);
    auto *cornerGrid = new QGridLayout(cornerFunctions);
    cornerGrid->setContentsMargins(8, 6, 8, 6);
    cornerGrid->setHorizontalSpacing(6);
    cornerGrid->setVerticalSpacing(5);
    const QStringList cornerNames = {QStringLiteral("左上角:"), QStringLiteral("右上角:"),
                                     QStringLiteral("左下角:"), QStringLiteral("右下角:")};
    const QStringList cornerKeys = {
        QStringLiteral("control/cornerTopLeft"), QStringLiteral("control/cornerTopRight"),
        QStringLiteral("control/cornerBottomLeft"), QStringLiteral("control/cornerBottomRight")};
    const QStringList cornerDefaults = {QStringLiteral("toggle-images"), QStringLiteral("toggle-images"),
                                        QString(), QString()};
    for (int row = 0; row < cornerNames.size(); ++row)
    {
        cornerGrid->addWidget(new QLabel(cornerNames.at(row), cornerFunctions), row, 0);
        auto *command = new QLineEdit(cornerFunctions);
        command->setText(m_config.value(cornerKeys.at(row), cornerDefaults.at(row)).toString());
        command->setObjectName(QStringLiteral("controlCorner%1").arg(row));
        command->setMinimumWidth(150);
        auto *preset = makeCommandPreset(command, cornerFunctions);
        preset->setObjectName(QStringLiteral("cornerPreset%1").arg(row));
        cornerGrid->addWidget(command, row, 1);
        cornerGrid->addWidget(preset, row, 2);
        controls->controlCornerCommands.append(command);
    }
    cornerLayout->addWidget(cornerFunctions);
    cornerLayout->addStretch();
    auto *cornerRestore = new QPushButton(QStringLiteral("恢复默认"), corners);
    cornerRestore->setFixedWidth(86);
    cornerRestore->setObjectName(QStringLiteral("controlCornerRestore"));
    cornerLayout->addWidget(cornerRestore, 0, Qt::AlignRight);
    QObject::connect(cornerRestore, &QPushButton::clicked, this, [=] {
        cornerEnabled->setChecked(false);
        cornerSize->setValue(10);
        cornerDelay->setValue(300);
        cornerInterval->setValue(125);
        ignoredCorner->clear();
        const QStringList defaults = {QStringLiteral("toggle-images"), QStringLiteral("toggle-images"),
                                      QString(), QString()};
        for (int i = 0; i < controls->controlCornerCommands.size() && i < defaults.size(); ++i)
            controls->controlCornerCommands.at(i)->setText(defaults.at(i));
    });
    m_pageSavers.append([=, this] {
        m_config.setValue(QStringLiteral("control/cornerEnabled"), cornerEnabled->isChecked());
        m_config.setValue(QStringLiteral("control/cornerSize"), cornerSize->value());
        m_config.setValue(QStringLiteral("control/cornerDelay"), cornerDelay->value());
        m_config.setValue(QStringLiteral("control/cornerInterval"), cornerInterval->value());
        QStringList ignored;
        for (const QString &entry :
             ignoredCorner->text().split(QRegularExpression(QStringLiteral("[,;\\n]")), Qt::SkipEmptyParts))
            if (!entry.trimmed().isEmpty())
                ignored.append(entry.trimmed());
        m_config.setValue(QStringLiteral("control/cornerIgnoredApplications"), ignored);
        const QStringList keys = {
            QStringLiteral("control/cornerTopLeft"), QStringLiteral("control/cornerTopRight"),
            QStringLiteral("control/cornerBottomLeft"), QStringLiteral("control/cornerBottomRight")};
        for (int i = 0; i < controls->controlCornerCommands.size() && i < keys.size(); ++i)
            m_config.setValue(keys.at(i), controls->controlCornerCommands.at(i)->text().trimmed());
    });
    controlTabs->addTab(corners, QStringLiteral("触发角"));
    controlLayout->addWidget(controlTabs);
    m_pageSavers.append([=, this] {
        m_config.setValue(QStringLiteral("hotkeys/snip"),
                          controls->snipHotkeyEdit->keySequence().toString(QKeySequence::PortableText));
        m_config.setValue(QStringLiteral("hotkeys/copy"),
                          controls->copyHotkeyEdit->keySequence().toString(QKeySequence::PortableText));
        m_config.setValue(QStringLiteral("hotkeys/paste"),
                          controls->pasteHotkeyEdit->keySequence().toString(QKeySequence::PortableText));
        for (int index = 0; index < controls->controlMouseCombos.size(); ++index)
        {
            m_config.setValue(QStringLiteral("control/mouseAction%1").arg(index),
                              controls->controlMouseCombos.at(index)->currentText());
        }
        const QStringList ignoredApplications = controls->ignoreApplicationsEdit->text().split(
            QRegularExpression(QStringLiteral("[,;]")), Qt::SkipEmptyParts);
        m_config.setValue(QStringLiteral("hotkeys/ignoreApplications"), ignoredApplications);
    });
    return controlPage;
}
} // namespace snipaste::ui
