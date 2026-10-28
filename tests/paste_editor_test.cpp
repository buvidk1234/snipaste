#include "core/paste/image_transform.h"
#include "core/paste/paste_state_store.h"
#include "platform/keyboard.h"
#include "ui/annotation/annotation_canvas.h"
#include "ui/annotation/annotation_options.h"
#include "ui/paste/paste_manager.h"
#include "ui/paste/paste_window.h"
#include "ui/snip/snip_overlay.h"
#include "ui/snip/capture_icons.h"

#include <QApplication>
#include <QClipboard>
#include <QCheckBox>
#include <QContextMenuEvent>
#include <QDir>
#include <QElapsedTimer>
#include <QFrame>
#include <QGraphicsProxyWidget>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QBackingStore>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QSignalSpy>
#include <QStackedLayout>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QWheelEvent>
#include <memory>

using namespace snipaste;

namespace
{
class PaintCounter final : public QObject
{
  public:
    int paints = 0;
  protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        paints += event->type() == QEvent::Paint;
        return false;
    }
};

QImage baseImage()
{
    QImage image(360, 240, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(38, 42, 49));
    return image;
}

ui::AnnotationCanvas *canvas(ui::PasteWindow *pin)
{
    auto *view = pin->findChild<QGraphicsView *>(QStringLiteral("pasteEditor"));
    if (!view) return nullptr;
    for (auto *item : view->scene()->items())
        if (auto *proxy = qgraphicsitem_cast<QGraphicsProxyWidget *>(item))
            return qobject_cast<ui::AnnotationCanvas *>(proxy->widget());
    return nullptr;
}

bool menuAction(ui::PasteWindow *pin, const QString &text, QKeySequence *shortcut = nullptr, bool trigger = true)
{
    bool found = false;
    QTimer::singleShot(0, pin, [&] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!menu) return;
        for (auto *action : menu->actions())
            if (action->text() == text)
            {
                found = true;
                if (shortcut) *shortcut = action->shortcut();
                if (text == QStringLiteral("显示工具条"))
                {
                    const QString artifacts = qEnvironmentVariable("SNIPASTE_TEST_ARTIFACTS");
                    if (!artifacts.isEmpty())
                    {
                        QDir().mkpath(artifacts);
                        menu->grab().save(QDir(artifacts).filePath(QStringLiteral("paste-menu.png")));
                    }
                }
                if (!trigger)
                {
                    menu->close();
                    return;
                }
                menu->setActiveAction(action);
                QTest::keyClick(menu, Qt::Key_Return);
                return;
            }
        menu->close();
    });
    QContextMenuEvent event(QContextMenuEvent::Mouse, pin->rect().center(),
                            pin->mapToGlobal(pin->rect().center()));
    QApplication::sendEvent(pin, &event);
    return found;
}

void addStroke(ui::PasteWindow *pin)
{
    QTest::keyClick(pin, Qt::Key_B);
    auto *drawing = canvas(pin);
    drawing->setPenWidth(8);
    QTest::mousePress(drawing, Qt::LeftButton, Qt::NoModifier, QPoint(40, 70));
    QTest::mouseMove(drawing, QPoint(240, 150));
    QTest::mouseRelease(drawing, Qt::LeftButton, Qt::NoModifier, QPoint(240, 150));
}

void settleClipboard()
{
    // Windows clipboard history requests delayed data from the GUI thread.
    // Give that request time to finish before the next clipboard change.
    if (QGuiApplication::platformName() == QStringLiteral("windows"))
        QTest::qWait(150);
}
}

class PasteEditorTest : public QObject
{
    Q_OBJECT
  private slots:
    void preparedEditorIsReused();
    void restoredEditorsArePreparedWhileIdle();
    void flashChangesOnlyEdges_data();
    void flashChangesOnlyEdges();
    void selectionUsesBlueAndClearsPreviousPin();
    void toolbarAndMenuShortcuts();
    void toolbarPreservesCaptureButtons();
    void restoredToolbarActions();
    void toolbarKeepsImagePixels_data();
    void toolbarKeepsImagePixels();
    void shadowControlsBorder();
    void rightDragKeepsCropPositionAndSuppressesMenu_data();
    void rightDragKeepsCropPositionAndSuppressesMenu();
    void annotationsFinishHideCopyAndUndo();
    void textInputKeepsSpacesAndFinishCommits();
    void proxyKeepsSourceCoordinatesAfterTransforms();
    void shadowHasVisiblePixelsAndFollowsWindow();
    void movingPinsReusesPaintedSurfaces_data();
    void movingPinsReusesPaintedSurfaces();
    void edgeHoverShowsResizeCursor_data();
    void edgeHoverShowsResizeCursor();
    void dragReleaseUsesFinalPointerPosition_data();
    void dragReleaseUsesFinalPointerPosition();
    void dragAmongManyPins_data();
    void dragAmongManyPins();
    void closedPinsReleaseDisplayBuffers();
    void annotationsAndDecorationPersist();
    void legacyDefaultDecorationsMigrate();
    void captureToolShortcutsAndLayers();
};

