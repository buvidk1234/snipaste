#include "app/application_controller.h"
#include "core/config/config_store.h"
#include "ui/paste/clipboard_content.h"
#include "ui/paste/paste_group_dialog.h"
#include "ui/paste/paste_window.h"
#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDebug>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QMimeData>
#include <QUrl>

namespace snipaste::app
{

using snipaste::ui::clipboardColorImage;
using snipaste::ui::clipboardHtmlImage;
using snipaste::ui::clipboardTextImage;
using snipaste::ui::parseClipboardColor;

void ApplicationController::registerPasteCommands()
{
    m_commands.registerCommand({
        QStringLiteral("paste.clipboard"),
        QStringLiteral("贴图"),
        [this] {
            // F3 during selection must output that live capture. It has not
            // reached the clipboard or history yet; reading either here
            // would paste an older image and leave the snip session open.
            if (m_capture.pasteSelection())
                return;
            QImage image = QApplication::clipboard()->image();
            QString sourceText;
            const QMimeData *mime = QApplication::clipboard()->mimeData();
            if (image.isNull())
            {
                if (mime && mime->hasUrls())
                {
                    QStringList paths;
                    for (const QUrl &url : mime->urls())
                        paths.append(url.toLocalFile().isEmpty() ? url.toString() : url.toLocalFile());
                    if (m_config.value(QStringLiteral("paste/pathToImage"), true).toBool())
                    {
                        for (const QString &path : paths)
                        {
                            if (QFileInfo::exists(path))
                            {
                                QImage candidate(path);
                                if (!candidate.isNull())
                                {
                                    image = std::move(candidate);
                                    break;
                                }
                            }
                        }
                    }
                    if (!image.isNull() && !paths.isEmpty())
                    {
                        const QString joined = paths.join(QChar::LineFeed);
                        if (joined == m_lastPastedFileClipboard)
                        {
                            sourceText = joined;
                            if (m_config.value(QStringLiteral("paste/pathToImage"), true).toBool())
                                image = clipboardTextImage(joined, &m_config);
                            m_lastPastedFileClipboard.clear();
                        }
                        else
                        {
                            m_lastPastedFileClipboard = joined;
                        }
                    }
                    else if (image.isNull() && !paths.isEmpty())
                    {
                        sourceText = paths.join(QChar::LineFeed);
                        if (m_config.value(QStringLiteral("paste/pathToImage"), true).toBool())
                            image = clipboardTextImage(sourceText, &m_config);
                    }
                }
                if (image.isNull() && mime && mime->hasHtml() &&
                    !m_config.value(QStringLiteral("paste/ignoreHtml"), false).toBool())
                {
                    image = clipboardHtmlImage(
                        mime->html(), m_config.value(QStringLiteral("paste/force_html_wrap"), true).toBool());
                }
                if (image.isNull() && mime && mime->hasText())
                {
                    const QString text = mime->text();
                    const QColor color = parseClipboardColor(text);
                    if (color.isValid() && m_config.value(QStringLiteral("paste/textToImage"), true).toBool())
                        image = clipboardColorImage(color);
                    else if (m_config.value(QStringLiteral("paste/textToImage"), true).toBool())
                    {
                        sourceText = text;
                        image = clipboardTextImage(text, &m_config);
                    }
                }
            }
            auto *window = m_pastes.pasteClipboard(image);
            if (window && !sourceText.isEmpty())
                window->setSourceText(sourceText);
            if (!window)
            {
                qInfo() << "clipboard does not contain an image or text";
            }
        },
    });
    m_commands.registerCommand({
        QStringLiteral("paste.toggleAll"),
        QStringLiteral("隐藏/显示所有贴图"),
        [this] { m_pastes.toggleAll(); },
    });
    m_commands.registerCommand({
        QStringLiteral("paste.showAll"),
        QStringLiteral("显示所有贴图"),
        [this] { m_pastes.showAll(); },
    });
    m_commands.registerCommand({
        QStringLiteral("paste.hideAll"),
        QStringLiteral("隐藏所有贴图"),
        [this] { m_pastes.hideAll(); },
    });
    m_commands.registerCommand({
        QStringLiteral("paste.switchGroup"),
        QStringLiteral("切换到另一贴图组"),
        [this] {
            m_pastes.switchGroup();
            qInfo() << "active paste group count:" << m_pastes.groupCount();
        },
    });
    m_commands.registerCommand({
        QStringLiteral("paste.switchGroupPrev"),
        QStringLiteral("切换到上一个贴图组"),
        [this] {
            const int count = m_pastes.groupCount();
            if (count > 1)
                m_pastes.switchGroup((m_pastes.activeGroup() + count - 1) % count);
        },
    });
    m_commands.registerCommand({
        QStringLiteral("paste.createGroup"),
        QStringLiteral("创建贴图组"),
        [this] {
            bool ok = false;
            const QString name =
                QInputDialog::getText(nullptr, QStringLiteral("创建贴图组"), QStringLiteral("组名:"),
                                      QLineEdit::Normal, QString(), &ok);
            if (ok)
                m_pastes.switchGroup(m_pastes.createGroup(name));
        },
    });
    m_commands.registerCommand({
        QStringLiteral("paste.emptyGroup"),
        QStringLiteral("清空当前贴图组"),
        [this] { m_pastes.emptyCurrentGroup(); },
    });
    m_commands.registerCommand({
        QStringLiteral("paste.solo"),
        QStringLiteral("切换 Solo 模式"),
        [this] { m_pastes.toggleSolo(); },
    });
    m_commands.registerCommand({
        QStringLiteral("paste.destroySelected"),
        QStringLiteral("销毁选中的贴图"),
        [this] { m_pastes.destroySelected(); },
    });
    m_commands.registerCommand({
        QStringLiteral("paste.toggleClickThrough"),
        QStringLiteral("切换鼠标穿透"),
        [this] { m_pastes.toggleClickThroughAtCursor(QCursor::pos()); },
    });
    m_commands.registerCommand({
        QStringLiteral("paste.noClickThrough"),
        QStringLiteral("取消所有贴图鼠标穿透"),
        [this] { m_pastes.disableClickThroughAll(); },
    });
    m_commands.registerCommand({QStringLiteral("paste.groupManager"), QStringLiteral("管理贴图组"),
                                [this] { ui::showPasteGroupDialog(m_pastes); }});
    m_commands.registerCommand({
        QStringLiteral("paste.destroyAll"),
        QStringLiteral("销毁所有贴图"),
        [this] { m_pastes.destroyAll(); },
    });
}

} // namespace snipaste::app
