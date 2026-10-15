#include "app/application_controller.h"
#include "app/command_line.h"
#include "app/external_command.h"
#include "app/runtime_settings.h"
#include "core/command/argument_parser.h"
#include "core/recognition/barcode_service.h"
#include "ui/paste/clipboard_content.h"
#include "ui/snip/capture_support.h"
#include <QApplication>
#include <QClipboard>
#include <QCommandLineParser>
#include <QCursor>
#include <QDateTime>
#include <QDebug>
#include <QScreen>
#include <QTimer>

#include "ui/clipboard_output.h"
namespace snipaste::app
{
using core::normalizeMultiValueOptions;
using core::parsePointSpec;
using core::parseRectSpec;
using core::parseSizeSpec;
using core::splitOutputSpec;
using ui::captureAllScreens;
using ui::clipboardHtmlImage;
using ui::clipboardTextImage;
using ui::resolvePasteFileArguments;
bool ApplicationController::dispatchArguments(const QStringList &arguments, bool quitAfterFlow)
{
    if (arguments.isEmpty())
        return true;
    QCommandLineParser parser;
    configureCommandLine(parser);
    if (!parser.parse(QStringList{QStringLiteral("snipaste")} + normalizeMultiValueOptions(arguments)))
    {
        qWarning() << "invalid command invocation" << parser.errorText();
        return false;
    }
    const auto positional = parser.positionalArguments();
    const auto command = positional.value(0);
    const auto argument = positional.value(1);
    if (command == QStringLiteral("external"))
        startExternalCommand(positional.mid(1));
    const bool whiteboard =
        command == QStringLiteral("whiteboard") || command == QStringLiteral("transparent-whiteboard");
    if (whiteboard || parser.isSet(QStringLiteral("whiteboard")) ||
        parser.isSet(QStringLiteral("transparent-whiteboard")))
    {
        const auto color = parser.value(QStringLiteral("color"));
        openWhiteboard(parseColorSpec(color.isEmpty() && whiteboard ? argument : color),
                       command == QStringLiteral("transparent-whiteboard") ||
                           parser.isSet(QStringLiteral("transparent-whiteboard")),
                       parser.isSet(QStringLiteral("active-screen")));
    }
    dispatchCaptureArguments(parser, command);
    dispatchPasteArguments(parser, positional);
    struct Action
    {
        const char *option;
        const char *command;
    };
    const Action actions[] = {{"history", "history.open"},
                              {"window", "snip.window"},
                              {"element", "snip.element"},
                              {"show-images", "paste.showAll"},
                              {"hide-images", "paste.hideAll"},
                              {"toggle-images", "paste.toggleAll"},
                              {"show-group-manager", "paste.groupManager"},
                              {"empty-group", "paste.emptyGroup"},
                              {"switch-group-prev", "paste.switchGroupPrev"},
                              {"solo", "paste.solo"},
                              {"toggle-click-through", "paste.toggleClickThrough"},
                              {"no-click-through", "paste.noClickThrough"},
                              {"disable-hotkeys", "hotkeys.disable"},
                              {"enable-hotkeys", "hotkeys.enable"},
                              {"toggle-hotkeys", "hotkeys.toggle"},
                              {"pick-color", "pickColor"}};
    for (const auto &action : actions)
        if (parser.isSet(QString::fromLatin1(action.option)))
            m_commands.execute(QString::fromLatin1(action.command));
    if (parser.isSet(QStringLiteral("full-screen")) && command != QStringLiteral("snip") &&
        !parser.isSet(QStringLiteral("snip")) && !parser.isSet(QStringLiteral("copy")))
        m_commands.execute(QStringLiteral("snip.fullScreen"));
    const auto barcodeFile = parser.value(QStringLiteral("barcode-decode"));
    if (!barcodeFile.isEmpty())
    {
        const auto result = core::BarcodeService::recognize(QImage(barcodeFile));
        QApplication::clipboard()->setText(result.success ? result.text : QString());
    }
    if (parser.isSet(QStringLiteral("plain")) && command != QStringLiteral("paste") &&
        !parser.isSet(QStringLiteral("paste")) && !parser.isSet(QStringLiteral("clipboard")))
        m_commands.execute(QStringLiteral("pasteAsImage"));
    // Commands with parameters have dedicated handlers. Parameterless aliases
    // are resolved exclusively by CommandRegistry, including custom commands.
    if (command == QStringLiteral("create-group"))
        m_pastes.createGroup(argument);
    else if (command == QStringLiteral("switch-group"))
    {
        bool ok = false;
        const int index = argument.toInt(&ok);
        if (ok)
            m_pastes.switchGroup(index);
        else
        {
            const int named = m_pastes.groupNames().indexOf(argument);
            if (named >= 0)
                m_pastes.switchGroup(named);
            else
                m_pastes.switchGroup();
        }
    }
    else if (command == QStringLiteral("barcode-decode") && !argument.isEmpty())
    {
        const auto result = core::BarcodeService::recognize(QImage(argument));
        QApplication::clipboard()->setText(result.success ? result.text : QString());
    }
    else if (!command.isEmpty() && command != QStringLiteral("snip") && command != QStringLiteral("paste") &&
             command != QStringLiteral("external") && !whiteboard && m_commands.contains(command))
    {
        m_commands.execute(command);
    }
    if (quitAfterFlow && parser.isSet(QStringLiteral("block")))
    {
        const auto deadline = commandBlockDeadline(arguments);
        auto *poll = new QTimer(this);
        poll->setInterval(25);
        QObject::connect(poll, &QTimer::timeout, poll, [this, poll, deadline] {
            if (!m_capture.active() || (deadline > 0 && QDateTime::currentMSecsSinceEpoch() >= deadline))
            {
                poll->stop();
                poll->deleteLater();
                m_app.quit();
            }
        });
        poll->start();
    }
    return true;
}
void ApplicationController::dispatchCaptureArguments(const QCommandLineParser &parser,
                                                     const QString &positionalCommand)
{
    const auto commandLineCopy = parser.isSet(QStringLiteral("copy"));
    const auto commandLineSnip = parser.isSet(QStringLiteral("snip"));
    const auto commandLineFullScreen = parser.isSet(QStringLiteral("full-screen"));
    const auto commandLineOfficialFull = parser.isSet(QStringLiteral("full"));
    const auto commandLineOfficialLast = parser.isSet(QStringLiteral("last"));
    const auto commandLineActiveWindow = parser.isSet(QStringLiteral("active-window"));
    const auto commandLineActiveScreen = parser.isSet(QStringLiteral("active-screen"));
    const auto commandLineCustom = parser.isSet(QStringLiteral("custom"));
    const auto commandLineHold = parser.isSet(QStringLiteral("hold"));
    const auto commandLineShadow = parser.isSet(QStringLiteral("shadow"));
    const auto commandLineNoShadow = parser.isSet(QStringLiteral("no-shadow"));
    const auto commandLineKeepAspect = parser.isSet(QStringLiteral("keep_aspect_ratio"));
    const auto commandLineDelay = parser.value(QStringLiteral("delay"));
    const auto commandLineOutput = parser.value(QStringLiteral("output"));
    const auto commandLineArea = parseRectSpec(parser.value(QStringLiteral("area")));
    const auto commandLineSize = parseSizeSpec(parser.value(QStringLiteral("size")));
    const bool officialSnip =
        positionalCommand == QStringLiteral("snip") || commandLineSnip || commandLineCopy;

    const QStringList outputTokens = splitOutputSpec(commandLineOutput);
    const bool copyOutput = outputTokens.contains(QStringLiteral("clipboard"));
    const bool pinOutput = outputTokens.contains(QStringLiteral("pin"));
    const bool noAutoSave = outputTokens.contains(QStringLiteral("no-auto-save"));
    const bool automaticOutput = !commandLineOutput.isEmpty() && !commandLineHold;
    QString effectiveOutput = commandLineOutput;
    if (commandLineShadow && !effectiveOutput.contains(QStringLiteral("shadow")))
        effectiveOutput +=
            (effectiveOutput.isEmpty() ? QString() : QStringLiteral(";")) + QStringLiteral("shadow");
    if (commandLineNoShadow && !effectiveOutput.contains(QStringLiteral("no-shadow")))
        effectiveOutput +=
            (effectiveOutput.isEmpty() ? QString() : QStringLiteral(";")) + QStringLiteral("no-shadow");
    const auto captureWithOutputs = [&](const QImage &image) {
        if (image.isNull())
            return;
        m_history.record(image, image);
        if (!noAutoSave)
            m_output.autoSave(image);
        if (effectiveOutput.isEmpty())
            ui::copyImageToClipboard(image, false, ui::ClipboardImageSource::Screenshot);
        else
            m_output.execute(image, effectiveOutput);
    };
    if (officialSnip && (commandLineOfficialFull || commandLineFullScreen))
        captureWithOutputs(captureAllScreens());
    else if (officialSnip && commandLineOfficialLast)
        m_capture.start({.autoCopy = automaticOutput || copyOutput || pinOutput,
                         .reuseLast = true,
                         .fixedSize = commandLineSize,
                         .selection = commandLineArea,
                         .output = effectiveOutput,
                         .hold = commandLineHold,
                         .noAutoSave = noAutoSave,
                         .keepAspect = commandLineKeepAspect});
    else if (officialSnip && commandLineActiveWindow)
        m_capture.startWindow(false, effectiveOutput, noAutoSave);
    else if (officialSnip && commandLineActiveScreen)
        [&] {
            QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
            if (!screen)
                screen = QGuiApplication::primaryScreen();
            if (screen)
                m_capture.start({.autoCopy = automaticOutput || copyOutput || pinOutput,
                                 .fixedSize = commandLineSize,
                                 .selection = screen->geometry(),
                                 .output = effectiveOutput,
                                 .hold = commandLineHold,
                                 .noAutoSave = noAutoSave,
                                 .keepAspect = commandLineKeepAspect});
        }();
    else if (officialSnip && commandLineCustom)
        m_commands.execute(QStringLiteral("snip.custom"));
    else if (officialSnip && (commandLineHold || !commandLineDelay.isEmpty() || !effectiveOutput.isEmpty() ||
                              commandLineArea.isValid() || commandLineSize.isValid()))
    {
        bool delayOk = false;
        const int delayMs = qMax(0, qRound(commandLineDelay.toDouble(&delayOk) * 1000.0));
        m_capture.schedule({.autoCopy = automaticOutput || copyOutput || pinOutput,
                            .fixedSize = commandLineSize,
                            .selection = commandLineArea,
                            .output = effectiveOutput,
                            .hold = commandLineHold,
                            .noAutoSave = noAutoSave,
                            .keepAspect = commandLineKeepAspect},
                           delayOk ? delayMs : 0);
    }
    else if (commandLineCopy)
        m_commands.execute(QStringLiteral("snip.copy"));
    else if (officialSnip)
        m_commands.execute(QStringLiteral("snip.start"));
}
void ApplicationController::dispatchPasteArguments(const QCommandLineParser &parser,
                                                   const QStringList &positionalArguments)
{
    const auto commandLinePaste =
        parser.isSet(QStringLiteral("paste")) || parser.isSet(QStringLiteral("clipboard"));
    const auto commandLinePlain = parser.isSet(QStringLiteral("plain"));
    const auto commandLineFiles = parser.isSet(QStringLiteral("files"));
    const auto commandLineHtml = parser.isSet(QStringLiteral("html"));
    const auto commandLinePosition = parsePointSpec(parser.value(QStringLiteral("pos")));
    const bool positionalPaste = positionalArguments.value(0) == QStringLiteral("paste");
    const bool officialPaste = positionalPaste || commandLinePaste;
    const auto pasteArguments = positionalPaste ? positionalArguments.mid(1) : positionalArguments;
    if (officialPaste && commandLinePlain)
    {
        const QString text = pasteArguments.join(QChar(' '));
        if (!text.isEmpty())
        {
            const QImage image = clipboardTextImage(text, &m_config);
            if (!commandLinePosition.isNull())
                m_pastes.createPasteAtPosition(image, commandLinePosition);
            else
                m_pastes.createPaste(image);
        }
    }
    else if (officialPaste && commandLineHtml)
    {
        const QString html = parser.value(QStringLiteral("html")).isEmpty()
                                 ? pasteArguments.join(QChar(' '))
                                 : parser.value(QStringLiteral("html"));
        if (!html.isEmpty())
        {
            const QImage image =
                clipboardHtmlImage(html, m_config
                                             .value(QStringLiteral("paste/force_html_wrap"),
                                                    m_config.value(QStringLiteral("force_html_wrap"), true))
                                             .toBool());
            if (!commandLinePosition.isNull())
                m_pastes.createPasteAtPosition(image, commandLinePosition);
            else
                m_pastes.createPaste(image);
        }
    }
    else if (commandLineFiles)
    {
        for (const QString &fileArgument : resolvePasteFileArguments(pasteArguments))
        {
            const QImage image(fileArgument);
            if (!image.isNull())
            {
                if (!commandLinePosition.isNull())
                    m_pastes.createPasteAtPosition(image, commandLinePosition);
                else
                    m_pastes.createPaste(image);
            }
        }
    }
    else if (officialPaste)
    {
        if (!commandLinePosition.isNull())
        {
            const QImage image = QApplication::clipboard()->image();
            if (!image.isNull())
                m_pastes.createPasteAtPosition(image, commandLinePosition);
            else
                m_commands.execute(QStringLiteral("paste.clipboard"));
        }
        else
        {
            m_commands.execute(QStringLiteral("paste.clipboard"));
        }
    }
}
} // namespace snipaste::app