void PasteEditorTest::preparedEditorIsReused()
{
    ui::PasteWindow cold(baseImage());
    cold.showExisting();
    QElapsedTimer timer;
    timer.start();
    cold.setToolbarVisible(true);
    const qint64 coldMs = timer.elapsed();
    cold.hide();
    ui::PasteManager manager;
    manager.setFlashNew(false);
    manager.prepareEditorCache();
    const auto prepared = []() -> ui::PasteWindow * {
        for (auto *widget : QApplication::topLevelWidgets())
            if (widget->objectName() == QStringLiteral("preparedPasteWindow"))
                return qobject_cast<ui::PasteWindow *>(widget);
        return nullptr;
    };
    auto *ready = prepared();
    QVERIFY(ready);
    QVERIFY(ready->sourceImage().isNull());
    QVERIFY(!ready->isVisible());
    auto *toolbar = ready->findChild<QFrame *>(QStringLiteral("pasteToolbar"));
    QVERIFY(toolbar && !toolbar->isVisible());
    QVERIFY(!toolbar->findChild<QToolButton *>(QStringLiteral("annotationTextBold")));
    QVERIFY(!toolbar->findChild<QToolButton *>(QStringLiteral("annotationRectangle")));
    timer.restart();
    auto *pin = manager.createPaste(baseImage());
    pin->setToolbarVisible(true);
    const qint64 cachedMs = timer.elapsed();
    QCOMPARE(pin, ready);
    QCOMPARE(pin->findChild<QFrame *>(QStringLiteral("pasteToolbar")), toolbar);
    QVERIFY(toolbar->isVisible());
    QTest::keyClick(pin, Qt::Key_B);
    auto *width = toolbar->findChild<QToolButton *>(QStringLiteral("penWidth4"));
    QVERIFY(width);
    const qreal oldWidth = canvas(pin)->penWidth();
    QWheelEvent wheel(QPointF(width->rect().center()), width->mapToGlobal(width->rect().center()),
                       QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                       Qt::NoScrollPhase, false);
    QApplication::sendEvent(width, &wheel);
    QCOMPARE(canvas(pin)->penWidth(), oldWidth + 1);
    QTest::keyClick(width, Qt::Key_Space);
    QVERIFY(!pin->toolbarVisible());
    pin->setToolbarVisible(true);
    QCOMPARE(pin->sourceImage(), baseImage());
    pin->setToolbarVisible(false);
    pin->setToolbarVisible(true);
    QCOMPARE(pin->findChild<QFrame *>(QStringLiteral("pasteToolbar")), toolbar);
    QTRY_VERIFY(prepared());
    auto *next = prepared();
    QVERIFY(next != pin);
    QImage second = baseImage();
    second.fill(Qt::red);
    QCOMPARE(manager.createPaste(second), next);
    next->setToolbarVisible(true);
    QCOMPARE(next->sourceImage(), second);
    QVERIFY(!canvas(next)->canUndo());
    const auto icon = toolbar->findChild<QToolButton *>(QStringLiteral("画笔"))->icon();
    QCOMPARE(icon.cacheKey(), ui::captureIcon(ui::CaptureIcon::Pen).cacheKey());
    QCOMPARE(next->findChild<QToolButton *>(QStringLiteral("画笔"))->icon().cacheKey(), icon.cacheKey());
    qInfo("Toolbar creation/open: cold %lld ms, prepared %lld ms", coldMs, cachedMs);
}

void PasteEditorTest::restoredEditorsArePreparedWhileIdle()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    {
        ui::PasteManager manager;
        manager.setFlashNew(false);
        manager.createPaste(baseImage());
        manager.createPaste(baseImage());
        manager.saveState(directory.path());
    }
    ui::PasteManager manager;
    manager.loadState(directory.path());
    const auto pins = manager.windowsInGroup(0);
    QCOMPARE(pins.size(), 2);
    for (auto *pin : pins)
    {
        QTRY_VERIFY(pin->findChild<QFrame *>(QStringLiteral("pasteToolbar")));
        auto *toolbar = pin->findChild<QFrame *>(QStringLiteral("pasteToolbar"));
        QVERIFY(!toolbar->isVisible());
        QVERIFY(!pin->toolbarVisible());
        QCOMPARE(pin->displayedImage().convertToFormat(baseImage().format()), baseImage());
        pin->setToolbarVisible(true);
        QCOMPARE(pin->findChild<QFrame *>(QStringLiteral("pasteToolbar")), toolbar);
        QCOMPARE(pin->displayedImage().convertToFormat(baseImage().format()), baseImage());
    }
}

void PasteEditorTest::flashChangesOnlyEdges_data()
{
    QTest::addColumn<bool>("editing");
    QTest::newRow("image") << false;
    QTest::newRow("editing") << true;
}

void PasteEditorTest::flashChangesOnlyEdges()
{
    QFETCH(bool, editing);
    ui::PasteManager manager;
    manager.setFlashNew(false);
    manager.setFlashTransparency(true);
    auto *pin = manager.createPaste(baseImage());
    pin->setToolbarVisible(editing);
    const QRect interior = pin->rect().adjusted(8, 8, -8, -8);
    const auto edge = [pin] { return pin->grab(QRect(pin->width() / 2, 0, 1, 3)).toImage(); };
    const QImage before = pin->grab(interior).toImage();
    const QImage normalEdge = edge();
    QList<QImage> flashEdges;
    const qreal opacity = pin->windowOpacity();
    pin->flash();
    QVERIFY(edge() != normalEdge);
    QElapsedTimer timer;
    timer.start();
    // Sample between the 80 ms timer ticks so delayed events do not hide colors.
    while (pin->isFlashing() && timer.elapsed() < 2000) {
        const QImage current = edge();
        if (current != normalEdge && !flashEdges.contains(current))
            flashEdges.append(current);
        QCOMPARE(pin->grab(interior).toImage(), before);
        QCOMPARE(pin->windowOpacity(), opacity);
        QTest::qWait(10);
    }
    QVERIFY(!pin->isFlashing());
    QVERIFY(flashEdges.size() >= 3);
    QCOMPARE(edge(), normalEdge);
    QCOMPARE(pin->displayedImage(), baseImage());
}

void PasteEditorTest::selectionUsesBlueAndClearsPreviousPin()
{
    ui::PasteManager manager;
    manager.setFlashNew(false);
    auto *first = manager.createPaste(baseImage());
    auto *second = manager.createPaste(baseImage());
    const auto edge = [](ui::PasteWindow *pin) {
        return pin->grab(QRect(pin->width() / 2, 0, 1, 2)).toImage();
    };
    const QImage neutral = edge(first);
    QTest::mouseClick(first, Qt::LeftButton, Qt::NoModifier, first->rect().center());
    QVERIFY(first->selected());
    QVERIFY(edge(first) != neutral);
    const QColor selected = edge(first).pixelColor(0, 0);
    QVERIFY(selected.blue() > selected.red());
    QTest::mouseClick(second, Qt::LeftButton, Qt::NoModifier, second->rect().center());
    QVERIFY(!first->selected());
    QVERIFY(second->selected());
    QCOMPARE(edge(first), neutral);
    QTest::mouseClick(first, Qt::LeftButton, platform::primaryModifier(), first->rect().center());
    QVERIFY(first->selected() && second->selected());
    manager.clearSelection();
    QVERIFY(!first->selected() && !second->selected());
    first->setShadowEnabled(false);
    const QImage undecorated = edge(first);
    first->setSelected(true);
    first->flash();
    QCOMPARE(edge(first), undecorated);
    QCOMPARE(first->displayedImage(), baseImage());
}

