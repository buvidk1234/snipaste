#include "app/application_controller.h"
#include "core/ai/ai_client.h"
#include "core/config/config_store.h"
#include "platform/windows/single_instance.h"
#include "ui/ai/ai_dialog.h"
#include "ui/annotation/annotation_canvas.h"
#include "ui/preferences/preferences_dialog.h"
#include "ui/snip/snip_overlay.h"

#include <QApplication>
#include <QAction>
#include <QBuffer>
#include <QClipboard>
#include <QDir>
#include <QFontDatabase>
#include <QFrame>
#include <QJsonDocument>
#include <QJsonObject>
#include <QInputMethodEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

using namespace snipaste;

namespace
{
QImage sourceImage()
{
    QImage image(120, 70, QImage::Format_RGB32);
    image.fill(QColor(245, 246, 248));
    image.setPixelColor(17, 24, Qt::red);
    return image;
}

QByteArray answer(const QString &text)
{
    const QJsonObject reasoning{{QStringLiteral("type"), QStringLiteral("reasoning")},
                                {QStringLiteral("encrypted_content"), QStringLiteral("test-reasoning")},
                                {QStringLiteral("summary"), QJsonArray{}}};
    const QJsonObject content{{QStringLiteral("type"), QStringLiteral("output_text")},
                              {QStringLiteral("text"), text}, {QStringLiteral("annotations"), QJsonArray{}}};
    const QJsonObject message{{QStringLiteral("type"), QStringLiteral("message")},
                              {QStringLiteral("role"), QStringLiteral("assistant")},
                              {QStringLiteral("content"), QJsonArray{content}}};
    return QJsonDocument(QJsonObject{{QStringLiteral("status"), QStringLiteral("completed")},
                                     {QStringLiteral("output"), QJsonArray{reasoning, message}}})
        .toJson(QJsonDocument::Compact);
}

QByteArray imageAnswer(const QList<QImage> &images, const QString &text = {})
{
    QJsonArray output = text.isEmpty() ? QJsonArray{} : QJsonDocument::fromJson(answer(text)).object()
                                                       .value(QStringLiteral("output")).toArray();
    for (const QImage &image : images)
    {
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        output.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("image_generation_call")},
                                  {QStringLiteral("id"), QStringLiteral("ig_%1").arg(output.size())},
                                  {QStringLiteral("status"), QStringLiteral("completed")},
                                  {QStringLiteral("result"), QString::fromLatin1(png.toBase64())}});
    }
    return QJsonDocument(QJsonObject{{QStringLiteral("status"), QStringLiteral("completed")},
                                     {QStringLiteral("output"), output}}).toJson(QJsonDocument::Compact);
}

class ResponsesServer final : public QTcpServer
{
  public:
    QList<QJsonObject> requests;
    QList<QByteArray> headers;
    QByteArray response = answer(QStringLiteral("这是一张截图。"));
    int status = 200;
    bool respond = true;

    ResponsesServer()
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections())
            {
                auto *socket = nextPendingConnection();
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    QByteArray buffer = socket->property("buffer").toByteArray() + socket->readAll();
                    socket->setProperty("buffer", buffer);
                    const auto end = buffer.indexOf("\r\n\r\n");
                    if (end < 0 || socket->property("handled").toBool())
                        return;
                    const QByteArray header = buffer.left(end);
                    qint64 length = 0;
                    for (const auto &line : header.split('\n'))
                        if (line.toLower().startsWith("content-length:"))
                            length = line.mid(line.indexOf(':') + 1).trimmed().toLongLong();
                    if (buffer.size() < end + 4 + length)
                        return;
                    socket->setProperty("handled", true);
                    headers.append(header);
                    requests.append(QJsonDocument::fromJson(buffer.mid(end + 4, length)).object());
                    if (respond)
                    {
                        socket->write("HTTP/1.1 " + QByteArray::number(status) + " Test\r\n"
                                      "Content-Type: application/json\r\nContent-Length: " +
                                      QByteArray::number(response.size()) + "\r\nConnection: close\r\n\r\n" + response);
                        socket->disconnectFromHost();
                    }
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
        const bool listening = listen(QHostAddress::LocalHost);
        Q_ASSERT(listening);
        Q_UNUSED(listening);
    }

    core::AiSettings settings() const
    {
        return {QStringLiteral("test-key"), QStringLiteral("http://127.0.0.1:%1/v1/").arg(serverPort()),
                QStringLiteral("test-vision-model")};
    }
};

