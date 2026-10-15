#include "platform/keyboard.h"
#include "ui/annotation/annotation_canvas.h"
#include "ui/annotation/annotation_option_widgets.h"
#include "ui/annotation/annotation_options.h"
#include "ui/snip/capture_icons.h"
#include "ui/snip/snip_overlay.h"

#include <QButtonGroup>
#include <QColorDialog>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMenu>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedLayout>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace snipaste::ui
{

void SnipOverlay::createToolbar()
{
    if (m_toolbar)
        return;
    m_toolbar = new QFrame(this);
    m_toolbar->setObjectName(QStringLiteral("captureToolbar"));
    m_toolbar->setCursor(Qt::ArrowCursor);
    m_toolbar->setStyleSheet(QStringLiteral(
        "QFrame#captureToolbar { background: transparent; border: none; }"
        "QFrame#captureActions, QFrame#captureOptions { background: #ffffff; border: 1px solid #2889ed; }"
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
    actions->setObjectName(QStringLiteral("captureActions"));
    auto *row = new QHBoxLayout(actions);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    stack->addWidget(actions);

    m_annotationOptions = new AnnotationOptions(m_toolbar, [this] { refreshAnnotationCursor(); }, this);
    m_toolOptions = m_annotationOptions;
    m_toolOptionsStack = m_annotationOptions->stack();
    stack->addWidget(m_toolOptions, 0, Qt::AlignLeft);

    // Keep the action row inside the overlay at fractional DPI.  The row has
    // nineteen regular buttons, one narrower "more" button, two separators,
    // and the action frame border.
    const qreal compact = qMin<qreal>(1, qMax<qreal>(0, (width() - 40) / 608.0));
    const auto button = [row, actions, compact](CaptureIcon icon, const QString &name, const QString &tip) {
        auto *b = new QToolButton(actions);
        b->setObjectName(name);
        b->setAccessibleName(name);
        b->setIcon(captureIcon(icon));
        b->setIconSize(QSize(16, 16));
        b->setFixedSize(qMax(16, int(32 * compact)), 30);
        b->setProperty("captureActionButton", true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setToolTip(tip);
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
    auto *toolGroup = new QButtonGroup(m_toolbar);
    toolGroup->setObjectName(QStringLiteral("annotationTools"));
    toolGroup->setExclusive(false);
    const auto tool = [this, button, toolGroup](CaptureIcon icon, const QString &name,
                                                AnnotationCanvas::Tool kind, int pageIndex) {
        auto *b = button(icon, name, name);
        b->setProperty("annotationTool", int(kind));
        b->setProperty("pageIndex", pageIndex);
        b->setCheckable(true);
        toolGroup->addButton(b);
        connect(b, &QToolButton::clicked, this, [this, b, toolGroup, kind, pageIndex](bool checked) {
            if (!m_canvas)
                return;
            for (auto *other : toolGroup->buttons())
            {
                if (other != b)
                {
                    QSignalBlocker blocker(other);
                    other->setChecked(false);
                }
            }
            if (checked)
            {
                m_canvas->setAttribute(Qt::WA_TransparentForMouseEvents, false);
                const auto toolKind = static_cast<AnnotationCanvas::Tool>(
                    b->property("annotationTool").isValid() ? b->property("annotationTool").toInt()
                                                            : int(kind));
                m_annotationOptions->showPage(pageIndex);
                m_canvas->setTool(toolKind);
                refreshAnnotationCursor();
                QTimer::singleShot(0, this, [this] { refreshAnnotationCursor(); });
                if (QWidget *w = m_toolOptionsStack->currentWidget())
                {
                    QSize s = w->sizeHint();
                    m_toolOptions->setFixedSize(s.width() + 2, s.height() + 2);
                    m_toolOptions->updateGeometry();
                }
                m_toolOptions->show();
                m_toolbar->adjustSize();
            }
            else
            {
                m_canvas->setAttribute(Qt::WA_TransparentForMouseEvents, true);
                m_toolOptions->hide();
            }
            updateToolbarGeometry();
            setFocus();
        });
        return b;
    };

    auto *shapes = tool(CaptureIcon::Shapes, QStringLiteral("形状"), AnnotationCanvas::Tool::Rectangle, 0);
    auto *lines = tool(CaptureIcon::Line, QStringLiteral("线条"), AnnotationCanvas::Tool::Line, 1);
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
    shapes->setToolTip(QStringLiteral("形状（右键选择矩形、椭圆、圆角矩形）"));
    choices(lines, {{QStringLiteral("直线"), AnnotationCanvas::Tool::Line},
                    {QStringLiteral("箭头"), AnnotationCanvas::Tool::Arrow}});
    auto *arrowStyles = lines->menu()->addMenu(QStringLiteral("箭头样式"));
    const auto addArrowStyle = [this, arrowStyles](const QString &label, AnnotationCanvas::ArrowStyle style) {
        auto *action = arrowStyles->addAction(label);
        connect(action, &QAction::triggered, this, [this, style] {
            if (m_canvas)
                m_canvas->setArrowStyle(style);
        });
    };
    addArrowStyle(QStringLiteral("末端箭头"), AnnotationCanvas::ArrowStyle::End);
    addArrowStyle(QStringLiteral("起点箭头"), AnnotationCanvas::ArrowStyle::Start);
    addArrowStyle(QStringLiteral("双向箭头"), AnnotationCanvas::ArrowStyle::Both);
    addArrowStyle(QStringLiteral("空心末端"), AnnotationCanvas::ArrowStyle::HollowEnd);
    addArrowStyle(QStringLiteral("空心双向"), AnnotationCanvas::ArrowStyle::HollowBoth);
    lines->setToolTip(QStringLiteral("线条（右键选择直线、箭头；Tab 切换）"));

    tool(CaptureIcon::Pen, QStringLiteral("画笔"), AnnotationCanvas::Tool::Pen, 2);
    tool(CaptureIcon::Marker, QStringLiteral("荧光笔"), AnnotationCanvas::Tool::Marker, 3);
    tool(CaptureIcon::Mosaic, QStringLiteral("马赛克"), AnnotationCanvas::Tool::Mosaic, 4);
    auto *textTool = tool(CaptureIcon::Text, QStringLiteral("文字"), AnnotationCanvas::Tool::Text, 5);
    textTool->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(textTool, &QWidget::customContextMenuRequested, this, [this, textTool](const QPoint &position) {
        QMenu menu;
        QAction *disable = menu.addAction(QStringLiteral("无描边"));
        QAction *colorAction = menu.addAction(QStringLiteral("描边颜色…"));
        QAction *widthAction = menu.addAction(QStringLiteral("描边宽度…"));
        QAction *chosen = menu.exec(textTool->mapToGlobal(position));
        if (!m_canvas)
            return;
        if (chosen == disable)
        {
            m_canvas->setTextOutlineWidth(0.0);
        }
        else if (chosen == colorAction)
        {
            const QColor color =
                QColorDialog::getColor(m_canvas->textOutlineColor(), this, QStringLiteral("文字描边颜色"),
                                       QColorDialog::ShowAlphaChannel);
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
            const double width =
                QInputDialog::getDouble(this, QStringLiteral("文字描边宽度"), QStringLiteral("宽度 (px):"),
                                        m_canvas->textOutlineWidth(), 0.0, 16.0, 1, &ok);
            if (ok)
                m_canvas->setTextOutlineWidth(width);
        }
    });
    tool(CaptureIcon::Number, QStringLiteral("序号"), AnnotationCanvas::Tool::Number, 6);
    tool(CaptureIcon::Magnifier, QStringLiteral("放大镜标注"), AnnotationCanvas::Tool::Magnifier, 7);
    tool(CaptureIcon::Eraser, QStringLiteral("橡皮擦"), AnnotationCanvas::Tool::Eraser, 8);
    for (auto *b : toolGroup->buttons())
    {
        const int number = b->property("pageIndex").toInt() + 1;
        QString letter;
        if (number == 3) letter = QStringLiteral("B");
        if (number == 6) letter = QStringLiteral("T");
        if (number == 7) letter = QStringLiteral("N");
        if (number == 9) letter = QStringLiteral("E");
        b->setProperty("toolKey", letter);
        QString keys = snipaste::platform::primaryShortcutText(QString::number(number));
        if (!letter.isEmpty()) keys += QStringLiteral(" / ") + letter;
        b->setToolTip(QStringLiteral("%1 (%2)").arg(b->toolTip(), keys));
    }
    separator();
    connect(button(CaptureIcon::Ocr, QStringLiteral("文字识别"), QStringLiteral("识别选区文字")),
            &QToolButton::clicked, this, &SnipOverlay::ocrSelection);
    connect(button(CaptureIcon::Ai, QStringLiteral("AI"), QStringLiteral("与 AI 聊聊这张截图")),
            &QToolButton::clicked, this, [this] {
                if (m_canvas)
                    m_canvas->finishTextInput();
                const QImage image = m_canvas ? m_canvas->renderedImage() : m_background.copy(imageSelection());
                if (image.isNull())
                    return;
                emit aiRequested(image);
            });
    auto *scrollCapture = button(CaptureIcon::Scroll, QStringLiteral("滚动截图"),
                                 QStringLiteral("滚动截图：手动滚动后采集当前视口"));
    connect(scrollCapture, &QToolButton::clicked, this, &SnipOverlay::startScrollCapture);
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
    scrollCapture->setEnabled(false);
    scrollCapture->setToolTip(QStringLiteral("当前平台不支持滚动截图"));
#endif
    auto *undo =
        button(CaptureIcon::Undo, QStringLiteral("撤销"),
               QStringLiteral("撤销 (%1)").arg(snipaste::platform::primaryShortcutText(QStringLiteral("Z"))));
    auto *redo =
        button(CaptureIcon::Redo, QStringLiteral("重做"),
               QStringLiteral("重做 (%1)").arg(snipaste::platform::primaryShortcutText(QStringLiteral("Y"))));
    undo->setEnabled(false);
    redo->setEnabled(false);
    connect(undo, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->undo();
    });
    connect(redo, &QToolButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->redo();
    });
    separator();
    connect(button(CaptureIcon::Cancel, QStringLiteral("取消"), QStringLiteral("取消截图 (Esc)")),
            &QToolButton::clicked, this, &SnipOverlay::close);
    auto *share = button(CaptureIcon::Share, QStringLiteral("分享"), QStringLiteral("分享图像"));
    m_shareButton = share;
    share->setVisible(m_showShareButton);
    connect(share, &QToolButton::clicked, this,
            [this, share] { showOutputMenu(share->mapToGlobal(QPoint(0, share->height())), true); });
    connect(button(CaptureIcon::Paste, QStringLiteral("贴图"),
                   QStringLiteral("贴到屏幕 (%1)")
                       .arg(snipaste::platform::primaryShortcutText(QStringLiteral("T")))),
            &QToolButton::clicked, this, [this] { pasteSelection(); });
    connect(
        button(CaptureIcon::Save, QStringLiteral("保存"),
               QStringLiteral("保存 (%1)").arg(snipaste::platform::primaryShortcutText(QStringLiteral("S")))),
        &QToolButton::clicked, this, [this] { saveSelection(); });
    connect(button(CaptureIcon::Finish, QStringLiteral("完成"), QStringLiteral("复制到剪贴板 (Enter)")),
            &QToolButton::clicked, this, [this] { copySelection(); });
    m_outputOptionsButton = button(CaptureIcon::More, QStringLiteral("更多"), QStringLiteral("更多输出操作"));
    m_outputOptionsButton->setFixedWidth(16);
    m_outputOptionsButton->setProperty("captureActionButton", false);
    m_outputOptionsButton->setVisible(!m_hideOutputOptions);
    connect(m_outputOptionsButton, &QToolButton::clicked, this, [this] {
        showOutputMenu(m_outputOptionsButton->mapToGlobal(QPoint(0, m_outputOptionsButton->height())));
    });

    m_toolbar->installEventFilter(this);
    m_toolOptions->installEventFilter(this);
    // Wheel events over child buttons/sliders are delivered to those widgets,
    // so filtering only the two frames misses the actual toolbar interaction.
    for (QWidget *widget : m_toolbar->findChildren<QWidget *>())
        if (!widget->isWindow())
            widget->installEventFilter(this);

    // Option widgets keep their own bindings and state.
    // Keep additional commands accessible without displacing the reference toolbar actions.
    m_toolbar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_toolbar, &QWidget::customContextMenuRequested, this,
            [this](const QPoint &pos) { showOutputMenu(m_toolbar->mapToGlobal(pos)); });

    m_toolOptions->hide();
    m_toolbar->hide();
    createSizeToolbar();

    connect(m_toolOptionsStack, &QStackedLayout::currentChanged, this, [this](int index) {
        if (!m_toolOptions || !m_toolOptionsStack)
            return;
        if (QWidget *w = m_toolOptionsStack->widget(index))
        {
            QSize s = w->sizeHint();
            m_toolOptions->setFixedSize(s.width() + 2, s.height() + 2);
            m_toolOptions->updateGeometry();
        }
        if (m_toolbar)
        {
            m_toolbar->adjustSize();
        }
        if (m_toolOptions->isVisible())
        {
            updateToolbarGeometry();
        }
    });
}

void SnipOverlay::setHideOutputOptions(bool enabled)
{
    m_hideOutputOptions = enabled;
    if (m_outputOptionsButton)
        m_outputOptionsButton->setVisible(!enabled);
    updateToolbarGeometry();
}

void SnipOverlay::setShowShareButton(bool enabled)
{
    m_showShareButton = enabled;
    if (m_shareButton)
        m_shareButton->setVisible(enabled);
    updateToolbarGeometry();
}

void SnipOverlay::confirmSelection()
{
    if (m_selection.isEmpty() || m_canvas)
        return;
    invalidateTargetDetection();
    setHoverSelection({}, false);
    if (!m_toolbar)
        createToolbar();
    // Saved desktops use the same full-frame coordinates as normal captures.
    // A legacy history item without a desktop keeps its original image size.
    const bool historyOverride = !m_selectionImageOverride.isNull();
    QImage image = historyOverride ? m_selectionImageOverride : m_background;
    m_selectionImageOverride = {};
    image.setDevicePixelRatio(1);
    m_canvas = m_preparedCanvas ? m_preparedCanvas : new AnnotationCanvas(image, this);
    m_canvas->setCaptureImage(image, {}, historyOverride ? QSizeF(m_selection.size()) : QSizeF(size()));
    m_canvas->setCanvasFrame(historyOverride ? QPoint() : m_selection.topLeft(),
                             historyOverride ? QSizeF(m_selection.size()) : QSizeF(size()),
                             historyOverride ? QRect() : m_selection);
    m_canvas->setBackgroundVisible(historyOverride);
    // These bindings belong to the current selection. Child/editor bindings
    // stay intact when the prepared canvas is reused.
    disconnect(m_canvas, nullptr, this, nullptr);
    m_canvas->setAutoSelectOnCreation(m_autoSelectAnnotations);
    m_canvas->setRememberToolColors(m_rememberToolColors);
    m_canvas->setFreeSelect(m_freeSelect);
    m_canvas->setDisableEraserEditing(m_disableEraserEditing);
    if (m_defaultAnnotation == QStringLiteral("矩形"))
        m_canvas->setTool(AnnotationCanvas::Tool::Rectangle);
    else if (m_defaultAnnotation == QStringLiteral("箭头"))
        m_canvas->setTool(AnnotationCanvas::Tool::Arrow);
    m_canvas->setObjectName(QStringLiteral("selectionCanvas"));
    m_canvas->installEventFilter(this);
    m_canvas->setMinimumSize(0, 0);
    m_canvas->setGeometry(m_selection);
    m_canvas->setAttribute(Qt::WA_TranslucentBackground);
    m_canvas->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_canvas->show();
    if (m_selectionBorder)
    {
        m_selectionBorder->show();
        m_selectionBorder->raise();
        m_selectionBorder->update();
    }
    m_toolbar->raise();
    m_sizeToolbar->raise();
    connect(m_canvas, &AnnotationCanvas::historyChanged, this, [this] {
        m_toolbar->findChild<QToolButton *>(QStringLiteral("撤销"))->setEnabled(m_canvas->canUndo());
        m_toolbar->findChild<QToolButton *>(QStringLiteral("重做"))->setEnabled(m_canvas->canRedo());
    });
    m_annotationOptions->setCanvas(m_canvas);
    connect(m_canvas, &AnnotationCanvas::selectionChanged, this, [this] {
        if (auto *group = m_toolbar->findChild<QButtonGroup *>(QStringLiteral("annotationTools")))
            for (auto *button : group->buttons())
            {
                const QSignalBlocker blocker(button);
                const bool selected = button->property("pageIndex").toInt() == m_toolOptionsStack->currentIndex();
                button->setChecked(selected);
                if (selected)
                    button->setProperty("annotationTool", int(m_canvas->tool()));
            }
        if (QWidget *page = m_toolOptionsStack->currentWidget())
            m_toolOptions->setFixedSize(page->sizeHint() + QSize(2, 2));
        m_toolOptions->show();
        m_toolbar->adjustSize();
        updateToolbarGeometry();
    });
    m_selectionConfirmed = true;
    updateToolbarGeometry();
    update();
}

void SnipOverlay::resetSelection()
{
    if (m_selectionBorder)
        m_selectionBorder->hide();
    invalidateTargetDetection();
    setHoverSelection({}, false);
    clearOcrOverlay();
    if (!m_toolbar)
        createToolbar();
    m_transformingSelection = false;
    m_movingSelection = false;
    m_resizeEdges = {};
    m_selectionConfirmed = false;
    m_externalDrawing = false;
    m_selection = {};
    m_selectionImageOverride = {};
    if (m_preparedCanvas)
    {
        m_preparedCanvas->hide();
        m_preparedCanvas->resetCapture();
        m_preparedCanvas->setObjectName(QStringLiteral("preparedSelectionCanvas"));
    }
    else
    {
        delete m_canvas;
    }
    m_annotationOptions->setCanvas(nullptr);
    m_canvas = nullptr;
    m_toolbar->hide();
    m_sizeToolbar->hide();
    m_toolOptions->hide();
    for (auto *b : m_toolbar->findChildren<QToolButton *>())
    {
        QSignalBlocker blocker(b);
        b->setChecked(false);
    }
    m_toolbar->findChild<QToolButton *>(QStringLiteral("撤销"))->setEnabled(false);
    m_toolbar->findChild<QToolButton *>(QStringLiteral("重做"))->setEnabled(false);
    update();
}

} // namespace snipaste::ui