void PasteEditorTest::toolbarAndMenuShortcuts()
{
    ui::PasteManager manager;
    auto *pin = manager.createPaste(baseImage());
    QVERIFY(!pin->toolbarVisible());
    QKeySequence shortcut;
    QVERIFY(menuAction(pin, QStringLiteral("显示工具条"), &shortcut));
    QCOMPARE(shortcut, QKeySequence(Qt::Key_Space));
    QVERIFY(pin->toolbarVisible());
    auto *toolbar = pin->findChild<QFrame *>(QStringLiteral("pasteToolbar"));
    QVERIFY(toolbar && toolbar->isVisible());
    QVERIFY(menuAction(pin, QStringLiteral("复制图像"), &shortcut, false));
    QCOMPARE(shortcut, QKeySequence(QKeySequence::Copy));
    QVERIFY(menuAction(pin, QStringLiteral("图像另存为…"), &shortcut, false));
    QCOMPARE(shortcut, QKeySequence(QKeySequence::Save));
    QVERIFY(toolbar->findChild<QToolButton *>(QStringLiteral("画笔"))->toolTip().contains(QStringLiteral("B")));
    QTest::keyClick(pin, Qt::Key_Space);
    QVERIFY(!toolbar->isVisible());
    QTest::keyClick(pin, Qt::Key_3, platform::primaryModifier());
    QVERIFY(toolbar->isVisible());
    QCOMPARE(canvas(pin)->tool(), ui::AnnotationCanvas::Tool::Pen);
    QTest::keyClick(pin, Qt::Key_Escape);
    QVERIFY(pin->isVisible());
    QVERIFY(!toolbar->isVisible());
    QTest::keyClick(pin, Qt::Key_Escape);
    QCOMPARE(pin->state(), ui::PasteWindow::State::Closed);
}

void PasteEditorTest::toolbarPreservesCaptureButtons()
{
    std::unique_ptr<ui::SnipOverlay> capture(ui::SnipOverlay::createPrepared(QApplication::primaryScreen()));
    capture->beginCapture(QApplication::primaryScreen(), false, false, QRect(30, 30, 360, 240));
    capture->show();
    auto *captureToolbar = capture->findChild<QFrame *>(QStringLiteral("captureToolbar"));
    auto *captureActions = captureToolbar->findChild<QFrame *>(QStringLiteral("captureActions"));
    const QStringList original{QStringLiteral("形状"), QStringLiteral("线条"), QStringLiteral("画笔"),
        QStringLiteral("荧光笔"), QStringLiteral("马赛克"), QStringLiteral("文字"), QStringLiteral("序号"),
        QStringLiteral("放大镜标注"), QStringLiteral("橡皮擦"), QStringLiteral("文字识别"), QStringLiteral("滚动截图"),
        QStringLiteral("撤销"), QStringLiteral("重做"), QStringLiteral("取消"), QStringLiteral("分享"),
        QStringLiteral("贴图"), QStringLiteral("保存"), QStringLiteral("完成"), QStringLiteral("更多")};
    const auto names = [](QFrame *actions) {
        QStringList result;
        for (auto *button : actions->findChildren<QToolButton *>(QString(), Qt::FindDirectChildrenOnly))
            result.append(button->objectName());
        return result;
    };
    auto captureExpected = original;
    captureExpected.insert(captureExpected.indexOf(QStringLiteral("文字识别")) + 1, QStringLiteral("AI"));
    QCOMPARE(names(captureActions), captureExpected);
    QVERIFY(captureActions->width() <= capture->width());
    QVERIFY(!captureToolbar->findChild<QToolButton *>(QStringLiteral("完成编辑")));
    ui::PasteManager manager;
    auto *pin = manager.createPaste(baseImage());
    pin->setToolbarVisible(true);
    auto *toolbar = pin->findChild<QFrame *>(QStringLiteral("pasteToolbar"));
    auto *actions = toolbar->findChild<QFrame *>(QStringLiteral("pasteActions"));
    auto expected = original;
    expected.removeOne(QStringLiteral("贴图"));
    expected.append(QStringLiteral("完成编辑"));
    QCOMPARE(names(actions), expected);
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    auto *scroll = actions->findChild<QToolButton *>(QStringLiteral("滚动截图"));
    QVERIFY(scroll && scroll->isEnabled());
#endif
    QVERIFY(actions->width() <= pin->screen()->availableGeometry().width());
    QCOMPARE(actions->height(), captureActions->height());
    QString captureStyle = captureToolbar->styleSheet();
    captureStyle.replace(QStringLiteral("captureToolbar"), QStringLiteral("pasteToolbar"));
    captureStyle.replace(QStringLiteral("captureActions"), QStringLiteral("pasteActions"));
    QCOMPARE(toolbar->styleSheet(), captureStyle);
    for (const auto &name : original)
    {
        if (name == QStringLiteral("贴图")) continue;
        auto *before = captureActions->findChild<QToolButton *>(name);
        auto *after = actions->findChild<QToolButton *>(name);
        QCOMPARE(after->height(), before->height());
        QCOMPARE(after->icon().pixmap(20, 20).toImage(), before->icon().pixmap(20, 20).toImage());
    }
    auto *copy = actions->findChild<QToolButton *>(QStringLiteral("完成"));
    auto *finish = actions->findChild<QToolButton *>(QStringLiteral("完成编辑"));
    QVERIFY(copy->icon().pixmap(20, 20).toImage() != finish->icon().pixmap(20, 20).toImage());
    auto *options = static_cast<ui::AnnotationOptions *>(toolbar->findChild<QFrame *>(QStringLiteral("captureOptions")));
    for (int index = 0; index < 9; ++index)
    {
        QTest::keyClick(pin, Qt::Key(Qt::Key_1 + index), platform::primaryModifier());
        QVERIFY(options->isVisible());
        QCOMPARE(options->stack()->currentIndex(), index);
        QCOMPARE(actions->height(), captureActions->height());
        QCOMPARE(toolbar->height(), actions->height() + 1 + options->height());
    }
    // Capture's existing copy action still completes the screenshot session.
    QSignalSpy copied(capture.get(), &ui::SnipOverlay::screenshotCaptured);
    QTest::mouseClick(captureActions->findChild<QToolButton *>(QStringLiteral("完成")), Qt::LeftButton);
    QCOMPARE(copied.count(), 1);
    QVERIFY(!capture->isVisible());
    settleClipboard();
}

