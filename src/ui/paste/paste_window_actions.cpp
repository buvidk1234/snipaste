#include "ui/clipboard_output.h"
#include "ui/paste/paste_manager.h"
#include "ui/paste/paste_window.h"

#include "platform/keyboard.h"
#include "ui/annotation/annotation_canvas.h"

#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QContextMenuEvent>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QWidgetAction>

namespace snipaste::ui
{
void PasteWindow::cropImage()
{
    if (m_sourceImage.isNull())
        return;
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("裁剪贴图"));
    auto *form = new QFormLayout(&dialog);
    auto makeSpin = [&dialog](int value, int maximum) {
        auto *spin = new QSpinBox(&dialog);
        spin->setRange(0, maximum);
        spin->setValue(value);
        return spin;
    };
    const QRect bounds = m_sourceImage.rect();
    auto *left = makeSpin(0, qMax(0, bounds.width() - 1));
    auto *top = makeSpin(0, qMax(0, bounds.height() - 1));
    auto *width = makeSpin(bounds.width(), bounds.width());
    auto *height = makeSpin(bounds.height(), bounds.height());
    width->setMinimum(1);
    height->setMinimum(1);
    form->addRow(QStringLiteral("左边"), left);
    form->addRow(QStringLiteral("顶部"), top);
    form->addRow(QStringLiteral("宽度"), width);
    form->addRow(QStringLiteral("高度"), height);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    const auto updateRanges = [&] {
        width->setRange(1, qMax(1, bounds.width() - left->value()));
        height->setRange(1, qMax(1, bounds.height() - top->value()));
    };
    QObject::connect(left, qOverload<int>(&QSpinBox::valueChanged), &dialog, updateRanges);
    QObject::connect(top, qOverload<int>(&QSpinBox::valueChanged), &dialog, updateRanges);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QRect crop(QPoint(left->value(), top->value()), QSize(width->value(), height->value()));
    if (crop.isEmpty() || crop == bounds)
        return;
    m_sourceImage = m_sourceImage.copy(crop);
    m_thumbnailMode = false;
    m_scale = 1.0;
    rebuildImage();
}
void PasteWindow::applySelectionThumbnail(const QRect &selection)
{
    if (m_image.isNull())
        return;
    const QRect clipped = selection.intersected(rect());
    if (clipped.width() < 2 || clipped.height() < 2)
        return;

    const qreal ratio = m_disableDpiScaling ? 1.0 : devicePixelRatioF();
    const QRect imageRect(qRound(clipped.left() * ratio), qRound(clipped.top() * ratio),
                          qRound(clipped.width() * ratio), qRound(clipped.height() * ratio));
    const QImage cropped = m_image.copy(imageRect.intersected(m_image.rect()));
    if (cropped.isNull())
        return;

    setSourceImage(cropped);
    m_thumbnailMode = true;
    m_scaleBeforeThumbnail = 1.0;
    m_scale = 1.0;
    rebuildImage();
}
void PasteWindow::editSize()
{
    if (m_image.isNull())
        return;
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("输入贴图尺寸 / 缩放"));
    auto *form = new QFormLayout(&dialog);
    const qreal ratio = m_disableDpiScaling ? 1.0 : devicePixelRatioF();
    auto *width = new QSpinBox(&dialog);
    auto *height = new QSpinBox(&dialog);
    width->setRange(1, 20000);
    height->setRange(1, 20000);
    width->setValue(qMax(1, this->width()));
    height->setValue(qMax(1, this->height()));
    auto *scale = new QDoubleSpinBox(&dialog);
    scale->setRange(0.1, 5.0);
    scale->setDecimals(2);
    scale->setSingleStep(0.05);
    scale->setValue(m_scale);
    form->addRow(QStringLiteral("宽度:"), width);
    form->addRow(QStringLiteral("高度:"), height);
    form->addRow(QStringLiteral("缩放比例:"), scale);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    // Editing either dimension keeps the current aspect ratio; the scale
    // field mirrors the resulting physical size and can be used for precise
    // percentage based changes.
    const auto updateScaleFromWidth = [this, width, scale, ratio] {
        if (m_image.width() > 0)
            scale->setValue(qBound(0.1, qreal(width->value()) * ratio / m_image.width(), 5.0));
    };
    QObject::connect(
        width, qOverload<int>(&QSpinBox::valueChanged), &dialog, [this, width, height, updateScaleFromWidth] {
            const int targetHeight =
                qMax(1, qRound(qreal(width->value()) * m_image.height() / qMax(1, m_image.width())));
            QSignalBlocker blocker(height);
            height->setValue(targetHeight);
            updateScaleFromWidth();
        });
    if (dialog.exec() != QDialog::Accepted)
        return;
    m_scale = qBound<qreal>(0.1, scale->value(), 5.0);
    m_thumbnailMode = false;
    rebuildImage();
}
void PasteWindow::editColorCard()
{
    if (m_sourceImage.isNull())
        return;
    const QColor initial = m_sourceImage.pixelColor(0, 0);
    bool uniform = true;
    const int stepX = qMax(1, m_sourceImage.width() / 32);
    const int stepY = qMax(1, m_sourceImage.height() / 32);
    for (int y = 0; y < m_sourceImage.height() && uniform; y += stepY)
    {
        for (int x = 0; x < m_sourceImage.width(); x += stepX)
        {
            if (m_sourceImage.pixelColor(x, y) != initial)
            {
                uniform = false;
                break;
            }
        }
    }
    if (!uniform)
        return;
    const QColor selected =
        QColorDialog::getColor(initial, this, QStringLiteral("编辑颜色卡片"), QColorDialog::ShowAlphaChannel);
    if (!selected.isValid())
        return;
    QImage replacement(m_sourceImage.size(), QImage::Format_ARGB32_Premultiplied);
    replacement.fill(selected);
    m_sourceImage = std::move(replacement);
    rebuildImage();
}
void PasteWindow::contextMenuEvent(QContextMenuEvent *event)
{
    if (mouseModifierMatches(mouseAction(16, QStringLiteral("Shift")), event->modifiers()) &&
        mouseButtonMatches(mouseAction(17, QStringLiteral("\u53f3\u952e\u5355\u51fb")), Qt::RightButton,
                           false) &&
        !m_sourceText.isEmpty())
    {
        QApplication::clipboard()->setText(m_sourceText);
        event->accept();
        return;
    }

    QMenu menu;
    QAction *copyAction = menu.addAction(QStringLiteral("\u590d\u5236\u56fe\u50cf"));
    QAction *saveAction = menu.addAction(QStringLiteral("\u56fe\u50cf\u53e6\u5b58\u4e3a\u2026"));

    QMenu *outputMenu = menu.addMenu(QStringLiteral("\u8f93\u51fa\u9009\u9879"));
    const auto colorButtonStyle = [](QPushButton *button, const QColor &color) {
        button->setStyleSheet(QStringLiteral("QPushButton { background-color: %1; border: 1px solid #777; }")
                                  .arg(color.name(QColor::HexArgb)));
    };
    const auto addOutputRow =
        [this, outputMenu, colorButtonStyle](const QString &label, bool checked, int value,
                                             const QColor &color, const std::function<void(bool)> &setChecked,
                                             const std::function<void(int)> &setValue,
                                             const std::function<void(const QColor &)> &setColor) {
            auto *row = new QWidget(outputMenu);
            auto *layout = new QHBoxLayout(row);
            layout->setContentsMargins(8, 2, 8, 2);
            layout->setSpacing(5);
            auto *check = new QCheckBox(label, row);
            check->setChecked(checked);
            auto *spin = new QSpinBox(row);
            spin->setRange(0, 128);
            spin->setValue(value);
            spin->setFixedWidth(52);
            auto *button = new QPushButton(row);
            button->setFixedSize(28, 22);
            colorButtonStyle(button, color);
            layout->addWidget(check);
            layout->addStretch(1);
            layout->addWidget(spin);
            layout->addWidget(button);
            auto *action = new QWidgetAction(outputMenu);
            action->setText(label);
            action->setDefaultWidget(row);
            outputMenu->addAction(action);
            QObject::connect(check, &QCheckBox::toggled, outputMenu, setChecked);
            QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), outputMenu, setValue);
            QObject::connect(
                button, &QPushButton::clicked, outputMenu, [this, button, colorButtonStyle, setColor] {
                    const QColor selected =
                        QColorDialog::getColor(Qt::black, this, QStringLiteral("\u9009\u62e9\u989c\u8272"),
                                               QColorDialog::ShowAlphaChannel);
                    if (!selected.isValid())
                        return;
                    colorButtonStyle(button, selected);
                    setColor(selected);
                });
        };
    addOutputRow(
        QStringLiteral("\u9634\u5f71"), m_shadowEnabled, m_shadowSize, m_shadowColor,
        [this](bool enabled) { setShadowEnabled(enabled); },
        [this](int size) {
            m_shadowSize = size;
            if (auto *shadow = qobject_cast<QGraphicsDropShadowEffect *>(graphicsEffect()))
                shadow->setBlurRadius(size);
        },
        [this](const QColor &color) {
            m_shadowColor = color;
            if (auto *shadow = qobject_cast<QGraphicsDropShadowEffect *>(graphicsEffect()))
                shadow->setColor(color);
        });
    addOutputRow(
        QStringLiteral("\u8fb9\u6846"), m_borderEnabled, m_borderWidth, m_borderColor,
        [this](bool enabled) {
            m_borderEnabled = enabled;
            update();
        },
        [this](int width) {
            m_borderWidth = qMax(0, width);
            update();
        },
        [this](const QColor &color) {
            m_borderColor = color;
            update();
        });

    QAction *extractTextAction = menu.addAction(QStringLiteral("\u63d0\u53d6\u6587\u5b57\u5e76\u590d\u5236"));
    QAction *helpKeysAction = nullptr;
    if (m_helpKeys)
    {
        menu.addSeparator();
        helpKeysAction = menu.addAction(QStringLiteral("快捷键帮助"));
    }
    menu.addSeparator();
    QAction *toolbarAction = menu.addAction(QStringLiteral("\u663e\u793a\u5de5\u5177\u6761"));
    toolbarAction->setCheckable(true);
    toolbarAction->setChecked(m_showToolbar);
    QAction *annotationsAction = menu.addAction(QStringLiteral("\u663e\u793a\u6807\u6ce8"));
    annotationsAction->setCheckable(true);
    annotationsAction->setChecked(m_showAnnotations);
    QAction *shadowAction = menu.addAction(QStringLiteral("\u7a97\u53e3\u9634\u5f71"));
    shadowAction->setCheckable(true);
    shadowAction->setChecked(m_shadowEnabled);
    QAction *alwaysOnTopAction = menu.addAction(QStringLiteral("\u4fdd\u6301\u6700\u4e0a\u5c42"));
    alwaysOnTopAction->setCheckable(true);
    alwaysOnTopAction->setChecked(m_alwaysOnTop);

    menu.addSeparator();
    QMenu *scaleMenu = menu.addMenu(QStringLiteral("\u7f29\u653e"));
    QActionGroup *scaleGroup = new QActionGroup(scaleMenu);
    scaleGroup->setExclusive(true);
    const auto addScale = [scaleMenu, scaleGroup, this](const QString &text, qreal scale) {
        auto *action = scaleMenu->addAction(text);
        action->setCheckable(true);
        action->setChecked(qAbs(m_scale - scale) < 0.001);
        scaleGroup->addAction(action);
        return action;
    };
    QAction *scale333 = addScale(QStringLiteral("33.3%"), 0.333);
    QAction *scale50 = addScale(QStringLiteral("50%"), 0.5);
    QAction *scale80 = addScale(QStringLiteral("80%"), 0.8);
    QAction *scale100 = addScale(QStringLiteral("100%"), 1.0);
    QAction *scale200 = addScale(QStringLiteral("200%"), 2.0);
    QAction *customScaleAction = scaleMenu->addAction(QStringLiteral("\u81ea\u5b9a\u4e49\u2026"));
    scaleMenu->addSeparator();
    QAction *currentScaleAction = scaleMenu->addAction(
        QStringLiteral("%1% \u5f53\u524d").arg(QString::number(m_scale * 100.0, 'f', 1)));
    currentScaleAction->setCheckable(true);
    currentScaleAction->setChecked(true);
    currentScaleAction->setEnabled(false);
    scaleMenu->addSeparator();
    QAction *smoothAction = scaleMenu->addAction(QStringLiteral("\u5e73\u6ed1\u5904\u7406"));
    smoothAction->setCheckable(true);
    smoothAction->setChecked(m_smoothScale);

    QMenu *backgroundMenu = menu.addMenu(QStringLiteral("\u80cc\u666f\u6a21\u5f0f"));
    QActionGroup *backgroundGroup = new QActionGroup(backgroundMenu);
    backgroundGroup->setExclusive(true);
    const auto addBackground = [this, backgroundMenu, backgroundGroup](const QString &label,
                                                                       const QString &mode) {
        auto *action = backgroundMenu->addAction(label);
        action->setCheckable(true);
        action->setChecked(m_backgroundMode == mode);
        backgroundGroup->addAction(action);
        return action;
    };
    QAction *transparentAction =
        addBackground(QStringLiteral("\u900f\u660e"), QStringLiteral("\u900f\u660e"));
    QAction *fakeTransparentAction =
        addBackground(QStringLiteral("\u4f2a\u900f\u660e"), QStringLiteral("\u4f2a\u900f\u660e"));
    QAction *darkCheckerAction =
        addBackground(QStringLiteral("\u6697\u8272\u68cb\u76d8"), QStringLiteral("\u9ed1\u8272"));
    QAction *lightCheckerAction =
        addBackground(QStringLiteral("\u6d45\u8272\u68cb\u76d8"), QStringLiteral("\u767d\u8272"));
    QAction *customBackgroundAction = addBackground(QStringLiteral("\u81ea\u5b9a\u4e49\u989c\u8272"),
                                                    QStringLiteral("\u81ea\u5b9a\u4e49\u989c\u8272"));

    QMenu *processingMenu = menu.addMenu(QStringLiteral("\u56fe\u50cf\u5904\u7406"));
    QAction *rotateLeftAction = processingMenu->addAction(QStringLiteral("\u5411\u5de6\u65cb\u8f6c"));
    QAction *rotateRightAction = processingMenu->addAction(QStringLiteral("\u5411\u53f3\u65cb\u8f6c"));
    QAction *flipHorizontalAction = processingMenu->addAction(QStringLiteral("\u6c34\u5e73\u7ffb\u8f6c"));
    QAction *flipVerticalAction = processingMenu->addAction(QStringLiteral("\u5782\u76f4\u7ffb\u8f6c"));
    QAction *grayscaleAction = processingMenu->addAction(QStringLiteral("\u7070\u5ea6\u663e\u793a"));
    grayscaleAction->setCheckable(true);
    grayscaleAction->setChecked(m_grayscale);
    QAction *invertedAction = processingMenu->addAction(QStringLiteral("\u53cd\u8272\u663e\u793a"));
    invertedAction->setCheckable(true);
    invertedAction->setChecked(m_inverted);

    menu.addSeparator();
    QAction *pasteAction = menu.addAction(QStringLiteral("\u7c98\u8d34"));
    QAction *replaceFileAction = menu.addAction(QStringLiteral("\u66ff\u6362\u4e3a\u6587\u4ef6\u2026"));
    QMenu *groupMenu = menu.addMenu(QStringLiteral("\u79fb\u52a8\u5230\u5206\u7ec4"));
    if (m_pasteManager)
    {
        const QStringList groupNames = m_pasteManager->groupNames();
        const int currentGroup = m_pasteManager->groupIndexOf(this);
        QActionGroup *groupActions = new QActionGroup(groupMenu);
        groupActions->setExclusive(true);
        for (int index = 0; index < groupNames.size(); ++index)
        {
            QAction *action =
                groupMenu->addAction(QStringLiteral("%1  %2").arg(groupNames.at(index)).arg(index + 1));
            action->setCheckable(true);
            action->setChecked(index == currentGroup);
            action->setData(index);
            groupActions->addAction(action);
        }
    }
    QAction *newGroupAction = groupMenu->addAction(QStringLiteral("\u65b0\u5206\u7ec4\u2026"));
    QAction *viewFolderAction = menu.addAction(QStringLiteral("\u5728\u6587\u4ef6\u5939\u4e2d\u67e5\u770b"));
    viewFolderAction->setEnabled(false);
    menu.addSeparator();
    QAction *closeAction = menu.addAction(QStringLiteral("\u5173\u95ed"));
    QAction *destroyAction = menu.addAction(QStringLiteral("\u9500\u6bc1"));
    QMenu *infoMenu = menu.addMenu(QStringLiteral("%1 \u00d7 %2").arg(width()).arg(height()));
    if (m_pasteManager)
    {
        const int group = m_pasteManager->groupIndexOf(this);
        const QStringList names = m_pasteManager->groupNames();
        const QString name = group >= 0 && group < names.size() ? names.at(group) : QStringLiteral("Default");
        infoMenu->addAction(QStringLiteral("\u6240\u5728\u7ec4: %1").arg(name))->setEnabled(false);
    }
    else
    {
        infoMenu->addAction(QStringLiteral("\u6240\u5728\u7ec4: Default"))->setEnabled(false);
    }
    infoMenu->addAction(QStringLiteral("\u7f29\u653e: %1%").arg(QString::number(m_scale * 100.0, 'f', 1)))
        ->setEnabled(false);
    infoMenu->addAction(QStringLiteral("\u65cb\u8f6c: %1\u00b0").arg(m_rotationQuarterTurns * 90))
        ->setEnabled(false);
    infoMenu->addAction(QStringLiteral("\u4e0d\u900f\u660e\u5ea6: %1%").arg(qRound(windowOpacity() * 100.0)))
        ->setEnabled(false);
    infoMenu
        ->addAction(QStringLiteral("\u6c34\u5e73\u955c\u50cf: %1")
                        .arg(m_flipHorizontal ? QStringLiteral("\u662f") : QStringLiteral("\u5426")))
        ->setEnabled(false);
    infoMenu
        ->addAction(QStringLiteral("\u5782\u76f4\u955c\u50cf: %1")
                        .arg(m_flipVertical ? QStringLiteral("\u662f") : QStringLiteral("\u5426")))
        ->setEnabled(false);
    infoMenu
        ->addAction(QStringLiteral("\u7070\u5ea6\u663e\u793a: %1")
                        .arg(m_grayscale ? QStringLiteral("\u662f") : QStringLiteral("\u5426")))
        ->setEnabled(false);
    infoMenu
        ->addAction(QStringLiteral("\u53cd\u8272\u663e\u793a: %1")
                        .arg(m_inverted ? QStringLiteral("\u662f") : QStringLiteral("\u5426")))
        ->setEnabled(false);

    QAction *selected = menu.exec(event->globalPos());
    if (selected == copyAction)
    {
        if (m_pasteManager)
            emit copyRequested(this);
        else
            copyImageToClipboard(m_image, false, ClipboardImageSource::Paste);
    }
    else if (selected == saveAction)
    {
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("\u56fe\u50cf\u53e6\u5b58\u4e3a"), QString(),
            QStringLiteral("PNG \u56fe\u50cf (*.png);;\u6240\u6709\u6587\u4ef6 (*)"));
        if (!path.isEmpty())
            m_image.save(path);
    }
    else if (selected == extractTextAction)
    {
        if (!m_sourceText.isEmpty())
            QApplication::clipboard()->setText(m_sourceText);
    }
    else if (selected == helpKeysAction)
    {
        QMessageBox::information(this, QStringLiteral("贴图快捷键"),
                                 QStringLiteral("Ctrl/Command+C 复制，Ctrl/Command+Alt+S 复制为文件，"
                                                "滚轮缩放，Esc 执行关闭操作。"));
    }
    else if (selected == toolbarAction)
    {
        m_showToolbar = toolbarAction->isChecked();
    }
    else if (selected == annotationsAction)
    {
        m_showAnnotations = annotationsAction->isChecked();
        update();
    }
    else if (selected == shadowAction)
    {
        setShadowEnabled(shadowAction->isChecked());
    }
    else if (selected == alwaysOnTopAction)
    {
        setAlwaysOnTop(alwaysOnTopAction->isChecked());
    }
    else if (selected == scale333)
    {
        m_scale = 0.333;
        rebuildImage();
    }
    else if (selected == scale50)
    {
        m_scale = 0.5;
        rebuildImage();
    }
    else if (selected == scale80)
    {
        m_scale = 0.8;
        rebuildImage();
    }
    else if (selected == scale100)
    {
        m_scale = 1.0;
        rebuildImage();
    }
    else if (selected == scale200)
    {
        m_scale = 2.0;
        rebuildImage();
    }
    else if (selected == customScaleAction)
    {
        bool ok = false;
        const double percentage = QInputDialog::getDouble(
            this, QStringLiteral("\u81ea\u5b9a\u4e49\u7f29\u653e"),
            QStringLiteral("\u7f29\u653e\u767e\u5206\u6bd4:"), m_scale * 100.0, 10.0, 500.0, 1, &ok);
        if (ok)
        {
            m_scale = percentage / 100.0;
            rebuildImage();
        }
    }
    else if (selected == smoothAction)
    {
        setSmoothScale(smoothAction->isChecked());
    }
    else if (selected == transparentAction)
    {
        setBackgroundMode(QStringLiteral("\u900f\u660e"));
    }
    else if (selected == fakeTransparentAction)
    {
        setBackgroundMode(QStringLiteral("\u4f2a\u900f\u660e"));
    }
    else if (selected == darkCheckerAction)
    {
        setBackgroundMode(QStringLiteral("\u9ed1\u8272"));
    }
    else if (selected == lightCheckerAction)
    {
        setBackgroundMode(QStringLiteral("\u767d\u8272"));
    }
    else if (selected == customBackgroundAction)
    {
        const QColor color = QColorDialog::getColor(
            m_customBackgroundColor, this, QStringLiteral("\u81ea\u5b9a\u4e49\u80cc\u666f\u989c\u8272"),
            QColorDialog::ShowAlphaChannel);
        if (color.isValid())
        {
            m_customBackgroundColor = color;
            setBackgroundMode(QStringLiteral("\u81ea\u5b9a\u4e49\u989c\u8272"));
        }
    }
    else if (selected == rotateLeftAction)
    {
        m_rotationQuarterTurns = (m_rotationQuarterTurns + 3) % 4;
        rebuildImage();
    }
    else if (selected == rotateRightAction)
    {
        m_rotationQuarterTurns = (m_rotationQuarterTurns + 1) % 4;
        rebuildImage();
    }
    else if (selected == flipHorizontalAction)
    {
        m_flipHorizontal = !m_flipHorizontal;
        rebuildImage();
    }
    else if (selected == flipVerticalAction)
    {
        m_flipVertical = !m_flipVertical;
        rebuildImage();
    }
    else if (selected == grayscaleAction)
    {
        m_grayscale = grayscaleAction->isChecked();
        rebuildImage();
    }
    else if (selected == invertedAction)
    {
        m_inverted = invertedAction->isChecked();
        rebuildImage();
    }
    else if (selected == pasteAction)
    {
        const QImage image = QApplication::clipboard()->image();
        if (!image.isNull())
            setSourceImage(image);
    }
    else if (selected == replaceFileAction)
    {
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("\u66ff\u6362\u4e3a\u6587\u4ef6"), QString(),
            QStringLiteral(
                "\u56fe\u50cf\u6587\u4ef6 (*.png *.jpg *.jpeg *.bmp *.gif);;\u6240\u6709\u6587\u4ef6 (*)"));
        QImage image;
        if (!path.isEmpty() && image.load(path))
            setSourceImage(image);
    }
    else if (selected == newGroupAction && m_pasteManager)
    {
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("\u65b0\u5206\u7ec4"),
                                                   QStringLiteral("\u5206\u7ec4\u540d\u79f0:"),
                                                   QLineEdit::Normal, QString(), &ok);
        if (ok)
        {
            const int index = m_pasteManager->createGroup(name);
            m_pasteManager->moveWindowToGroup(this, index);
        }
    }
    else if (selected && selected->parent() == groupMenu && m_pasteManager && selected->data().isValid())
    {
        m_pasteManager->moveWindowToGroup(this, selected->data().toInt());
    }
    else if (selected == closeAction)
    {
        if (m_closeConfirm && m_pasteManager && m_pasteManager->selectedCount() > 1 &&
            QMessageBox::question(this, QStringLiteral("关闭贴图"),
                                  QStringLiteral("确认关闭选中的多个贴图吗？"),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        hidePaste();
    }
    else if (selected == destroyAction)
    {
        if (m_destroyConfirm && m_pasteManager && m_pasteManager->selectedCount() > 1 &&
            QMessageBox::question(this, QStringLiteral("销毁贴图"),
                                  QStringLiteral("确认销毁选中的多个贴图吗？"),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        close();
    }
}
} // namespace snipaste::ui
