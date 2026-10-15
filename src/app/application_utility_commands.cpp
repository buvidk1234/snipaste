#include "app/application_controller.h"
#include "app/external_command.h"
#include "core/config/config_store.h"
#include "core/recognition/barcode_service.h"
#include "platform/desktop_integration.h"
#include "ui/clipboard_output.h"
#include "ui/history/screenshot_history_dialog.h"
#include "ui/paste/clipboard_content.h"
#include "ui/preferences/preferences_dialog.h"
#include "ui/snip/capture_support.h"
#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QMessageBox>
#include <QMimeData>
#include <QProcess>

namespace snipaste::app
{

using snipaste::ui::captureAllScreens;
using snipaste::ui::colorAtCursor;

using snipaste::ui::clipboardTextImage;

using snipaste::platform::pasteClipboardAsKeyboardInput;

void ApplicationController::registerUtilityCommands()
{
    m_commands.registerCommand({QStringLiteral("preferences.open"), QStringLiteral("首选项"), [this] {
                                    if (m_preferences && m_preferences->isVisible())
                                    {
                                        m_preferences->showNormal();
                                        m_preferences->raise();
                                        m_preferences->activateWindow();
                                        return;
                                    }
                                    m_preferences = std::make_unique<ui::PreferencesDialog>(m_config);
                                    m_preferences->setUpdateCheckHandler(
                                        [this](QWidget *parent) { m_updates.checkNow(parent); });
                                    QObject::connect(m_preferences.get(), &QDialog::accepted, this,
                                                     [this] { applySettings(); });
                                    m_preferences->show();
                                }});
    m_commands.registerCommand({
        QStringLiteral("history.clear"),
        QStringLiteral("清空截屏历史"),
        [this] {
            m_history.clear();

            qInfo() << "screenshot history cleared";
        },
    });
    m_commands.registerCommand({
        QStringLiteral("history.open"),
        QStringLiteral("打开截屏历史"),
        [this] {
            snipaste::ui::ScreenshotHistoryDialog dialog(m_history.history());
            QObject::connect(&dialog, &snipaste::ui::ScreenshotHistoryDialog::copyRequested,
                             [](const QImage &image) {
                                 snipaste::ui::copyImageToClipboard(
                                     image, false, snipaste::ui::ClipboardImageSource::Screenshot);
                             });
            QObject::connect(&dialog, &snipaste::ui::ScreenshotHistoryDialog::pasteRequested,
                             [this](const QImage &image) { m_pastes.createPaste(image); });
            QObject::connect(&dialog, &snipaste::ui::ScreenshotHistoryDialog::clearRequested, this,
                             [this] { m_history.persist(); });
            dialog.exec();
        },
    });
    m_commands.registerCommand({QStringLiteral("hotkeys.toggle"), QStringLiteral("切换快捷键"),
                                [this] { m_input.setHotkeysEnabled(!m_input.hotkeysEnabled()); }});
    m_commands.registerCommand({QStringLiteral("hotkeys.disable"), QStringLiteral("禁用快捷键"),
                                [this] { m_input.setHotkeysEnabled(false); }});
    m_commands.registerCommand({QStringLiteral("hotkeys.enable"), QStringLiteral("启用快捷键"),
                                [this] { m_input.setHotkeysEnabled(true); }});
    m_commands.registerCommand({
        QStringLiteral("help.open"),
        QStringLiteral("帮助"),
        [] {
            QMessageBox::information(nullptr, QStringLiteral("帮助"),
                                     QStringLiteral("快捷键：F1 截图，F3 贴图。\n"
                                                    "项目文档位于 docs/ 目录。"));
        },
    });
    m_commands.registerCommand({
        QStringLiteral("app.about"),
        QStringLiteral("关于 snipaste"),
        [] {
            QMessageBox::about(nullptr, QStringLiteral("关于 snipaste"),
                               QStringLiteral("snipaste %1\n开源截图、贴图和标注工具")
                                   .arg(QCoreApplication::applicationVersion()));
        },
    });
    m_commands.registerCommand({
        QStringLiteral("update.checkNow"),
        QStringLiteral("检查更新"),
        [this] { m_updates.checkNow(); },
    });
    m_commands.registerCommand({
        QStringLiteral("app.purchase"),
        QStringLiteral("支持原 Snipaste"),
        [] {
            QMessageBox::information(nullptr, QStringLiteral("snipaste"),
                                     QStringLiteral("snipaste 是开源版本，不需要购买授权。\n"
                                                    "如果你需要支持原 Snipaste，请访问其官方网站。"));
        },
    });
    m_commands.registerCommand({
        QStringLiteral("app.restart"),
        QStringLiteral("重新启动"),
        [this] {
            const QString program = QCoreApplication::applicationFilePath();
            QStringList arguments = QCoreApplication::arguments().mid(1);
            arguments.removeAll(QStringLiteral("--snipaste-restart"));
            arguments.append(QStringLiteral("--snipaste-restart"));
            if (QProcess::startDetached(program, arguments))
                m_app.quit();
            else
                qWarning() << "failed to restart application" << program;
        },
    });
    m_commands.registerCommand({
        QStringLiteral("app.quit"),
        QStringLiteral("退出"),
        [this] { m_app.quit(); },
    });
    m_commands.registerCommand({
        QStringLiteral("whiteboard"),
        QStringLiteral("打开白板"),
        [this] { openWhiteboard(QColor(Qt::white), false, false); },
    });
    m_commands.registerCommand({
        QStringLiteral("transparentWhiteboard"),
        QStringLiteral("打开透明白板"),
        [this] { openWhiteboard(QColor(Qt::white), true, false); },
    });
    m_commands.registerCommand({
        QStringLiteral("pickColor"),
        QStringLiteral("拾取颜色"),
        [] {
            const QColor color = colorAtCursor();
            if (color.isValid())
                QApplication::clipboard()->setText(color.name(QColor::HexArgb));
        },
    });
    m_commands.registerCommand({
        QStringLiteral("pasteAsImage"),
        QStringLiteral("将剪贴板转为图像并粘贴"),
        [this] {
            const QMimeData *mime = QApplication::clipboard()->mimeData();
            if (!mime || !mime->hasText())
                return;
            snipaste::ui::copyImageToClipboard(clipboardTextImage(mime->text(), &m_config), false,
                                               snipaste::ui::ClipboardImageSource::Screenshot);
            pasteClipboardAsKeyboardInput();
        },
    });
    m_commands.registerCommand({
        QStringLiteral("pasteAsPlainText"),
        QStringLiteral("将剪贴板转为纯文本并粘贴"),
        [] {
            const QMimeData *mime = QApplication::clipboard()->mimeData();
            if (!mime)
                return;
            const QString text = mime->text();
            if (text.isEmpty())
                return;
            QApplication::clipboard()->setText(text);
            pasteClipboardAsKeyboardInput();
        },
    });
    m_commands.registerCommand({
        QStringLiteral("barcodeDecode"),
        QStringLiteral("识别图片条码"),
        [] {
            QImage image = QApplication::clipboard()->image();
            if (image.isNull())
                image = captureAllScreens();
            const auto result = snipaste::core::BarcodeService::recognize(image);
            QApplication::clipboard()->setText(result.success ? result.text : QString());
        },
    });
    m_commands.registerCommand({QStringLiteral("corners.disable"), QStringLiteral("禁用触发角"),
                                [this] { m_input.setCornersEnabled(false); }});
    m_commands.registerCommand({QStringLiteral("corners.enable"), QStringLiteral("启用触发角"),
                                [this] { m_input.setCornersEnabled(true); }});
    m_commands.registerCommand(
        {QStringLiteral("corners.toggle"), QStringLiteral("切换触发角"), [this] {
             m_input.setCornersEnabled(
                 !m_config.value(QStringLiteral("control/cornerEnabled"), false).toBool());
         }});
}

} // namespace snipaste::app