void PasteEditorTest::restoredToolbarActions()
{
    ui::PasteManager manager;
    auto *pin = manager.createPaste(baseImage());
    pin->setToolbarVisible(true);
    auto *toolbar = pin->findChild<QFrame *>(QStringLiteral("pasteToolbar"));
    pin->setSourceText(QStringLiteral("recognized text"));
    settleClipboard();
    QTest::mouseClick(toolbar->findChild<QToolButton *>(QStringLiteral("文字识别")), Qt::LeftButton);
    settleClipboard();
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("recognized text"));
    QTest::mouseClick(toolbar->findChild<QToolButton *>(QStringLiteral("分享")), Qt::LeftButton);
    auto *shareMenu = toolbar->findChild<QMenu *>(QStringLiteral("pasteShareMenu"));
    QVERIFY(shareMenu && shareMenu->isVisible());
    QCOMPARE(shareMenu->actions().first()->shortcut(), QKeySequence(QKeySequence::Copy));
    shareMenu->close();
    QTest::mouseClick(toolbar->findChild<QToolButton *>(QStringLiteral("更多")), Qt::LeftButton);
    auto *outputMenu = toolbar->findChild<QMenu *>(QStringLiteral("pasteOutputMenu"));
    QVERIFY(outputMenu && outputMenu->isVisible());
    auto *panel = outputMenu->findChild<QFrame *>(QStringLiteral("outputOptionsPanel"));
    QVERIFY(panel);
    auto checks = panel->findChildren<QCheckBox *>();
    QCOMPARE(checks.size(), 2);
    QCOMPARE(checks.first()->text(), QStringLiteral("阴影:"));
    QCOMPARE(checks.last()->text(), QStringLiteral("边框:"));
    checks.first()->click();
    QVERIFY(!pin->shadowEnabled());
    outputMenu->close();
    QTest::mouseClick(toolbar->findChild<QToolButton *>(QStringLiteral("取消")), Qt::LeftButton);
    QVERIFY(!pin->toolbarVisible());
    QVERIFY(pin->isVisible());
}

void PasteEditorTest::toolbarKeepsImagePixels_data()
{
    QTest::addColumn<int>("rotation");
    QTest::addColumn<bool>("grayscale");
    QTest::addColumn<qreal>("scale");
    QTest::newRow("original") << 0 << false << qreal(1);
    QTest::newRow("rotated") << 1 << false << qreal(1);
    QTest::newRow("grayscale") << 0 << true << qreal(1);
    QTest::newRow("zoomed") << 0 << false << qreal(1.5);
    QTest::newRow("small") << 0 << false << qreal(0.75);
}

void PasteEditorTest::toolbarKeepsImagePixels()
{
    QFETCH(int, rotation);
    QFETCH(bool, grayscale);
    QFETCH(qreal, scale);
    QImage image(360, 240, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            image.setPixelColor(x, y, QColor((x % 2) * 255, (y % 2) * 255, ((x + y) % 2) * 255));
    ui::PasteManager manager;
    manager.setFlashNew(false);
    manager.setShadowEnabled(false);
    manager.prepareEditorCache();
    auto *pin = manager.createPaste(image);
    pin->restoreTransform(rotation, false, false, grayscale, false, scale, false, true);
    const auto pixels = [pin] {
        const QImage frame = pin->grab().toImage();
        return frame.copy(frame.rect().adjusted(3, 3, -3, -3));
    };
    const QImage before = pixels();
    pin->setToolbarVisible(true);
    const QString artifacts = qEnvironmentVariable("SNIPASTE_TEST_ARTIFACTS");
    if (!artifacts.isEmpty())
    {
        QDir().mkpath(artifacts);
        before.save(QDir(artifacts).filePath(QStringLiteral("paste-pixels-%1-before.png").arg(QString::fromLatin1(QTest::currentDataTag()))));
        pixels().save(QDir(artifacts).filePath(QStringLiteral("paste-pixels-%1-editing.png").arg(QString::fromLatin1(QTest::currentDataTag()))));
    }
    QCOMPARE(pixels(), before);
    QTest::keyClick(pin, Qt::Key_B);
    QCOMPARE(pixels(), before);
    pin->setToolbarVisible(false);
    QCOMPARE(pixels(), before);
}

void PasteEditorTest::shadowControlsBorder()
{
    ui::PasteManager manager;
    manager.setFlashNew(false);
    auto *pin = manager.createPaste(baseImage());
    const auto edgeColor = [pin] {
        const QImage image = pin->grab().toImage();
        return image.pixelColor(image.width() / 2, 0);
    };
    const QColor decorated = edgeColor();
    QVERIFY(decorated != baseImage().pixelColor(0, 0));
    pin->setShadowEnabled(false);
    QCOMPARE(edgeColor(), baseImage().pixelColor(0, 0));
    pin->setShadowEnabled(true);
    QCOMPARE(edgeColor(), decorated);
    pin->setToolbarVisible(true);
    const QColor editing = edgeColor();
    QVERIFY(editing != decorated);
    QVERIFY(editing.blue() > editing.red());
    pin->setShadowEnabled(false);
    QCOMPARE(edgeColor(), baseImage().pixelColor(0, 0));
    pin->setShadowEnabled(true);
    QCOMPARE(edgeColor(), editing);
    pin->setToolbarVisible(false);
    QCOMPARE(edgeColor(), decorated);
    pin->setShadowEnabled(false);
    pin->flash();
    QCOMPARE(edgeColor(), baseImage().pixelColor(0, 0));
}

void PasteEditorTest::rightDragKeepsCropPositionAndSuppressesMenu_data()
{
    QTest::addColumn<bool>("editing");
    QTest::addColumn<qreal>("scale");
    QTest::addColumn<int>("rotation");
    QTest::newRow("normal") << false << qreal(1.5) << 0;
    QTest::newRow("editing") << true << qreal(1.5) << 0;
    QTest::newRow("small-rotated") << true << qreal(0.75) << 1;
}

void PasteEditorTest::rightDragKeepsCropPositionAndSuppressesMenu()
{
    QFETCH(bool, editing);
    QFETCH(qreal, scale);
    QFETCH(int, rotation);
    ui::PasteManager manager;
    manager.setFlashNew(false);
    QImage image = baseImage();
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            image.setPixelColor(x, y, QColor(x % 256, y % 256, (x + y) % 256));
    auto *pin = manager.createPaste(image);
    pin->move(120, 110);
    pin->restoreTransform(rotation, false, false, false, false, scale, false, true);
    QWidget *target = pin;
    if (editing)
    {
        QTest::keyClick(pin, Qt::Key_B);
        target = pin->findChild<QGraphicsView *>(QStringLiteral("pasteEditor"))->viewport();
    }
    const QRect selection(QPoint(12, 12), QPoint(59, 47));
    const QPoint position = pin->mapToGlobal(selection.topLeft());
    const QSize originalSize = pin->size();
    const QImage original = pin->displayedImage();
    QTest::mousePress(target, Qt::RightButton, Qt::NoModifier, selection.topLeft());
    QMouseEvent move(QEvent::MouseMove, selection.bottomRight(), target->mapToGlobal(selection.bottomRight()),
                     Qt::NoButton, Qt::RightButton, Qt::NoModifier);
    QApplication::sendEvent(target, &move);
    QTest::mouseRelease(target, Qt::RightButton, Qt::NoModifier, selection.bottomRight());
    QCOMPARE(pin->pos(), position);
    const QRect crop(qRound(selection.x() * qreal(original.width()) / originalSize.width()),
                     qRound(selection.y() * qreal(original.height()) / originalSize.height()),
                     qRound(selection.width() * qreal(original.width()) / originalSize.width()),
                     qRound(selection.height() * qreal(original.height()) / originalSize.height()));
    QCOMPARE(pin->displayedImage(), original.copy(crop));
    QCOMPARE(pin->scale(), scale);
    bool opened = false;
    QTimer::singleShot(0, pin, [&opened] {
        if (auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget()))
        {
            opened = true;
            menu->close();
        }
    });
    QContextMenuEvent event(QContextMenuEvent::Mouse, pin->rect().center(), pin->mapToGlobal(pin->rect().center()));
    QApplication::sendEvent(pin, &event);
    QVERIFY(!opened);
    QApplication::processEvents();
    // A later ordinary right click still opens the menu.
    QTest::mouseClick(pin, Qt::RightButton, Qt::NoModifier, pin->rect().center());
    QVERIFY(menuAction(pin, QStringLiteral("显示工具条"), nullptr, false));
}

