#include "app/command_line.h"
#include "core/command/argument_parser.h"
#include <QCommandLineParser>
#include <QDateTime>

namespace snipaste::app
{

using snipaste::core::normalizeMultiValueOptions;

void configureCommandLine(QCommandLineParser &parser)
{
    parser.setApplicationDescription(QStringLiteral("开源截图、贴图和标注工具"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOptions({
        QCommandLineOption(QStringLiteral("snip"), QStringLiteral("启动区域截图")),
        QCommandLineOption(QStringLiteral("copy"), QStringLiteral("启动截图并自动复制")),
        QCommandLineOption(QStringLiteral("paste"), QStringLiteral("将剪贴板内容贴到屏幕")),
        QCommandLineOption(QStringLiteral("clipboard"), QStringLiteral("paste 的 --clipboard 别名")),
        QCommandLineOption(QStringLiteral("history"), QStringLiteral("打开截屏历史")),
        QCommandLineOption(QStringLiteral("full-screen"), QStringLiteral("捕获所有屏幕并复制")),
        QCommandLineOption(QStringLiteral("full"), QStringLiteral("snip 命令的全屏模式")),
        QCommandLineOption(QStringLiteral("last"), QStringLiteral("重复上次截图区域")),
        QCommandLineOption(QStringLiteral("active-window"), QStringLiteral("截取当前活动窗口")),
        QCommandLineOption(QStringLiteral("active-screen"), QStringLiteral("截取光标所在屏幕")),
        QCommandLineOption(QStringLiteral("custom"), QStringLiteral("打开自定义截图")),
        QCommandLineOption(QStringLiteral("hold"), QStringLiteral("截图确认后保留标注界面")),
        QCommandLineOption(QStringLiteral("delay"), QStringLiteral("截图前延迟秒数"),
                           QStringLiteral("seconds")),
        QCommandLineOption({QStringLiteral("o"), QStringLiteral("output")}, QStringLiteral("截图输出方式"),
                           QStringLiteral("output")),
        QCommandLineOption(QStringLiteral("block"), QStringLiteral("等待截图流程结束后退出"),
                           QStringLiteral("seconds")),
        QCommandLineOption(QStringLiteral("area"), QStringLiteral("选区 x,y,width,height"),
                           QStringLiteral("rect")),
        QCommandLineOption(QStringLiteral("size"), QStringLiteral("固定尺寸 width,height"),
                           QStringLiteral("size")),
        QCommandLineOption(QStringLiteral("pos"), QStringLiteral("贴图位置 x,y"), QStringLiteral("position")),
        QCommandLineOption(QStringLiteral("keep_aspect_ratio"), QStringLiteral("锁定选区宽高比")),
        QCommandLineOption(QStringLiteral("shadow"), QStringLiteral("截图包含阴影")),
        QCommandLineOption(QStringLiteral("no-shadow"), QStringLiteral("截图不含阴影")),
        QCommandLineOption(QStringLiteral("window"), QStringLiteral("选择窗口截图")),
        QCommandLineOption(QStringLiteral("element"), QStringLiteral("选择界面元素截图")),
        QCommandLineOption(QStringLiteral("show-images"), QStringLiteral("显示所有贴图")),
        QCommandLineOption(QStringLiteral("hide-images"), QStringLiteral("隐藏所有贴图")),
        QCommandLineOption(QStringLiteral("toggle-images"), QStringLiteral("切换所有贴图可见性")),
        QCommandLineOption(QStringLiteral("show-group-manager"), QStringLiteral("打开贴图组管理")),
        QCommandLineOption(QStringLiteral("empty-group"), QStringLiteral("清空当前贴图组")),
        QCommandLineOption(QStringLiteral("switch-group-prev"), QStringLiteral("切换到上一个贴图组")),
        QCommandLineOption(QStringLiteral("solo"), QStringLiteral("切换 Solo 模式")),
        QCommandLineOption(QStringLiteral("toggle-click-through"), QStringLiteral("切换鼠标穿透")),
        QCommandLineOption(QStringLiteral("no-click-through"), QStringLiteral("取消所有贴图鼠标穿透")),
        QCommandLineOption(QStringLiteral("disable-hotkeys"), QStringLiteral("禁用全局热键")),
        QCommandLineOption(QStringLiteral("enable-hotkeys"), QStringLiteral("启用全局热键")),
        QCommandLineOption(QStringLiteral("toggle-hotkeys"), QStringLiteral("切换全局热键")),
        QCommandLineOption(QStringLiteral("pick-color"), QStringLiteral("拾取光标所在颜色")),
        QCommandLineOption(QStringLiteral("barcode-decode"), QStringLiteral("识别图片中的二维码/条码"),
                           QStringLiteral("file")),
        QCommandLineOption(QStringLiteral("whiteboard"), QStringLiteral("打开白板")),
        QCommandLineOption(QStringLiteral("transparent-whiteboard"), QStringLiteral("打开透明白板")),
        QCommandLineOption(QStringLiteral("color"), QStringLiteral("白板颜色"), QStringLiteral("color")),
        QCommandLineOption(QStringLiteral("plain"), QStringLiteral("将剪贴板纯文本贴图")),
        QCommandLineOption(QStringLiteral("files"), QStringLiteral("将文件列表贴图")),
        QCommandLineOption(QStringLiteral("config"), QStringLiteral("指定配置文件路径"),
                           QStringLiteral("path")),
        QCommandLineOption(QStringLiteral("html"), QStringLiteral("将 HTML 贴出"), QStringLiteral("html")),
    });
    QCommandLineOption restartOption(QStringLiteral("snipaste-restart"), QStringLiteral("内部重启标记"));
    restartOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addPositionalArgument(QStringLiteral("command"),
                                 QStringLiteral("附加命令，如 create-group、switch-group、exit"));
    parser.addOption(restartOption);
}
qint64 commandBlockDeadline(const QStringList &arguments)
{
    QCommandLineParser parser;
    configureCommandLine(parser);
    parser.parse(QStringList{QStringLiteral("snipaste")} + normalizeMultiValueOptions(arguments));
    bool ok = false;
    const double seconds = parser.value(QStringLiteral("block")).toDouble(&ok);
    return ok && seconds > 0 ? QDateTime::currentMSecsSinceEpoch() + qRound64(seconds * 1000.0) : 0;
}

} // namespace snipaste::app