void saveArtifact(QWidget &widget, const QString &name)
{
    const QString directory = qEnvironmentVariable("SNIPASTE_TEST_ARTIFACTS");
    if (directory.isEmpty())
        return;
    QDir().mkpath(directory);
    QVERIFY(widget.grab().save(QDir(directory).filePath(name)));
}
} // namespace

class AiTest final : public QObject
{
    Q_OBJECT
  private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
#ifdef Q_OS_WIN
        const int font = QFontDatabase::addApplicationFont(
            QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("Fonts/msyh.ttc")));
        if (font >= 0)
            QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(font).first(), 9));
#endif
    }

    void responsesCarryScreenshotAndConversation()
    {
        ResponsesServer server;
        core::AiClient client(server.settings(), sourceImage());
        QSignalSpy answers(&client, &core::AiClient::answered);
        QSignalSpy errors(&client, &core::AiClient::failed);
        client.send(QStringLiteral("解释这张截图"));
        QVERIFY(client.isBusy());
        client.send(QStringLiteral("重复点击"));
        QTRY_COMPARE(answers.size(), 1);
        QVERIFY(errors.isEmpty());
        QCOMPARE(server.requests.size(), 1);
        QVERIFY(server.headers.first().startsWith("POST /v1/responses HTTP/1.1"));
        QVERIFY(server.headers.first().contains("Authorization: Bearer test-key"));
        const auto first = server.requests.first();
        QCOMPARE(first.value(QStringLiteral("model")).toString(), QStringLiteral("test-vision-model"));
        QCOMPARE(first.value(QStringLiteral("store")).toBool(true), false);
        QCOMPARE(first.value(QStringLiteral("tools")).toArray(),
                 (QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("image_generation")}}}));
        const auto input = first.value(QStringLiteral("input")).toArray();
        QCOMPARE(input.size(), 1);
        const auto content = input.first().toObject().value(QStringLiteral("content")).toArray();
        QCOMPARE(content.first().toObject().value(QStringLiteral("text")).toString(), QStringLiteral("解释这张截图"));
        const auto imagePart = content.at(1).toObject();
        QCOMPARE(imagePart.value(QStringLiteral("type")).toString(), QStringLiteral("input_image"));
        const QString data = imagePart.value(QStringLiteral("image_url")).toString();
        QVERIFY(data.startsWith(QStringLiteral("data:image/png;base64,")));
        const auto image = QImage::fromData(QByteArray::fromBase64(data.section(QLatin1Char(','), 1).toLatin1()));
        QCOMPARE(image.convertToFormat(QImage::Format_RGB32), sourceImage());

        client.send(QStringLiteral("再解释一下红点"));
        QTRY_COMPARE(answers.size(), 2);
        const auto followup = server.requests.last().value(QStringLiteral("input")).toArray();
        QCOMPARE(followup.size(), 4);
        QCOMPARE(followup.first(), input.first());
        QCOMPARE(followup.at(1).toObject().value(QStringLiteral("encrypted_content")).toString(), QStringLiteral("test-reasoning"));
        QCOMPARE(followup.at(2).toObject().value(QStringLiteral("role")).toString(), QStringLiteral("assistant"));
        QCOMPARE(followup.last().toObject().value(QStringLiteral("content")).toArray().size(), 1);
        QCOMPARE(answers.last().first().toString(), QStringLiteral("这是一张截图。"));
    }

    void receivesImagesAndRetainsThemInConversation()
    {
        ResponsesServer server;
        QImage second(96, 150, QImage::Format_RGB32);
        second.fill(QColor("#6979bd"));
        server.response = imageAnswer({sourceImage(), second});
        core::AiClient client(server.settings(), sourceImage());
        QSignalSpy answers(&client, &core::AiClient::answered);
        QSignalSpy errors(&client, &core::AiClient::failed);
        client.send(QStringLiteral("参照截图生成两张图片"));
        QTRY_VERIFY(!client.isBusy());
        QVERIFY(errors.isEmpty());
        QCOMPARE(answers.size(), 1);
        QVERIFY(answers.first().first().toString().isEmpty());
        QCOMPARE(qvariant_cast<QList<QImage>>(answers.first().at(1)), QList<QImage>({sourceImage(), second}));
        server.response = imageAnswer({second}, QStringLiteral("已改成蓝色。"));
        client.send(QStringLiteral("把第二张改成蓝色"));
        QTRY_COMPARE(answers.size(), 2);
        QCOMPARE(answers.last().first().toString(), QStringLiteral("已改成蓝色。"));
        QCOMPARE(qvariant_cast<QList<QImage>>(answers.last().at(1)), QList<QImage>{second});
        const auto followup = server.requests.last().value(QStringLiteral("input")).toArray();
        const auto previousOutput = QJsonDocument::fromJson(imageAnswer({sourceImage(), second})).object()
                                        .value(QStringLiteral("output")).toArray();
        QCOMPARE(followup.size(), 4);
        QCOMPARE(followup.at(1), previousOutput.first());
        QCOMPARE(followup.at(2), previousOutput.last());
    }

    void failuresAllowCleanRetry_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<QByteArray>("body");
        QTest::newRow("unsupported-tool") << 400 << QByteArray("{}");
        QTest::newRow("authentication") << 401 << QByteArray("{}");
        QTest::newRow("quota") << 429 << QByteArray("{}");
        QTest::newRow("unsupported") << 404 << QByteArray("{}");
        QTest::newRow("server") << 500 << QByteArray("{}");
        QTest::newRow("malformed") << 200 << QByteArray("invalid");
        QTest::newRow("incomplete") << 200 << QByteArray("{\"status\":\"incomplete\"}");
        QTest::newRow("empty") << 200 << QByteArray("{\"status\":\"completed\",\"output\":[]}");
        QTest::newRow("api-error") << 200 << QByteArray("{\"error\":{\"message\":\"test-key\"}}");
        QTest::newRow("invalid-image") << 200 << QByteArray(
            "{\"output\":[{\"type\":\"image_generation_call\",\"status\":\"completed\",\"result\":\"broken!\"}]}");
        QTest::newRow("failed-image") << 200 << QByteArray(
            "{\"output\":[{\"type\":\"image_generation_call\",\"status\":\"failed\",\"result\":null}]}");
    }

    void failuresAllowCleanRetry()
    {
        QFETCH(int, status);
        QFETCH(QByteArray, body);
        ResponsesServer server;
        server.status = status;
        server.response = body;
        core::AiClient client(server.settings(), sourceImage());
        QSignalSpy errors(&client, &core::AiClient::failed);
        QSignalSpy answers(&client, &core::AiClient::answered);
        client.send(QStringLiteral("第一次"));
        QTRY_COMPARE(errors.size(), 1);
        QVERIFY(!errors.first().first().toString().contains(QStringLiteral("test-key")));
        QVERIFY(!client.isBusy());
        QVERIFY(answers.isEmpty());
        server.status = 200;
        server.response = answer(QStringLiteral("成功"));
        client.send(QStringLiteral("重试"));
        QTRY_COMPARE(answers.size(), 1);
        const auto input = server.requests.last().value(QStringLiteral("input")).toArray();
        QCOMPARE(input.size(), 1);
        QCOMPARE(input.first().toObject().value(QStringLiteral("content")).toArray().size(), 2);
    }

    void validatesConfigurationAndCancels()
    {
        ResponsesServer server;
        auto settings = server.settings();
        settings.apiKey.clear();
        core::AiClient invalid(settings, sourceImage());
        QSignalSpy errors(&invalid, &core::AiClient::failed);
        invalid.send(QStringLiteral("测试"));
        QCOMPARE(errors.size(), 1);
        QVERIFY(server.requests.isEmpty());

        settings = server.settings();
        settings.baseUrl = QStringLiteral("file:///tmp/responses");
        core::AiClient badUrl(settings, sourceImage());
        QSignalSpy urlErrors(&badUrl, &core::AiClient::failed);
        badUrl.send(QStringLiteral("测试"));
        QCOMPARE(urlErrors.size(), 1);

        server.respond = false;
        core::AiClient client(server.settings(), sourceImage());
        QSignalSpy answers(&client, &core::AiClient::answered);
        QSignalSpy failures(&client, &core::AiClient::failed);
        client.send(QStringLiteral("取消这次请求"));
        QTRY_COMPARE(server.requests.size(), 1);
        client.cancel();
        QVERIFY(!client.isBusy());
        QTest::qWait(20);
        QVERIFY(answers.isEmpty());
        QVERIFY(failures.isEmpty());
        server.respond = true;
        client.send(QStringLiteral("重新发送"));
        QTRY_COMPARE(answers.size(), 1);
        QCOMPARE(server.requests.last().value(QStringLiteral("input")).toArray().size(), 1);
    }

    void preferencesPersistAndMaskKey()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("config.ini"));
        core::ConfigStore config(path);
        ui::PreferencesDialog dialog(config);
        auto *tabs = dialog.findChild<QTabWidget *>(QStringLiteral("preferencesTabs"));
        const int index = tabs->indexOf(dialog.findChild<QLineEdit *>(QStringLiteral("aiApiKey"))->parentWidget());
        QVERIFY(index >= 0);
        QCOMPARE(tabs->tabText(index), QStringLiteral("AI"));
        tabs->setCurrentIndex(index);
        auto *key = dialog.findChild<QLineEdit *>(QStringLiteral("aiApiKey"));
        auto *base = dialog.findChild<QLineEdit *>(QStringLiteral("aiBaseUrl"));
        auto *model = dialog.findChild<QLineEdit *>(QStringLiteral("aiModel"));
        QCOMPARE(key->echoMode(), QLineEdit::Password);
        key->setText(QStringLiteral("  test-key  "));
        base->setText(QStringLiteral("  https://example.com/v1/  "));
        model->setText(QStringLiteral(" vision-model "));
        dialog.show();
        QTest::qWait(30);
        saveArtifact(dialog, QStringLiteral("preferences-ai.png"));
        dialog.close();
        core::ConfigStore reloaded(path);
        QVERIFY(reloaded.load());
        const auto saved = core::AiSettings::fromConfig(reloaded);
        QCOMPARE(saved.apiKey, QStringLiteral("test-key"));
        QCOMPARE(saved.baseUrl, QStringLiteral("https://example.com/v1/"));
        QCOMPARE(saved.model, QStringLiteral("vision-model"));
    }

    void dialogRetainsQuestionOnFailureAndStopsOnClose()
    {
        ResponsesServer server;
        server.status = 401;
        ui::AiDialog dialog(sourceImage(), server.settings());
        auto *question = dialog.findChild<QPlainTextEdit *>(QStringLiteral("aiQuestion"));
        auto *send = dialog.findChild<QPushButton *>(QStringLiteral("aiSend"));
        auto *status = dialog.findChild<QLabel *>(QStringLiteral("aiStatus"));
        dialog.show();
        question->setPlainText(QStringLiteral("截图是什么意思？"));
        QTest::mouseClick(send, Qt::LeftButton);
        QTRY_VERIFY(question->isEnabled());
        QVERIFY(dialog.findChild<QLabel *>(QStringLiteral("aiAnswer"))->text().contains(QStringLiteral("认证失败")));
        QVERIFY(!question->toPlainText().isEmpty());
        QCOMPARE(dialog.findChildren<QWidget *>(QStringLiteral("aiTurn")).size(), 1);
        server.status = 200;
        QTest::mouseClick(send, Qt::LeftButton);
        QTRY_COMPARE(dialog.findChild<QLabel *>(QStringLiteral("aiAnswer"))->text(), QStringLiteral("这是一张截图。"));
        QVERIFY(question->toPlainText().isEmpty());
        QCOMPARE(dialog.findChildren<QWidget *>(QStringLiteral("aiTurn")).size(), 1);
        server.response = answer(QStringLiteral("红色标记是截图中需要注意的位置。"));
        question->setPlainText(QStringLiteral("红点代表什么？"));
        QTest::keyClick(question, Qt::Key_Return, Qt::ShiftModifier);
        QVERIFY(question->toPlainText().contains(QLatin1Char('\n')));
        QCOMPARE(server.requests.size(), 2);
        QTest::keyClick(question, Qt::Key_Return);
        QTRY_COMPARE(dialog.findChildren<QLabel *>(QStringLiteral("aiAnswer")).size(), 2);
        QTRY_COMPARE(dialog.findChildren<QLabel *>(QStringLiteral("aiAnswer")).last()->text(),
                     QStringLiteral("红色标记是截图中需要注意的位置。"));
        saveArtifact(dialog, QStringLiteral("ai-dialog.png"));
        server.respond = false;
        question->setPlainText(QStringLiteral("继续"));
        QTest::mouseClick(send, Qt::LeftButton);
        QTRY_COMPARE(server.requests.size(), 4);
        QVERIFY(!question->isEnabled());
        dialog.close();
        QTest::qWait(20);
        QVERIFY(!status->text().contains(QStringLiteral("失败")));
    }

    void dialogDisplaysImagesAndCopiesOriginalPixels()
    {
        ResponsesServer server;
        QImage image(1200, 800, QImage::Format_RGB32);
        image.fill(QColor("#6979bd"));
        image.setPixelColor(10, 20, Qt::red);
        server.response = imageAnswer({image}, QStringLiteral("已根据截图生成图片。"));
        ui::AiDialog dialog(sourceImage(), server.settings());
        auto *question = dialog.findChild<QPlainTextEdit *>(QStringLiteral("aiQuestion"));
        dialog.show();
        question->setPlainText(QStringLiteral("参考这张截图，生成蓝色版本"));
        QTest::keyClick(question, Qt::Key_Return);
        QTRY_VERIFY(question->isEnabled());
        auto *preview = dialog.findChild<QLabel *>(QStringLiteral("aiGeneratedImage"));
        QVERIFY(preview);
        QTRY_VERIFY(preview->isVisible());
        QVERIFY(!preview->pixmap().isNull());
        QVERIFY(preview->width() <= dialog.width() - 32);
        QVERIFY(preview->height() <= 280);
        QCOMPARE(dialog.findChild<QLabel *>(QStringLiteral("aiAnswer"))->text(), QStringLiteral("已根据截图生成图片。"));
        auto *copy = preview->findChild<QAction *>(QStringLiteral("aiCopyImage"));
        QVERIFY(copy);
        copy->trigger();
        QCOMPARE(QApplication::clipboard()->image().convertToFormat(QImage::Format_RGB32), image);
        auto *history = dialog.findChild<QScrollArea *>(QStringLiteral("aiHistory"));
        QTRY_VERIFY(history->verticalScrollBar()->maximum() > 0);
        QTRY_COMPARE(history->verticalScrollBar()->value(), history->verticalScrollBar()->maximum());
        saveArtifact(dialog, QStringLiteral("ai-image-answer.png"));
        server.response = imageAnswer({image});
        question->setPlainText(QStringLiteral("继续修改"));
        QTest::keyClick(question, Qt::Key_Return);
        QTRY_VERIFY(question->isEnabled());
        QCOMPARE(dialog.findChildren<QLabel *>(QStringLiteral("aiGeneratedImage")).size(), 2);
        QVERIFY(dialog.findChildren<QLabel *>(QStringLiteral("aiAnswer")).last()->isHidden());
    }

    void inputMethodPreeditDoesNotOverlapPlaceholder()
    {
        ResponsesServer server;
        ui::AiDialog dialog(sourceImage(), server.settings());
        auto *question = dialog.findChild<QPlainTextEdit *>(QStringLiteral("aiQuestion"));
        dialog.show();
        question->setFocus();
        QTest::qWait(20);
        const QString placeholder = question->placeholderText();
        QVERIFY(!placeholder.isEmpty());
        const auto emptyInput = question->viewport()->grab().toImage();
        QInputMethodEvent composing(QStringLiteral("zhe'shi'shen'me"), {});
        QApplication::sendEvent(question, &composing);
        QVERIFY(question->toPlainText().isEmpty());
        const auto withPlaceholder = question->viewport()->grab().toImage();
        saveArtifact(dialog, QStringLiteral("ai-ime.png"));
        question->setPlaceholderText(QString());
        const auto withoutPlaceholder = question->viewport()->grab().toImage();
        QCOMPARE(withPlaceholder, withoutPlaceholder);
        QInputMethodEvent cancelled;
        QApplication::sendEvent(question, &cancelled);
        QCOMPARE(question->placeholderText(), placeholder);
        QCOMPARE(question->viewport()->grab().toImage(), emptyInput);
        QApplication::sendEvent(question, &composing);
        QInputMethodEvent committed;
        committed.setCommitString(QStringLiteral("这是什么"));
        QApplication::sendEvent(question, &committed);
        QCOMPARE(question->toPlainText(), QStringLiteral("这是什么"));
        question->clear();
        QCOMPARE(question->viewport()->grab().toImage(), emptyInput);
    }

    void enterDuringInputMethodCompositionDoesNotSend()
    {
        ResponsesServer server;
        ui::AiDialog dialog(sourceImage(), server.settings());
        auto *question = dialog.findChild<QPlainTextEdit *>(QStringLiteral("aiQuestion"));
        dialog.show();
        question->setPlainText(QStringLiteral("这张截图"));
        QInputMethodEvent composing(QStringLiteral("shi'shen'me"), {});
        QApplication::sendEvent(question, &composing);
        QTest::keyClick(question, Qt::Key_Return);
        QVERIFY(question->isEnabled());
        QVERIFY(dialog.findChildren<QWidget *>(QStringLiteral("aiTurn")).isEmpty());
        QVERIFY(server.requests.isEmpty());
        QInputMethodEvent committed;
        committed.setCommitString(QStringLiteral("是什么？"));
        QApplication::sendEvent(question, &committed);
        QTest::keyClick(question, Qt::Key_Return);
        QTRY_COMPARE(server.requests.size(), 1);
        QVERIFY(dialog.findChild<QLabel *>(QStringLiteral("aiUserMessage"))->text().contains(QStringLiteral("是什么？")));
    }

    void toolbarOpensFloatingConversationAndPreservesSelection()
    {
        ResponsesServer server;
        QTemporaryDir directory;
        core::ConfigStore config(directory.filePath(QStringLiteral("config.ini")));
        const auto settings = server.settings();
        config.setValue(QStringLiteral("ai/apiKey"), settings.apiKey);
        config.setValue(QStringLiteral("ai/baseUrl"), settings.baseUrl);
        config.setValue(QStringLiteral("ai/model"), settings.model);
        config.setValue(QStringLiteral("general/history_dir"), directory.filePath(QStringLiteral("history")));
        config.setValue(QStringLiteral("capture/autoSave"), false);
        config.setValue(QStringLiteral("capture/autoDetectWindow"), false);
        config.setValue(QStringLiteral("capture/autoDetectElements"), false);
        config.setValue(QStringLiteral("capture/soundEnabled"), false);
        config.setValue(QStringLiteral("update/checkAtStartup"), false);
        for (const char *name : {"snip", "copy", "paste", "custom", "toggleImages", "switchGroup", "clickThrough"})
            config.setValue(QStringLiteral("hotkeys/") + QString::fromLatin1(name), QString());
        platform::SingleInstance instance;
        app::ApplicationController controller(*static_cast<QApplication *>(qApp), config, instance);
        QVERIFY(controller.dispatchArguments({QStringLiteral("snip")}, false));
        ui::SnipOverlay *overlay = nullptr;
        for (auto *widget : QApplication::topLevelWidgets())
            if (auto *candidate = qobject_cast<ui::SnipOverlay *>(widget); candidate && candidate->isVisible())
                overlay = candidate;
        QVERIFY(overlay);
        QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, QPoint(30, 30));
        QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, QPoint(270, 170));
        auto *canvas = overlay->findChild<ui::AnnotationCanvas *>();
        QVERIFY(canvas);
        canvas->setTool(ui::AnnotationCanvas::Tool::Rectangle);
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(15, 15));
        QTest::mouseMove(canvas, QPoint(90, 65));
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(90, 65));
        QVERIFY(canvas->canUndo());
        const auto expected = canvas->renderedImage();
        QSignalSpy images(overlay, &ui::SnipOverlay::aiRequested);
        QSignalSpy outputs(overlay, &ui::SnipOverlay::screenshotCaptured);
        const QRect selection = overlay->captureGeometry().region;
        auto *button = overlay->findChild<QToolButton *>(QStringLiteral("AI"));
        QVERIFY(button && !button->icon().isNull());
        saveArtifact(*overlay, QStringLiteral("ai-toolbar.png"));
        QTest::mouseClick(button, Qt::LeftButton);
        QCOMPARE(images.size(), 1);
        QCOMPARE(qvariant_cast<QImage>(images.first().first()), expected);
        QVERIFY(overlay->isVisible());
        QPointer<ui::AiDialog> dialog = overlay->findChild<ui::AiDialog *>();
        QVERIFY(dialog && dialog->isVisible());
        QVERIFY(!dialog->isWindow());
        QVERIFY(overlay->isAncestorOf(dialog));
        QTRY_COMPARE(QApplication::focusWidget(), dialog->findChild<QPlainTextEdit *>(QStringLiteral("aiQuestion")));
        auto *toolbar = overlay->findChild<QFrame *>(QStringLiteral("captureToolbar"));
        QVERIFY(!toolbar->isVisible());
        auto *layer = dialog->parentWidget();
        QVERIFY(layer->rect().contains(dialog->geometry()));
        QTest::mouseClick(layer, Qt::RightButton, Qt::NoModifier, QPoint(5, 5));
        QCOMPARE(overlay->captureGeometry().region, selection);
        QVERIFY(server.requests.isEmpty());
        dialog->findChild<QPlainTextEdit *>(QStringLiteral("aiQuestion"))->setPlainText(QStringLiteral("分析这张图"));
        QTest::mouseClick(dialog->findChild<QPushButton *>(QStringLiteral("aiSend")), Qt::LeftButton);
        QTRY_COMPARE(dialog->findChild<QLabel *>(QStringLiteral("aiAnswer"))->text(), QStringLiteral("这是一张截图。"));
        QVERIFY(outputs.isEmpty());
        saveArtifact(*overlay, QStringLiteral("ai-floating.png"));
        const auto parts = server.requests.first().value(QStringLiteral("input")).toArray().first().toObject()
                               .value(QStringLiteral("content")).toArray();
        const QString data = parts.at(1).toObject().value(QStringLiteral("image_url")).toString();
        const auto uploaded = QImage::fromData(QByteArray::fromBase64(data.section(QLatin1Char(','), 1).toLatin1()));
        QCOMPARE(uploaded.convertToFormat(QImage::Format_ARGB32), expected.convertToFormat(QImage::Format_ARGB32));
        QTest::keyClick(dialog->findChild<QPlainTextEdit *>(QStringLiteral("aiQuestion")), Qt::Key_Escape);
        QTRY_VERIFY(dialog.isNull());
        QVERIFY(overlay->isVisible());
        QVERIFY(toolbar->isVisible());
        QCOMPARE(overlay->captureGeometry().region, selection);
        QCOMPARE(canvas->renderedImage(), expected);
        QVERIFY(canvas->canUndo());
        QTest::mouseClick(button, Qt::LeftButton);
        dialog = overlay->findChild<ui::AiDialog *>();
        QVERIFY(dialog && dialog->isVisible());
        server.respond = false;
        auto *question = dialog->findChild<QPlainTextEdit *>(QStringLiteral("aiQuestion"));
        question->setPlainText(QStringLiteral("关闭时取消"));
        QTest::keyClick(question, Qt::Key_Return);
        QTRY_COMPARE(server.requests.size(), 2);
        overlay->close();
        QVERIFY(dialog.isNull());
    }

    void floatingPlacementAndHeaderDrag()
    {
        ResponsesServer server;
        QWidget parent;
        parent.resize(1000, 800);
        ui::AiDialog dialog(sourceImage(), server.settings(), &parent);
        const QRect selection(80, 50, 300, 120);
        dialog.placeNear(selection, parent.rect());
        QVERIFY(parent.rect().contains(dialog.geometry()));
        QVERIFY(dialog.y() > selection.bottom());
        for (const QRect &edge : {QRect(850, 650, 140, 140), QRect(5, 650, 120, 140), QRect(5, 5, 980, 780)})
        {
            dialog.placeNear(edge, parent.rect());
            QVERIFY(parent.rect().contains(dialog.geometry()));
        }
        dialog.placeNear(selection, parent.rect());
        parent.show();
        dialog.show();
        QTest::qWait(20);
        auto *header = dialog.findChild<QWidget *>(QStringLiteral("aiHeader"));
        const QPoint before = dialog.pos();
        const QPoint start = header->rect().center();
        QTest::mousePress(header, Qt::LeftButton, Qt::NoModifier, start);
        const QPoint target = start + QPoint(30, 20);
        QMouseEvent move(QEvent::MouseMove, QPointF(target), QPointF(header->mapToGlobal(target)),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(header, &move);
        QTest::mouseRelease(header, Qt::LeftButton, Qt::NoModifier, target);
        QCOMPARE(dialog.pos(), before + QPoint(30, 20));
        dialog.placeNear(QRect(5, 5, 100, 100), QRect(0, 0, 320, 340));
        QVERIFY(QRect(0, 0, 320, 340).contains(dialog.geometry()));
    }
};

QTEST_MAIN(AiTest)
#include "ai_test.moc"