void PasteEditorTest::annotationsFinishHideCopyAndUndo()
{
    ui::PasteManager manager;
    const QImage source = baseImage();
    auto *pin = manager.createPaste(source);
    addStroke(pin);
    QCOMPARE(canvas(pin)->document().shapes().size(), 1);
    auto *toolbar = pin->findChild<QFrame *>(QStringLiteral("pasteToolbar"));
    settleClipboard();
    QTest::mouseClick(toolbar->findChild<QToolButton *>(QStringLiteral("完成")), Qt::LeftButton);
    settleClipboard();
    QVERIFY(pin->toolbarVisible());
    QCOMPARE(QApplication::clipboard()->image().convertToFormat(source.format()), pin->displayedImage());
    QApplication::clipboard()->setText(QStringLiteral("finish does not copy"));
    settleClipboard();
    QTest::mouseClick(toolbar->findChild<QToolButton *>(QStringLiteral("完成编辑")), Qt::LeftButton);
    QVERIFY(!pin->toolbarVisible());
    QVERIFY(pin->isVisible());
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("finish does not copy"));
    const QImage edited = pin->displayedImage();
    QVERIFY(edited != source);
    const QString artifacts = qEnvironmentVariable("SNIPASTE_TEST_ARTIFACTS");
    if (!artifacts.isEmpty())
    {
        QDir().mkpath(artifacts);
        pin->setToolbarVisible(true);
        QApplication::processEvents();
        QImage preview(720, 440, QImage::Format_RGB32);
        preview.fill(QColor(30, 33, 39));
        QPainter painter(&preview);
        painter.drawPixmap(170, 20, pin->grab());
        painter.drawPixmap(30, 280, toolbar->grab());
        painter.end();
        QVERIFY(preview.save(QDir(artifacts).filePath(QStringLiteral("paste-editor.png"))));
        pin->setToolbarVisible(false);
    }
    QVERIFY(menuAction(pin, QStringLiteral("显示标注")));
    QCOMPARE(pin->displayedImage(), source);
    QTest::keyClick(pin, Qt::Key_C, platform::primaryModifier());
    settleClipboard();
    QCOMPARE(QApplication::clipboard()->image().convertToFormat(source.format()), source);
    QVERIFY(menuAction(pin, QStringLiteral("显示标注")));
    QCOMPARE(pin->displayedImage(), edited);
    QTest::keyClick(pin, Qt::Key_C, platform::primaryModifier());
    settleClipboard();
    QCOMPARE(QApplication::clipboard()->image().convertToFormat(edited.format()), edited);
    QTest::keyClick(pin, Qt::Key_Space);
    QTest::keyClick(pin, Qt::Key_Z, platform::primaryModifier());
    QCOMPARE(pin->displayedImage(), source);
    QTest::keyClick(pin, Qt::Key_Y, platform::primaryModifier());
    QCOMPARE(pin->displayedImage(), edited);
    pin->setToolbarVisible(false);
    QTest::keyClick(pin, Qt::Key_Escape);
    pin->restoreClosed();
    QCOMPARE(pin->displayedImage(), edited);
    QCOMPARE(canvas(pin)->document().shapes().size(), 1);
}

void PasteEditorTest::textInputKeepsSpacesAndFinishCommits()
{
    ui::PasteManager manager;
    auto *pin = manager.createPaste(baseImage());
    QTest::keyClick(pin, Qt::Key_T);
    auto *drawing = canvas(pin);
    QTest::mouseClick(drawing, Qt::LeftButton, Qt::NoModifier, QPoint(30, 40));
    auto *editor = drawing->findChild<QPlainTextEdit *>(QStringLiteral("annotationTextEditor"));
    QVERIFY(editor->isVisible());
    QTest::keyClicks(editor, "Hello world");
    QCOMPARE(editor->toPlainText(), QStringLiteral("Hello world"));
    QVERIFY(pin->toolbarVisible());
    auto *toolbar = pin->findChild<QFrame *>(QStringLiteral("pasteToolbar"));
    QTest::mouseClick(toolbar->findChild<QToolButton *>(QStringLiteral("完成编辑")), Qt::LeftButton);
    QVERIFY(!pin->toolbarVisible());
    QCOMPARE(drawing->document().shapes().size(), 1);
    QCOMPARE(drawing->document().shapes().first().text, QStringLiteral("Hello world"));
    QVERIFY(pin->displayedImage() != baseImage());
}

