#include "ui/paste/paste_window.h"
#include "core/paste/image_transform.h"
#include "platform/keyboard.h"
#include "ui/annotation/annotation_canvas.h"
#include "ui/annotation/annotation_options.h"
#include "ui/clipboard_output.h"
#include "ui/paste/paste_manager.h"
#include "ui/snip/capture_icons.h"
#include "ui/snip/capture_support.h"
#include "ui/snip/snip_overlay.h"

#include <QApplication>
#include <QButtonGroup>
#include <QColorDialog>
#include <QContextMenuEvent>
#include <QFileDialog>
#include <QGraphicsEffect>
#include <QGraphicsProxyWidget>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGuiApplication>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QScreen>
#include <QSignalBlocker>
#include <QStackedLayout>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace snipaste::ui
{
namespace
{
// The proxy keeps editing coordinates in source pixels while the view applies
// rotation, mirroring and zoom. Text input and shape handles use the same map.
class PasteColorEffect final : public QGraphicsEffect
{
  public:
    bool grayscale = false;
    bool inverted = false;

  protected:
    void draw(QPainter *painter) override
    {
        QPoint offset;
        const QPixmap source = sourcePixmap(Qt::DeviceCoordinates, &offset, NoPad);
        QImage image = core::transformImage(source.toImage(), {0, false, false, grayscale, inverted});
        image.setDevicePixelRatio(source.devicePixelRatio());
        painter->save();
        painter->setWorldTransform(QTransform());
        painter->drawImage(offset, image);
        painter->restore();
    }
};
}

class PasteEditorView final : public QGraphicsView
{
  public:
    explicit PasteEditorView(PasteWindow *owner) : QGraphicsView(owner), m_owner(owner) {}

  protected:
    void drawForeground(QPainter *painter, const QRectF &) override
    {
        painter->save();
        painter->resetTransform();
        m_owner->paintBorder(*painter);
        painter->restore();
    }

  private:
    PasteWindow *m_owner;
};

void PasteWindow::createEditor()
{
    if (m_canvas)
        return;
    m_editor = new PasteEditorView(this);
    m_editor->setObjectName(QStringLiteral("pasteEditor"));
    m_editor->setFrameShape(QFrame::NoFrame);
    m_editor->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_editor->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_editor->setStyleSheet(QStringLiteral("QGraphicsView { background: transparent; border: none; }"));
    m_editor->setAlignment(Qt::AlignCenter);
    m_editor->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_editor->installEventFilter(this);
    m_editor->viewport()->installEventFilter(this);
    auto *scene = new QGraphicsScene(m_editor);
    m_editor->setScene(scene);
    m_canvas = new AnnotationCanvas(m_sourceImage);
    m_canvas->setObjectName(QStringLiteral("pasteCanvas"));
    m_canvas->setMinimumSize(0, 0);
    m_canvas->setAttribute(Qt::WA_TranslucentBackground);
    m_canvas->resize(m_sourceImage.size().expandedTo(QSize(1, 1)));
    m_canvas->installEventFilter(this);
    m_canvasProxy = scene->addWidget(m_canvas);
    m_canvasProxy->setGraphicsEffect(new PasteColorEffect);
    m_editor->hide();

    m_toolbar = new QFrame(this, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    m_toolbar->setObjectName(QStringLiteral("pasteToolbar"));
    m_toolbar->setAttribute(Qt::WA_TranslucentBackground);
    m_toolbar->setAttribute(Qt::WA_ShowWithoutActivating);
    m_toolbar->setAttribute(Qt::WA_QuitOnClose, false);
    m_toolbar->installEventFilter(this);
    m_toolbar->setStyleSheet(QStringLiteral(
        "QFrame#pasteToolbar { background: transparent; border: none; }"
        "QFrame#pasteActions, QFrame#captureOptions { background: #ffffff; border: 1px solid #2889ed; }"
        "QToolButton { background: transparent; border: none; border-radius: 0; padding: 0; }"
        "QToolButton:hover { background: #d6e8fa; }"
        "QToolButton:checked { background: #2889ed; color: white; }"
        "QToolButton::menu-indicator { image: none; width: 0; }"
        "QSlider { background: transparent; height: 24px; }"
        "QSlider::groove:horizontal { height: 3px; background: #c4c9cf; border-radius: 1px; }"
        "QSlider::sub-page:horizontal { background: #2889ed; border-radius: 1px; }"
        "QSlider::add-page:horizontal { background: #c4c9cf; border-radius: 1px; }"
        "QSlider::handle:horizontal { width: 10px; height: 10px; margin: -4px 0; border-radius: 5px; "
        "background: #ffffff; border: 2px solid #2889ed; }"
        "QSlider::handle:horizontal:hover { border: 2px solid #1874cd; }"
        "QComboBox { border: 1px solid #c4c9cf; border-radius: 1px; padding: 1px 3px; font-size: 11px; "
        "background: white; }"
        "QComboBox::drop-down { width: 14px; border: none; }"));
    auto *stack = new QVBoxLayout(m_toolbar);
    stack->setContentsMargins(0, 0, 0, 0);
    stack->setSpacing(1);
    auto *actions = new QFrame(m_toolbar);
    actions->setObjectName(QStringLiteral("pasteActions"));
    auto *row = new QHBoxLayout(actions);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    stack->addWidget(actions);
    m_annotationOptions = new AnnotationOptions(m_toolbar, [this] { updateEditorGeometry(); }, this);
    m_annotationOptions->setCanvas(m_canvas);
    stack->addWidget(m_annotationOptions, 0, Qt::AlignLeft);
    m_annotationOptions->hide();
    auto *group = new QButtonGroup(m_toolbar);
    group->setExclusive(false);
    const qreal compact = qMin<qreal>(1, qMax<qreal>(0, (screen()->availableGeometry().width() - 40) / 576.0));
    const auto button = [actions, row, compact](CaptureIcon icon, const QString &name, const QString &shortcut) {
        auto *b = new QToolButton(actions);
        b->setObjectName(name);
        b->setAccessibleName(name);
        b->setIcon(captureIcon(icon));
        b->setIconSize(QSize(16, 16));
        b->setFixedSize(qMax(16, int(32 * compact)), 30);
        b->setFocusPolicy(Qt::NoFocus);
        b->setToolTip(shortcut.isEmpty() ? name : QStringLiteral("%1 (%2)").arg(name, shortcut));
        row->addWidget(b);
        return b;
    };
    const auto separator = [actions, row] {
        auto *line = new QFrame(actions);
        line->setFrameShape(QFrame::VLine);
        line->setStyleSheet(QStringLiteral("background: #c4c9cf;"));
        line->setFixedSize(1, 19);
        row->addSpacing(1);
        row->addWidget(line);
        row->addSpacing(1);
    };
    const auto tool = [this, button, group](CaptureIcon icon, const QString &name,
                                          AnnotationCanvas::Tool kind, int number, const QString &key) {
        const QString shortcut = snipaste::platform::primaryShortcutText(QString::number(number));
        auto *b = button(icon, name, key.isEmpty() ? shortcut : shortcut + QStringLiteral(" / ") + key);
        b->setProperty("annotationTool", int(kind));
        b->setProperty("toolNumber", number);
        b->setProperty("toolKey", key);
        b->setCheckable(true);
        group->addButton(b);
        connect(b, &QToolButton::clicked, this, [this, group, b](bool checked) {
            for (auto *other : group->buttons())
                if (other != b)
                {
                    const QSignalBlocker blocker(other);
                    other->setChecked(false);
                }
            if (checked)
            {
                setAnnotationsVisible(true);
                m_annotationOptions->showPage(b->property("toolNumber").toInt() - 1);
                m_canvas->setTool(static_cast<AnnotationCanvas::Tool>(b->property("annotationTool").toInt()));
            }
            m_editor->setAttribute(Qt::WA_TransparentForMouseEvents, !checked);
            m_annotationOptions->setVisible(checked);
            updateEditorGeometry();
            setFocus();
        });
        return b;
    };
    auto *shapes = tool(CaptureIcon::Shapes, QStringLiteral("形状"), AnnotationCanvas::Tool::Rectangle, 1, QString());
    auto *lines = tool(CaptureIcon::Line, QStringLiteral("线条"), AnnotationCanvas::Tool::Line, 2, QString());
    const auto choices = [this](QToolButton *b, const QList<QPair<QString, AnnotationCanvas::Tool>> &items) {
        auto *menu = new QMenu(b);
        for (const auto &item : items)
        {
            auto *action = menu->addAction(item.first);
            action->setObjectName(item.first);
            connect(action, &QAction::triggered, this, [b, kind = item.second] {
                b->setProperty("annotationTool", int(kind));
                b->setChecked(false);
                b->click();
            });
        }
        b->setMenu(menu);
        b->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(b, &QWidget::customContextMenuRequested, this,
                [b, menu] { menu->popup(b->mapToGlobal(QPoint(0, b->height()))); });
    };
    choices(shapes, {{QStringLiteral("矩形"), AnnotationCanvas::Tool::Rectangle},
                     {QStringLiteral("椭圆"), AnnotationCanvas::Tool::Ellipse},
                     {QStringLiteral("圆角矩形"), AnnotationCanvas::Tool::RoundedRectangle}});
    choices(lines, {{QStringLiteral("直线"), AnnotationCanvas::Tool::Line},
                    {QStringLiteral("箭头"), AnnotationCanvas::Tool::Arrow}});
    shapes->setToolTip(QStringLiteral("形状（右键选择矩形、椭圆、圆角矩形） (%1)")
                          .arg(snipaste::platform::primaryShortcutText(QStringLiteral("1"))));
    lines->setToolTip(QStringLiteral("线条（右键选择直线、箭头；Tab 切换） (%1)")
                         .arg(snipaste::platform::primaryShortcutText(QStringLiteral("2"))));
    auto *arrowStyles = lines->menu()->addMenu(QStringLiteral("箭头样式"));
    const auto addArrowStyle = [this, arrowStyles](const QString &label, AnnotationCanvas::ArrowStyle style) {
        auto *action = arrowStyles->addAction(label);
        connect(action, &QAction::triggered, this, [this, style] { m_canvas->setArrowStyle(style); });
    };
    addArrowStyle(QStringLiteral("末端箭头"), AnnotationCanvas::ArrowStyle::End);
    addArrowStyle(QStringLiteral("起点箭头"), AnnotationCanvas::ArrowStyle::Start);
    addArrowStyle(QStringLiteral("双向箭头"), AnnotationCanvas::ArrowStyle::Both);
    addArrowStyle(QStringLiteral("空心末端"), AnnotationCanvas::ArrowStyle::HollowEnd);
    addArrowStyle(QStringLiteral("空心双向"), AnnotationCanvas::ArrowStyle::HollowBoth);
    tool(CaptureIcon::Pen, QStringLiteral("画笔"), AnnotationCanvas::Tool::Pen, 3, QStringLiteral("B"));
    tool(CaptureIcon::Marker, QStringLiteral("荧光笔"), AnnotationCanvas::Tool::Marker, 4, QString());
    tool(CaptureIcon::Mosaic, QStringLiteral("马赛克"), AnnotationCanvas::Tool::Mosaic, 5, QString());
    auto *textTool = tool(CaptureIcon::Text, QStringLiteral("文字"), AnnotationCanvas::Tool::Text, 6, QStringLiteral("T"));
    textTool->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(textTool, &QWidget::customContextMenuRequested, this, [this, textTool](const QPoint &position) {
        QMenu menu;
        QAction *disable = menu.addAction(QStringLiteral("无描边"));
        QAction *colorAction = menu.addAction(QStringLiteral("描边颜色…"));
        QAction *widthAction = menu.addAction(QStringLiteral("描边宽度…"));
        QAction *chosen = menu.exec(textTool->mapToGlobal(position));
        if (chosen == disable)
            m_canvas->setTextOutlineWidth(0.0);
        else if (chosen == colorAction)
        {
            const QColor color = QColorDialog::getColor(m_canvas->textOutlineColor(), this,
                QStringLiteral("文字描边颜色"), QColorDialog::ShowAlphaChannel);
            if (color.isValid())
            {
                m_canvas->setTextOutlineColor(color);
                if (m_canvas->textOutlineWidth() <= 0.0)
                    m_canvas->setTextOutlineWidth(1.0);
            }
        }
        else if (chosen == widthAction)
        {
            bool ok = false;
            const double width = QInputDialog::getDouble(this, QStringLiteral("文字描边宽度"),
                QStringLiteral("宽度 (px):"), m_canvas->textOutlineWidth(), 0.0, 16.0, 1, &ok);
            if (ok)
                m_canvas->setTextOutlineWidth(width);
        }
    });
    tool(CaptureIcon::Number, QStringLiteral("序号"), AnnotationCanvas::Tool::Number, 7, QStringLiteral("N"));
    tool(CaptureIcon::Magnifier, QStringLiteral("放大镜标注"), AnnotationCanvas::Tool::Magnifier, 8, QString());
    tool(CaptureIcon::Eraser, QStringLiteral("橡皮擦"), AnnotationCanvas::Tool::Eraser, 9, QStringLiteral("E"));
    separator();
    connect(button(CaptureIcon::Ocr, QStringLiteral("文字识别"), QString()),
            &QToolButton::clicked, this, &PasteWindow::extractText);
    auto *scrollCapture = button(CaptureIcon::Scroll, QStringLiteral("滚动截图"), QString());
    scrollCapture->setToolTip(QStringLiteral("滚动截图：手动滚动后采集当前视口"));
    connect(scrollCapture, &QToolButton::clicked, this, &PasteWindow::startScrollCapture);
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
    scrollCapture->setEnabled(false);
    scrollCapture->setToolTip(QStringLiteral("当前平台不支持滚动截图"));
#endif
    auto *undo = button(CaptureIcon::Undo, QStringLiteral("撤销"), snipaste::platform::primaryShortcutText(QStringLiteral("Z")));
    auto *redo = button(CaptureIcon::Redo, QStringLiteral("重做"), snipaste::platform::primaryShortcutText(QStringLiteral("Y")));
    connect(undo, &QToolButton::clicked, m_canvas, &AnnotationCanvas::undo);
    connect(redo, &QToolButton::clicked, m_canvas, &AnnotationCanvas::redo);
    connect(m_canvas, &AnnotationCanvas::historyChanged, this, [this, undo, redo] {
        undo->setEnabled(m_canvas->canUndo());
        redo->setEnabled(m_canvas->canRedo());
        rebuildImage();
    });
    undo->setEnabled(false);
    redo->setEnabled(false);
    separator();
    connect(button(CaptureIcon::Cancel, QStringLiteral("取消"), QStringLiteral("Esc")),
            &QToolButton::clicked, this, [this] { setToolbarVisible(false); });
    auto *share = button(CaptureIcon::Share, QStringLiteral("分享"), QString());
    connect(share, &QToolButton::clicked, this,
            [this, share] { showShareMenu(share->mapToGlobal(QPoint(0, share->height()))); });
    connect(button(CaptureIcon::Save, QStringLiteral("保存"), snipaste::platform::primaryShortcutText(QStringLiteral("S"))),
            &QToolButton::clicked, this, &PasteWindow::saveImage);
    auto *copy = button(CaptureIcon::Finish, QStringLiteral("完成"),
                        snipaste::platform::primaryShortcutText(QStringLiteral("C")));
    copy->setToolTip(QStringLiteral("复制到剪贴板 (%1)").arg(snipaste::platform::primaryShortcutText(QStringLiteral("C"))));
    connect(copy,
            &QToolButton::clicked, this, &PasteWindow::copyImage);
    auto *more = button(CaptureIcon::More, QStringLiteral("更多"), QString());
    more->setFixedWidth(16);
    more->setToolTip(QStringLiteral("更多输出操作"));
    connect(more, &QToolButton::clicked, this, [this, more] {
        auto *menu = new QMenu(m_toolbar);
        menu->setObjectName(QStringLiteral("pasteOutputMenu"));
        menu->setAttribute(Qt::WA_DeleteOnClose);
        populateOutputMenu(menu);
        menu->popup(more->mapToGlobal(QPoint(0, more->height())));
    });
    connect(button(CaptureIcon::FinishEditing, QStringLiteral("完成编辑"), QStringLiteral("Enter / Space / Esc")),
            &QToolButton::clicked, this, [this] { setToolbarVisible(false); });
    connect(m_annotationOptions->stack(), &QStackedLayout::currentChanged, this,
            [this] { updateEditorGeometry(); });
    connect(m_canvas, &AnnotationCanvas::selectionChanged, this, [this, group] {
        for (auto *b : group->buttons())
        {
            const QSignalBlocker blocker(b);
            const bool selected = b->property("toolNumber").toInt() - 1 == m_annotationOptions->stack()->currentIndex();
            b->setChecked(selected);
            if (selected)
                b->setProperty("annotationTool", int(m_canvas->tool()));
        }
        m_annotationOptions->show();
        updateEditorGeometry();
    });
    for (QWidget *widget : m_toolbar->findChildren<QWidget *>())
        if (!widget->isWindow())
            widget->installEventFilter(this);
    m_toolbar->hide();
}

void PasteWindow::setAnnotations(const core::AnnotationDocument &document, const QImage &source)
{
    if (!source.isNull())
        setSourceImage(source);
    createEditor();
    m_canvas->setDocument(document);
    rebuildImage();
}

void PasteWindow::setToolbarVisible(bool visible)
{
    if (visible)
        createEditor();
    if (m_canvas && !visible)
        m_canvas->finishTextInput();
    m_showToolbar = visible;
    if (m_annotationOptions && !visible)
        m_annotationOptions->closePopups();
    rebuildImage();
    updateEditorGeometry();
    if (isVisible())
    {
        activateWindow();
        setFocus();
    }
}

void PasteWindow::setAnnotationsVisible(bool visible)
{
    if (m_canvas)
        m_canvas->finishTextInput();
    m_showAnnotations = visible;
    rebuildImage();
}

void PasteWindow::updateEditorGeometry()
{
    if (!m_editor || !m_canvasProxy || !m_toolbar || !m_annotationOptions)
        return;
    m_editor->setGeometry(rect());
    const QTransform rotation = QTransform().rotate(90 * m_rotationQuarterTurns);
    const QTransform flip = QTransform::fromScale(m_flipHorizontal ? -1 : 1, m_flipVertical ? -1 : 1);
    const QTransform orientation = rotation * flip;
    m_canvasProxy->setTransform(orientation);
    const QRectF bounds = m_canvasProxy->mapRectToScene(m_canvas->rect());
    m_editor->setSceneRect(bounds);
    m_editor->setTransform(QTransform::fromScale(width() / bounds.width(), height() / bounds.height()));
    m_editor->centerOn(bounds.center());
    const qreal ratio = m_disableDpiScaling ? 1.0 : devicePixelRatioF();
    const bool nativeSize = qAbs(width() * ratio - m_image.width()) < 0.5 &&
                            qAbs(height() * ratio - m_image.height()) < 0.5;
    m_editor->setRenderHint(QPainter::SmoothPixmapTransform, m_smoothScale && !nativeSize);
    auto *effect = static_cast<PasteColorEffect *>(m_canvasProxy->graphicsEffect());
    effect->grayscale = m_grayscale;
    effect->inverted = m_inverted;
    effect->setEnabled(m_grayscale || m_inverted);
    effect->update();
    m_editor->setVisible(isVisible() && m_showToolbar && m_showAnnotations && !m_selectionThumbnailCandidate);
    m_toolbar->setWindowFlag(Qt::WindowStaysOnTopHint, m_alwaysOnTop);
    m_toolbar->setVisible(isVisible() && m_showToolbar && !m_clickThrough);
    if (!m_toolbar->isVisible())
        return;
    if (auto *page = m_annotationOptions->stack()->currentWidget())
        m_annotationOptions->setFixedSize(page->sizeHint().expandedTo(QSize(0, 0)) + QSize(2, 2));
    m_toolbar->setFixedSize(m_toolbar->sizeHint());
    updateToolbarPosition();
}

void PasteWindow::updateToolbarPosition()
{
    if (!m_toolbar || !m_toolbar->isVisible())
        return;
    QScreen *target = QGuiApplication::screenAt(frameGeometry().center());
    if (!target)
        target = screen();
    const QRect area = target->availableGeometry();
    QPoint position(frameGeometry().right() - m_toolbar->width() + 1, frameGeometry().bottom() + 7);
    if (position.y() + m_toolbar->height() > area.bottom() + 1)
        position.setY(frameGeometry().top() - m_toolbar->height() - 7);
    position.setX(qBound(area.left(), position.x(), qMax(area.left(), area.right() - m_toolbar->width() + 1)));
    position.setY(qBound(area.top(), position.y(), qMax(area.top(), area.bottom() - m_toolbar->height() + 1)));
    m_toolbar->move(position);
}

void PasteWindow::copyImage()
{
    if (m_canvas)
        m_canvas->finishTextInput();
    rebuildImage();
    if (m_pasteManager)
        emit copyRequested(this);
    else
        copyImageToClipboard(m_image, false, ClipboardImageSource::Paste);
}

void PasteWindow::saveImage()
{
    if (m_canvas)
        m_canvas->finishTextInput();
    rebuildImage();
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("保存贴图"), QStringLiteral("snipaste.png"),
        QStringLiteral("PNG 图片 (*.png);;JPEG 图片 (*.jpg *.jpeg)"));
    if (!path.isEmpty())
        m_image.save(path);
}

void PasteWindow::showShareMenu(const QPoint &position)
{
    auto *menu = new QMenu(m_toolbar);
    menu->setObjectName(QStringLiteral("pasteShareMenu"));
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->addAction(QStringLiteral("复制图像"), this, &PasteWindow::copyImage)->setShortcut(QKeySequence::Copy);
    menu->addAction(QStringLiteral("复制图像文件"), this, [this] {
        m_canvas->finishTextInput();
        rebuildImage();
        copyImageToClipboard(m_image, true, ClipboardImageSource::Paste);
    });
    menu->addAction(QStringLiteral("保存图像…"), this, &PasteWindow::saveImage)->setShortcut(QKeySequence::Save);
#ifdef Q_OS_WIN
    if (QGuiApplication::platformName() != QStringLiteral("offscreen"))
        menu->addAction(QStringLiteral("系统分享"), this, [this] {
            m_canvas->finishTextInput();
            rebuildImage();
            if (!showNativeShare(this, m_image))
                copyImageToClipboard(m_image, true, ClipboardImageSource::Paste);
        });
#endif
    menu->popup(position);
}

void PasteWindow::startScrollCapture()
{
    QScreen *target = screen();
    const QRect selection = frameGeometry().translated(-target->geometry().topLeft());
    setToolbarVisible(false);
    hide();
    // Let the pin, shadow and toolbar leave the desktop before recapturing.
    QTimer::singleShot(80, this, [this, target, selection] {
        auto *overlay = new SnipOverlay(target, false, false, selection, this);
        const auto createResult = [this](const QImage &image, bool edit) {
            if (image.isNull())
                return;
            auto *result = m_pasteManager ? m_pasteManager->createPaste(image) : new PasteWindow(image);
            if (!m_pasteManager)
                result->showAtCursor();
            if (edit)
                result->setToolbarVisible(true);
        };
        connect(overlay, &SnipOverlay::screenshotCaptured, this,
                [createResult](const QImage &image) { createResult(image, false); });
        connect(overlay, &SnipOverlay::pasteRequested, this,
                [createResult](const QImage &image) { createResult(image, false); });
        connect(overlay, &SnipOverlay::annotateRequested, this,
                [createResult](const QImage &image) { createResult(image, true); });
        connect(overlay, &SnipOverlay::sessionClosed, this, [this] {
            showExisting();
            setToolbarVisible(true);
        });
        overlay->show();
        overlay->startScrollCapture();
    });
}

bool PasteWindow::eventFilter(QObject *watched, QEvent *event)
{
    auto *widget = qobject_cast<QWidget *>(watched);
    if (m_editor && watched == m_editor->viewport())
    {
        if (event->type() == QEvent::ContextMenu)
        {
            auto *context = static_cast<QContextMenuEvent *>(event);
            QContextMenuEvent forwarded(context->reason(), widget->mapTo(this, context->pos()),
                                        context->globalPos(), context->modifiers());
            contextMenuEvent(&forwarded);
            return true;
        }
        if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseMove ||
            event->type() == QEvent::MouseButtonRelease)
        {
            auto *mouse = static_cast<QMouseEvent *>(event);
            const bool start = event->type() == QEvent::MouseButtonPress && mouse->button() == Qt::RightButton &&
                mouseModifierMatches(mouseAction(14, QStringLiteral("无")), mouse->modifiers()) &&
                mouseAction(15, QStringLiteral("鼠标右键拖拽")) == QStringLiteral("鼠标右键拖拽");
            if (start || m_selectionThumbnailCandidate)
            {
                QMouseEvent forwarded(mouse->type(), widget->mapTo(this, mouse->position().toPoint()),
                    mouse->globalPosition(), mouse->button(), mouse->buttons(), mouse->modifiers());
                if (event->type() == QEvent::MouseButtonPress) mousePressEvent(&forwarded);
                else if (event->type() == QEvent::MouseMove) mouseMoveEvent(&forwarded);
                else mouseReleaseEvent(&forwarded);
                return true;
            }
        }
    }
    if (event->type() == QEvent::Wheel && m_showToolbar && m_canvas &&
        !m_editor->testAttribute(Qt::WA_TransparentForMouseEvents) && widget &&
        (widget == m_toolbar || m_toolbar->isAncestorOf(widget)))
    {
        QApplication::sendEvent(m_canvas, event);
        return true;
    }
    if (event->type() == QEvent::Close && watched == m_toolbar)
    {
        setToolbarVisible(false);
        event->ignore();
        return true;
    }
    if (event->type() == QEvent::KeyPress)
    {
        auto *key = static_cast<QKeyEvent *>(event);
        const auto modifiers = key->modifiers();
        const bool command = modifiers.testFlag(snipaste::platform::primaryModifier());
        const bool plain = modifiers == Qt::NoModifier;
        const bool tool = (command && key->key() >= Qt::Key_1 && key->key() <= Qt::Key_9) ||
                          (plain && (key->key() == Qt::Key_B || key->key() == Qt::Key_T ||
                                     key->key() == Qt::Key_N || key->key() == Qt::Key_E));
        if ((plain && (key->key() == Qt::Key_Space || key->key() == Qt::Key_Return ||
                       key->key() == Qt::Key_Enter || key->key() == Qt::Key_Escape || key->key() == Qt::Key_Tab)) || tool ||
            (command && (key->key() == Qt::Key_S || key->key() == Qt::Key_W ||
                         (key->key() == Qt::Key_C && !modifiers.testFlag(Qt::ShiftModifier)))))
        {
            keyPressEvent(key);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}
} // namespace snipaste::ui
