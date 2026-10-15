#include "app/application_controller.h"
#include "platform/screen_capture.h"
#include "ui/clipboard_output.h"
#include "ui/snip/capture_support.h"
#include <QCursor>
#include <QInputDialog>
#include <QMenu>
#include <QScreen>

namespace snipaste::app
{

using snipaste::ui::captureAllScreens;
using snipaste::ui::ensureMacScreenCaptureAccess;

void ApplicationController::registerCaptureCommands()
{
    m_commands.registerCommand({
        QStringLiteral("snip.start"),
        QStringLiteral("开始截图"),
        [this] { m_capture.start({}); },
    });
    m_commands.registerCommand({
        QStringLiteral("snip.copy"),
        QStringLiteral("截屏并自动复制"),
        [this] { m_capture.start({.autoCopy = true}); },
    });
    m_commands.registerCommand({
        QStringLiteral("snip.fullScreen"),
        QStringLiteral("所有屏幕全屏并复制"),
        [this] {
            const QImage image = captureAllScreens();
            if (image.isNull())
                return;
            m_history.record(image, image);
            m_output.autoSave(image);
            snipaste::ui::copyImageToClipboard(image, false, snipaste::ui::ClipboardImageSource::Screenshot);
            m_pastes.createPaste(image);
        },
    });
    m_commands.registerCommand({
        QStringLiteral("snip.activeScreen"),
        QStringLiteral("截取当前屏幕"),
        [this] {
            if (!ensureMacScreenCaptureAccess(QStringLiteral("当前屏幕截图")))
                return;
            QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
            if (!screen)
                screen = QGuiApplication::primaryScreen();
            if (!screen)
                return;
            QImage image = snipaste::platform::captureScreen(screen);
            image.setDevicePixelRatio(1.0);
            if (image.isNull())
                return;
            m_history.record(image, image);
            m_output.autoSave(image);
            snipaste::ui::copyImageToClipboard(image, false, snipaste::ui::ClipboardImageSource::Screenshot);
            m_pastes.createPaste(image);
        },
    });
    m_commands.registerCommand({
        QStringLiteral("snip.window"),
        QStringLiteral("窗口截图"),
        [this] { m_capture.startWindow(false); },
    });
    m_commands.registerCommand({
        QStringLiteral("snip.element"),
        QStringLiteral("界面元素截图"),
        [this] { m_capture.startWindow(true); },
    });
    m_commands.registerCommand({
        QStringLiteral("snip.last"),
        QStringLiteral("重复上次截屏区域"),
        [this] { m_capture.start({.reuseLast = true}); },
    });
    m_commands.registerCommand({
        QStringLiteral("snip.custom"),
        QStringLiteral("自定义截屏"),
        [this] {
            QMenu menu;
            QAction *regionAction = menu.addAction(QStringLiteral("当前屏幕区域"));
            QAction *delayedAction = menu.addAction(QStringLiteral("延时 3 秒后截图"));
            QAction *fixedSizeAction = menu.addAction(QStringLiteral("固定尺寸区域"));
            QAction *fullScreenAction = menu.addAction(QStringLiteral("当前屏幕全屏并复制"));
            QAction *allScreensAction = menu.addAction(QStringLiteral("所有屏幕全屏并复制"));
            menu.addSeparator();
            QAction *lastRegionAction = menu.addAction(QStringLiteral("重复上次截屏区域"));
            QAction *windowAction = menu.addAction(QStringLiteral("窗口截图"));
            QAction *elementAction = menu.addAction(QStringLiteral("界面元素截图"));
            QAction *historyAction = menu.addAction(QStringLiteral("打开截屏历史"));
            QAction *selected = menu.exec(QCursor::pos());
            if (selected == regionAction)
            {
                m_capture.start({});
            }
            else if (selected == delayedAction)
            {
                m_capture.schedule({}, 3000);
            }
            else if (selected == fixedSizeAction)
            {
                bool widthOk = false;
                const int width =
                    QInputDialog::getInt(nullptr, QStringLiteral("固定尺寸截图"),
                                         QStringLiteral("宽度（像素）:"), 800, 16, 10000, 1, &widthOk);
                if (!widthOk)
                    return;
                bool heightOk = false;
                const int height =
                    QInputDialog::getInt(nullptr, QStringLiteral("固定尺寸截图"),
                                         QStringLiteral("高度（像素）:"), 600, 16, 10000, 1, &heightOk);
                if (heightOk)
                    m_capture.start({.fixedSize = QSize(width, height)});
            }
            else if (selected == fullScreenAction)
            {
                if (!ensureMacScreenCaptureAccess(QStringLiteral("当前屏幕截图")))
                    return;
                QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
                if (!screen)
                    screen = QGuiApplication::primaryScreen();
                if (!screen)
                    return;
                QImage image = snipaste::platform::captureScreen(screen);
                image.setDevicePixelRatio(1.0);
                m_pastes.rememberCapture(image, screen->geometry());
                m_history.record(image, image);
                m_output.autoSave(image);
                snipaste::ui::copyImageToClipboard(image, false,
                                                   snipaste::ui::ClipboardImageSource::Screenshot);
                m_pastes.createPaste(image);
            }
            else if (selected == allScreensAction)
            {
                m_commands.execute(QStringLiteral("snip.fullScreen"));
            }
            else if (selected == lastRegionAction)
            {
                m_commands.execute(QStringLiteral("snip.last"));
            }
            else if (selected == windowAction)
            {
                m_commands.execute(QStringLiteral("snip.window"));
            }
            else if (selected == elementAction)
            {
                m_commands.execute(QStringLiteral("snip.element"));
            }
            else if (selected == historyAction)
            {
                m_commands.execute(QStringLiteral("history.open"));
            }
        },
    });
}

} // namespace snipaste::app