void PasteEditorTest::proxyKeepsSourceCoordinatesAfterTransforms()
{
    ui::PasteManager manager;
    auto *pin = manager.createPaste(baseImage());
    addStroke(pin);
    pin->setToolbarVisible(false);
    const QImage annotated = pin->displayedImage();
    QTest::keyClick(pin, Qt::Key_1);
    QTest::keyClick(pin, Qt::Key_3);
    pin->scaleBy(0.8);
    QCOMPARE(pin->displayedImage(), core::transformImage(annotated, {1, true, false, false, false}));
    QTest::keyClick(pin, Qt::Key_N);
    auto *view = pin->findChild<QGraphicsView *>();
    auto *proxy = qgraphicsitem_cast<QGraphicsProxyWidget *>(view->scene()->items().first());
    const QPointF source(180, 100);
    const QPoint position = view->mapFromScene(proxy->mapToScene(source));
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, position);
    QCOMPARE(canvas(pin)->document().shapes().size(), 2);
    QVERIFY(QLineF(canvas(pin)->document().shapes().last().start, source).length() < 3);
    pin->setAnnotationsVisible(false);
    QCOMPARE(pin->displayedImage(), core::transformImage(baseImage(), {1, true, false, false, false}));
}

void PasteEditorTest::shadowHasVisiblePixelsAndFollowsWindow()
{
    ui::PasteManager manager;
    auto *pin = manager.createPaste(baseImage());
    auto *shadow = pin->findChild<QWidget *>(QStringLiteral("pasteShadow"));
    QVERIFY(shadow && shadow->isVisible());
    const auto shadows = pin->findChildren<QWidget *>(QStringLiteral("pasteShadow"));
    QCOMPARE(shadows.size(), 4);
    const int margin = pin->pos().x() - shadow->pos().x();
    const QSize extent = pin->size() + QSize(margin * 2, margin * 2);
    const qreal ratio = pin->devicePixelRatioF();
    QImage pixels(QSize(qRound(extent.width() * ratio), qRound(extent.height() * ratio)),
                  QImage::Format_ARGB32_Premultiplied);
    pixels.setDevicePixelRatio(ratio);
    pixels.fill(Qt::transparent);
    QPainter painter(&pixels);
    qint64 surfacePixels = 0;
    for (auto *edge : shadows)
    {
        QVERIFY(edge->isVisible());
        QVERIFY(qMin(edge->width(), edge->height()) <= margin);
        surfacePixels += qint64(edge->width()) * edge->height();
        painter.drawPixmap(edge->pos() - shadow->pos(), edge->grab());
    }
    painter.end();
    QCOMPARE(surfacePixels, qint64(extent.width()) * extent.height() - qint64(pin->width()) * pin->height());
    QImage expected(pixels.size(), pixels.format());
    expected.setDevicePixelRatio(ratio);
    expected.fill(Qt::transparent);
    QPainter reference(&expected);
    reference.setRenderHint(QPainter::Antialiasing);
    reference.setBrush(Qt::NoBrush);
    for (int distance = 1; distance <= margin; ++distance)
    {
        QColor stroke(60, 68, 80, 100);
        const qreal strength = 1.0 - qreal(distance) / (margin + 1);
        stroke.setAlphaF(stroke.alphaF() * strength * strength * 0.6);
        reference.setPen(QPen(stroke, 1));
        reference.drawRoundedRect(QRectF(QPointF(), QSizeF(extent)).adjusted(
            margin - distance + 0.5, margin - distance + 0.5,
            -margin + distance - 0.5, -margin + distance - 0.5), distance, distance);
    }
    // Only the shadow outside the image is visible; the old full surface's
    // antialiasing could also touch the four pixels underneath its corners.
    reference.setCompositionMode(QPainter::CompositionMode_Source);
    reference.fillRect(QRect(QPoint(margin, margin), pin->size()), Qt::transparent);
    reference.end();
    // Compositing the separate premultiplied surfaces can round a color
    // channel by one; alpha and the shadow geometry must stay identical.
    for (int y = 0; y < pixels.height(); ++y)
        for (int x = 0; x < pixels.width(); ++x)
        {
            const QRgb actual = pixels.pixel(x, y);
            const QRgb wanted = expected.pixel(x, y);
            QCOMPARE(qAlpha(actual), qAlpha(wanted));
            QVERIFY(qAbs(qRed(actual) - qRed(wanted)) <= 1);
            QVERIFY(qAbs(qGreen(actual) - qGreen(wanted)) <= 1);
            QVERIFY(qAbs(qBlue(actual) - qBlue(wanted)) <= 1);
        }
    QVERIFY(pixels.pixelColor(pixels.rect().center()).alpha() == 0);
    bool hasShadow = false;
    for (int x = 0; x < pixels.width() / 2; ++x)
        hasShadow |= pixels.pixelColor(x, pixels.height() / 2).alpha() > 0;
    QVERIFY(hasShadow);
    const QPoint offset = shadow->pos() - pin->pos();
    pin->move(pin->pos() + QPoint(50, 40));
    QApplication::processEvents();
    QCOMPARE(shadow->pos() - pin->pos(), offset);
    QVERIFY(menuAction(pin, QStringLiteral("窗口阴影")));
    for (auto *edge : shadows)
        QVERIFY(!edge->isVisible());
    QVERIFY(menuAction(pin, QStringLiteral("窗口阴影")));
    QVERIFY(shadow->isVisible());
    QCOMPARE(pin->displayedImage(), baseImage());
    pin->hide();
    for (auto *edge : shadows)
        QVERIFY(!edge->isVisible());
}

void PasteEditorTest::movingPinsReusesPaintedSurfaces_data()
{
    QTest::addColumn<int>("initializationDelayMs");
    QTest::newRow("ready") << 0;
    QTest::newRow("delayed-initialization") << 150;
}

void PasteEditorTest::movingPinsReusesPaintedSurfaces()
{
    QFETCH(int, initializationDelayMs);
    ui::PasteManager manager;
    manager.setFlashNew(false);
    auto *pin = manager.createPaste(baseImage());
    pin->setToolbarVisible(true);
    auto *view = pin->findChild<QGraphicsView *>(QStringLiteral("pasteEditor"));
    auto *toolbar = pin->findChild<QFrame *>(QStringLiteral("pasteToolbar"));
    const auto shadows = pin->findChildren<QWidget *>(QStringLiteral("pasteShadow"));
    QCOMPARE(shadows.size(), 4);
    const QPoint start = pin->pos();
    if (initializationDelayMs)
        QTimer::singleShot(0, &manager, [initializationDelayMs] { QTest::qSleep(initializationDelayMs); });
    QTest::qWait(100);
    // Slow initialization can outlast qWait and leave its paints queued.
    QApplication::processEvents();
    PaintCounter counter;
    for (auto *widget : {static_cast<QWidget *>(pin), static_cast<QWidget *>(canvas(pin)),
                         static_cast<QWidget *>(toolbar), view->viewport()})
        widget->installEventFilter(&counter);
    for (auto *shadow : shadows)
        shadow->installEventFilter(&counter);
    const auto sourceKey = pin->sourceImage().cacheKey();
    const auto displayKey = pin->displayedImage().cacheKey();
    const QTransform transform = view->transform();
    const QPoint shadowOffset = shadows.first()->pos() - pin->pos();
    const QPoint toolbarOffset = toolbar->pos() - pin->pos();
    for (int step = 0; step < 12; ++step)
    {
        pin->move(start + QPoint(20 + step * 2, 15));
        QApplication::processEvents();
        QCOMPARE(shadows.first()->pos() - pin->pos(), shadowOffset);
        QCOMPARE(toolbar->y() - pin->y(), toolbarOffset.y());
    }
    QCOMPARE(counter.paints, 0);
    QCOMPARE(view->transform(), transform);
    QCOMPARE(pin->sourceImage().cacheKey(), sourceKey);
    QCOMPARE(pin->displayedImage().cacheKey(), displayKey);
}

void PasteEditorTest::edgeHoverShowsResizeCursor_data()
{
    QTest::addColumn<QPoint>("edge");
    QTest::addColumn<int>("shape");
    QTest::newRow("left") << QPoint(-1, 0) << int(Qt::SizeHorCursor);
    QTest::newRow("right") << QPoint(1, 0) << int(Qt::SizeHorCursor);
    QTest::newRow("top") << QPoint(0, -1) << int(Qt::SizeVerCursor);
    QTest::newRow("bottom") << QPoint(0, 1) << int(Qt::SizeVerCursor);
    QTest::newRow("top-left") << QPoint(-1, -1) << int(Qt::SizeFDiagCursor);
    QTest::newRow("bottom-right") << QPoint(1, 1) << int(Qt::SizeFDiagCursor);
    QTest::newRow("top-right") << QPoint(1, -1) << int(Qt::SizeBDiagCursor);
    QTest::newRow("bottom-left") << QPoint(-1, 1) << int(Qt::SizeBDiagCursor);
}

void PasteEditorTest::edgeHoverShowsResizeCursor()
{
    QFETCH(QPoint, edge);
    QFETCH(int, shape);
    ui::PasteManager manager;
    manager.setFlashNew(false);
    auto *pin = manager.createPasteAtPosition(baseImage(), QPoint(80, 80));
    const auto coordinate = [](int side, int length) {
        return side < 0 ? 2 : side > 0 ? length - 3 : length / 2;
    };
    const QPoint hit(coordinate(edge.x(), pin->width()), coordinate(edge.y(), pin->height()));
    QTest::mouseMove(pin, hit);
    QCOMPARE(pin->cursor().shape(), Qt::CursorShape(shape));
    QTest::mouseMove(pin, pin->rect().center());
    QCOMPARE(pin->cursor().shape(), Qt::ArrowCursor);
    pin->setResizeEnabled(false);
    QTest::mouseMove(pin, hit);
    QCOMPARE(pin->cursor().shape(), Qt::ArrowCursor);
}

void PasteEditorTest::dragReleaseUsesFinalPointerPosition_data()
{
    QTest::addColumn<bool>("intermediateMove");
    QTest::newRow("last-move-coalesced") << true;
    QTest::newRow("quick-drag") << false;
}

void PasteEditorTest::dragReleaseUsesFinalPointerPosition()
{
    QFETCH(bool, intermediateMove);
    ui::PasteManager manager;
    manager.setFlashNew(false);
    auto *pin = manager.createPasteAtPosition(baseImage(), QPoint(80, 80));
    const QPoint start = pin->pos();
    const QPoint press = pin->mapToGlobal(pin->rect().center());
    const auto send = [&](QEvent::Type type, const QPoint &global) {
        QMouseEvent event(type, pin->mapFromGlobal(global), global,
                          type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                          type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                          Qt::NoModifier);
        QApplication::sendEvent(pin, &event);
    };
    send(QEvent::MouseButtonPress, press);
    if (intermediateMove)
        send(QEvent::MouseMove, press + QPoint(25, 20));
    send(QEvent::MouseButtonRelease, press + QPoint(110, 90));
    QCOMPARE(pin->pos(), start + QPoint(110, 90));
}

void PasteEditorTest::dragAmongManyPins_data()
{
    QTest::addColumn<bool>("shift");
    QTest::addColumn<bool>("nearby");
    QTest::addColumn<bool>("nearestFirst");
    QTest::newRow("ordinary-drag") << false << true << true;
    QTest::newRow("distant-pins-cannot-attract") << true << false << true;
    QTest::newRow("nearest-first") << true << true << true;
    QTest::newRow("nearest-last") << true << true << false;
}

void PasteEditorTest::dragAmongManyPins()
{
    QFETCH(bool, shift);
    QFETCH(bool, nearby);
    QFETCH(bool, nearestFirst);
    ui::PasteManager manager;
    manager.setFlashNew(false);
    manager.setActivateNew(false);
    manager.setShadowEnabled(false);
    manager.setDisableDpiScaling(true);
    manager.setMagneticAttachThreshold(20);
    QImage image(120, 80, QImage::Format_RGB32);
    image.fill(Qt::yellow);
    auto *pin = manager.createPasteAtPosition(image, QPoint(80, 80));
    const QPoint destination(400, 300);
    if (nearby)
        for (int gap : nearestFirst ? QList<int>{3, 18} : QList<int>{18, 3})
            manager.createPasteAtPosition(image, destination + QPoint(-image.width() + gap, 0));
    // These edges form a chain in X, but their windows are far away in Y.
    for (int index = 1; index <= 24; ++index)
        manager.createPasteAtPosition(image, destination + QPoint(-image.width() + index * 4, 500 + index * 90));
    const QPoint hit = pin->rect().center();
    const QPoint press = pin->mapToGlobal(hit);
    const QPoint release = destination + hit;
    const auto modifiers = shift ? Qt::ShiftModifier : Qt::NoModifier;
    QMouseEvent down(QEvent::MouseButtonPress, hit, press, Qt::LeftButton, Qt::LeftButton, modifiers);
    QApplication::sendEvent(pin, &down);
    QMouseEvent move(QEvent::MouseMove, pin->mapFromGlobal(release), release,
                     Qt::NoButton, Qt::LeftButton, modifiers);
    QApplication::sendEvent(pin, &move);
    QMouseEvent up(QEvent::MouseButtonRelease, pin->mapFromGlobal(release), release,
                   Qt::LeftButton, Qt::NoButton, modifiers);
    QApplication::sendEvent(pin, &up);
    QCOMPARE(pin->pos(), destination + QPoint(shift && nearby ? 3 : 0, 0));
    // Plain arrow keys must still move by one pixel beside many magnetic edges.
    QTest::keyClick(pin, Qt::Key_Right);
    QCOMPARE(pin->pos(), destination + QPoint((shift && nearby ? 3 : 0) + 1, 0));
}

void PasteEditorTest::closedPinsReleaseDisplayBuffers()
{
    ui::PasteManager manager;
    manager.setFlashNew(false);
    QImage source(1920, 1080, QImage::Format_ARGB32_Premultiplied);
    source.fill(QColor(38, 42, 49));
    auto *pin = manager.createPaste(source);
    addStroke(pin);
    auto *toolbar = pin->findChild<QFrame *>(QStringLiteral("pasteToolbar"));
    pin->setToolbarVisible(false);
    const QImage edited = pin->displayedImage();
    const QImage visible = pin->grab().toImage();
    auto *shadow = pin->findChild<QWidget *>(QStringLiteral("pasteShadow"));
    QVERIFY(shadow);
    const auto shadows = pin->findChildren<QWidget *>(QStringLiteral("pasteShadow"));
    QCOMPARE(shadows.size(), 4);
    for (int cycle = 0; cycle < 2; ++cycle)
    {
        QTest::keyClick(pin, Qt::Key_Escape);
        QCOMPARE(pin->state(), ui::PasteWindow::State::Closed);
        QCOMPARE(pin->backingStore()->size(), QSize(1, 1));
        for (auto *edge : shadows)
            QCOMPARE(edge->backingStore()->size(), QSize(1, 1));
        QCOMPARE(pin->sourceImage(), source);
        QCOMPARE(pin->displayedImage(), edited);
        pin->restoreClosed();
        QCOMPARE(pin->backingStore()->size(), pin->size());
        for (auto *edge : shadows)
            QCOMPARE(edge->backingStore()->size(), edge->size());
        QCOMPARE(pin->grab().toImage(), visible);
    }
    pin->setToolbarVisible(true);
    QCOMPARE(pin->findChild<QFrame *>(QStringLiteral("pasteToolbar")), toolbar);
    QVERIFY(canvas(pin)->canUndo());
    canvas(pin)->undo();
    QCOMPARE(pin->displayedImage(), source);
}

void PasteEditorTest::annotationsAndDecorationPersist()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QImage edited;
    {
        ui::PasteManager manager;
        auto *pin = manager.createPaste(baseImage());
        addStroke(pin);
        pin->setToolbarVisible(false);
        edited = pin->displayedImage();
        pin->setAnnotationsVisible(false);
        pin->setShadowEnabled(false);
        manager.saveState(directory.path());
    }
    ui::PasteManager restored;
    restored.loadState(directory.path());
    const auto windows = restored.windowsInGroup(0);
    QCOMPARE(windows.size(), 1);
    auto *pin = windows.first();
    QVERIFY(!pin->annotationsVisible());
    QVERIFY(!pin->shadowEnabled());
    QVERIFY(!pin->toolbarVisible());
    QCOMPARE(pin->displayedImage().convertToFormat(baseImage().format()), baseImage());
    pin->setAnnotationsVisible(true);
    QCOMPARE(pin->displayedImage().convertToFormat(edited.format()), edited);
    QCOMPARE(canvas(pin)->document().shapes().size(), 1);
}

void PasteEditorTest::legacyDefaultDecorationsMigrate()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    for (int version : {0, 2})
    {
        core::PasteWindowState pin{baseImage(), {
            {QStringLiteral("borderColor"), QStringLiteral("#ffffcd3c")},
            {QStringLiteral("shadowColor"), QStringLiteral("#96000000")}}};
        if (version)
            pin.options.insert(QStringLiteral("decorationVersion"), version);
        core::PasteSessionState state{{{QStringLiteral("test"), {pin}}}};
        QVERIFY(core::PasteStateStore::save(state, directory.path()));
        ui::PasteManager manager;
        manager.loadState(directory.path());
        manager.saveState(directory.path());
        const auto saved = core::PasteStateStore::load(directory.path());
        QVERIFY(saved);
        const auto options = saved->groups.first().windows.first().options;
        QCOMPARE(options.value(QStringLiteral("decorationVersion")).toInt(), 2);
        QCOMPARE(QColor(options.value(QStringLiteral("borderColor")).toString()),
                 version ? QColor(255, 205, 60, 255) : QColor(220, 225, 232, 220));
        QCOMPARE(QColor(options.value(QStringLiteral("shadowColor")).toString()),
                 version ? QColor(0, 0, 0, 150) : QColor(60, 68, 80, 100));
    }
}

void PasteEditorTest::captureToolShortcutsAndLayers()
{
    ui::SnipOverlay overlay(QApplication::primaryScreen());
    overlay.setTargetDetector([](QPoint, bool) { return QRect(); });
    overlay.show();
    QTest::mousePress(&overlay, Qt::LeftButton, Qt::NoModifier, QPoint(30, 30));
    QTest::mouseMove(&overlay, QPoint(390, 270));
    QTest::mouseRelease(&overlay, Qt::LeftButton, Qt::NoModifier, QPoint(390, 270));
    QTest::keyClick(&overlay, Qt::Key_3, platform::primaryModifier());
    auto *drawing = overlay.findChild<ui::AnnotationCanvas *>();
    QVERIFY(drawing);
    QCOMPARE(drawing->tool(), ui::AnnotationCanvas::Tool::Pen);
    QVERIFY(overlay.findChild<QToolButton *>(QStringLiteral("画笔"))->toolTip().contains(platform::primaryShortcutText(QStringLiteral("3"))));
    QTest::mousePress(drawing, Qt::LeftButton, Qt::NoModifier, QPoint(40, 60));
    QTest::mouseMove(drawing, QPoint(240, 150));
    QTest::mouseRelease(drawing, Qt::LeftButton, Qt::NoModifier, QPoint(240, 150));
    const QImage base = overlay.pasteSourceImage();
    ui::PasteManager manager;
    auto *pin = manager.createPaste(base);
    pin->setAnnotations(overlay.pasteAnnotations());
    QVERIFY(pin->displayedImage() != base);
    pin->setAnnotationsVisible(false);
    QCOMPARE(pin->displayedImage(), base);
}

QTEST_MAIN(PasteEditorTest)
#include "paste_editor_test.moc"
