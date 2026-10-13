#include "ui/paste/paste_window.h"
#include "ui/paste/paste_manager.h"

#include "ui/annotation/annotation_dialog.h"
#include "platform/keyboard.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QColorDialog>
#include <QCursor>
#include <QDateTime>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QDir>
#include <QFileDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QActionGroup>
#include <QWidgetAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QCheckBox>
#include <QPushButton>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QFileInfo>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QSpinBox>
#include <QStandardPaths>
#include <QMimeData>
#include <QUrl>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QWheelEvent>
#include <QLineEdit>
#include <functional>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
#include "platform/macos/system.h"
#endif

namespace snipaste::ui {

#ifdef Q_OS_WIN
namespace {
struct __declspec(uuid("B5A399E7-1C87-46B8-88E9-FC5747B171BD")) VirtualDesktopPinnedAppsClass;
struct __declspec(uuid("4CE81583-1E4C-4632-A621-07A53543148F")) VirtualDesktopPinnedAppsInterface : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE IsAppIdPinned(void *, BOOL *) = 0;
    virtual HRESULT STDMETHODCALLTYPE PinAppID(void *) = 0;
    virtual HRESULT STDMETHODCALLTYPE UnpinAppID(void *) = 0;
    virtual HRESULT STDMETHODCALLTYPE IsWindowPinned(HWND, BOOL *) = 0;
    virtual HRESULT STDMETHODCALLTYPE PinWindow(HWND) = 0;
    virtual HRESULT STDMETHODCALLTYPE UnpinWindow(HWND) = 0;
};

void pinWindowToVirtualDesktops(HWND window, bool enabled)
{
    if (!window)
        return;
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    VirtualDesktopPinnedAppsInterface *apps = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(VirtualDesktopPinnedAppsClass), nullptr,
                                   CLSCTX_INPROC_SERVER,
                                   __uuidof(VirtualDesktopPinnedAppsInterface),
                                   reinterpret_cast<void **>(&apps))) && apps) {
        if (enabled)
            apps->PinWindow(window);
        else
            apps->UnpinWindow(window);
        apps->Release();
    }
    if (SUCCEEDED(init))
        CoUninitialize();
}
} // namespace
#endif

PasteWindow::PasteWindow(QImage image, QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_QuitOnClose, false);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::ArrowCursor);
    setAcceptDrops(true);

    auto *shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(m_shadowSize);
    shadow->setOffset(0, 3);
    shadow->setColor(m_shadowColor);
    setGraphicsEffect(shadow);

    setSourceImage(image);
}

void PasteWindow::showAtCursor()
{
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
        screen = QGuiApplication::primaryScreen();

    if (screen) {
        setScreen(screen);
        updateDisplaySize();
        const QRect available = screen->availableGeometry();
        move(available.center() - QPoint(width() / 2, height() / 2));
    }
    showExisting();
}

void PasteWindow::showExisting()
{
    if (m_state != State::Active)
        return;
    // Re-apply the flag before every show. Windows can drop top-level flags
    // when input transparency or a restored transform changes the native
    // window, which otherwise lets another application's window cover a pin.
    Qt::WindowFlags flags = windowFlags();
    flags.setFlag(Qt::WindowStaysOnTopHint, m_alwaysOnTop);
    if (flags != windowFlags())
        setWindowFlags(flags);
    show();
#ifdef Q_OS_WIN
    if (m_virtualDesktopPinned)
        pinWindowToVirtualDesktops(reinterpret_cast<HWND>(winId()), true);
#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    // A newly shown QWidget may own a different NSWindow after flags or
    // screen changes, so apply the Space policy after show() each time.
    snipaste::platform::setMacWindowCollectionBehavior(winId(), m_virtualDesktopPinned);
#endif
    updateDisplaySize();
    activateWindow();
    raise();
    setFocus();
}

void PasteWindow::showAtPosition(const QPoint &globalPosition)
{
    QScreen *targetScreen = QGuiApplication::screenAt(globalPosition);
    if (targetScreen)
        setScreen(targetScreen);
    updateDisplaySize();
    move(globalPosition);
    showExisting();
}

PasteWindow::State PasteWindow::state() const
{
    return m_state;
}

void PasteWindow::restoreClosed()
{
    if (m_state != State::Closed)
        return;
    m_state = State::Active;
    m_soloHidden = false;
    showExisting();
}

const QImage &PasteWindow::sourceImage() const
{
    return m_sourceImage;
}

const QImage &PasteWindow::displayedImage() const
{
    return m_image;
}

void PasteWindow::setSourceText(const QString &text)
{
    m_sourceText = text;
}

const QString &PasteWindow::sourceText() const
{
    return m_sourceText;
}

bool PasteWindow::clickThrough() const
{
    return m_clickThrough;
}

void PasteWindow::setShadowEnabled(bool enabled)
{
    m_shadowEnabled = enabled;
    if (auto *shadow = qobject_cast<QGraphicsDropShadowEffect *>(graphicsEffect()))
        shadow->setEnabled(enabled);
    update();
}

bool PasteWindow::shadowEnabled() const
{
    return m_shadowEnabled;
}

void PasteWindow::setResizeEnabled(bool enabled)
{
    m_resizeEnabled = enabled;
    if (!enabled && !m_pressed)
        setCursor(Qt::ArrowCursor);
}

bool PasteWindow::resizeEnabled() const
{
    return m_resizeEnabled;
}

void PasteWindow::setExchangeNavigationKeys(bool enabled)
{
    m_exchangeNavigationKeys = enabled;
}

void PasteWindow::setMaxNetworkImageBytes(qint64 bytes)
{
    m_maxNetworkImageBytes = qBound<qint64>(0, bytes, 1024LL * 1024 * 1024);
}

void PasteWindow::setClickThrough(bool enabled)
{
    if (m_clickThrough == enabled)
        return;
    m_clickThrough = enabled;
    Qt::WindowFlags flags = windowFlags();
    flags.setFlag(Qt::WindowTransparentForInput, enabled);
    setWindowFlags(flags);
    showExisting();
}

bool PasteWindow::isSoloHidden() const
{
    return m_soloHidden;
}

void PasteWindow::setSoloHidden(bool hidden)
{
    m_soloHidden = hidden;
    if (hidden)
        hide();
    else
        showExisting();
}

int PasteWindow::rotationQuarterTurns() const { return m_rotationQuarterTurns; }
bool PasteWindow::flipHorizontal() const { return m_flipHorizontal; }
bool PasteWindow::flipVertical() const { return m_flipVertical; }
bool PasteWindow::grayscale() const { return m_grayscale; }
bool PasteWindow::inverted() const { return m_inverted; }
qreal PasteWindow::scale() const { return m_scale; }
bool PasteWindow::transparentBackground() const { return m_transparentBackground; }
QString PasteWindow::backgroundMode() const { return m_backgroundMode; }

void PasteWindow::setBackgroundMode(const QString &mode)
{
    const QString normalized = mode.trimmed();
    if (normalized == QStringLiteral("透明") || normalized == QStringLiteral("白色")
        || normalized == QStringLiteral("黑色") || normalized == QStringLiteral("伪透明")
        || normalized == QStringLiteral("自定义颜色"))
        m_backgroundMode = normalized;
    else
        m_backgroundMode = QStringLiteral("伪透明");
    m_transparentBackground = m_backgroundMode == QStringLiteral("透明");
    update();
}

void PasteWindow::setSmoothScale(bool enabled)
{
    m_smoothScale = enabled;
    update();
}

void PasteWindow::setEscapeActions(const QString &escapeAction, const QString &shiftEscapeAction)
{
    m_escapeAction = escapeAction.trimmed();
    m_shiftEscapeAction = shiftEscapeAction.trimmed();
}

void PasteWindow::setMouseActions(const QStringList &actions)
{
    m_mouseActions = actions;
}

QString PasteWindow::mouseAction(int index, const QString &fallback) const
{
    if (index >= 0 && index < m_mouseActions.size() && !m_mouseActions.at(index).trimmed().isEmpty())
        return m_mouseActions.at(index).trimmed();
    return fallback;
}

bool PasteWindow::mouseModifierMatches(const QString &value, Qt::KeyboardModifiers modifiers) const
{
    const QString expected = value.trimmed();
    const Qt::KeyboardModifiers actual = modifiers & (Qt::ControlModifier
                                                       | Qt::MetaModifier
                                                       | Qt::ShiftModifier
                                                       | Qt::AltModifier);
    if (expected.isEmpty() || expected == QStringLiteral("无"))
        return actual == Qt::NoModifier;
    if (expected == QStringLiteral("Ctrl"))
        return actual == Qt::ControlModifier;
    if (expected == QStringLiteral("Command") || expected == QStringLiteral("Cmd"))
        return actual == snipaste::platform::primaryModifier();
    if (expected == QStringLiteral("Shift"))
        return actual == Qt::ShiftModifier;
    if (expected == QStringLiteral("Alt"))
        return actual == Qt::AltModifier;
    return false;
}

bool PasteWindow::mouseButtonMatches(const QString &value, Qt::MouseButton button, bool doubleClick) const
{
    const QString expected = value.trimmed();
    if (doubleClick)
        return (button == Qt::LeftButton && expected == QStringLiteral("左键双击"));
    if (button == Qt::MiddleButton)
        return expected == QStringLiteral("中键单击");
    if (button == Qt::RightButton)
        return expected == QStringLiteral("右键单击")
               || expected == QStringLiteral("鼠标右键拖拽");
    return false;
}

void PasteWindow::setVirtualDesktopPinned(bool enabled)
{
    m_virtualDesktopPinned = enabled;
#ifdef Q_OS_WIN
    if (isVisible())
        pinWindowToVirtualDesktops(reinterpret_cast<HWND>(winId()), enabled);
#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    if (isVisible())
        snipaste::platform::setMacWindowCollectionBehavior(winId(), enabled);
#endif
}

bool PasteWindow::virtualDesktopPinned() const
{
    return m_virtualDesktopPinned;
}

void PasteWindow::setPasteManager(PasteManager *manager)
{
    m_pasteManager = manager;
}

bool PasteWindow::alwaysOnTop() const { return m_alwaysOnTop; }

bool PasteWindow::thumbnailMode() const { return m_thumbnailMode; }

void PasteWindow::scaleBy(qreal factor)
{
    changeScale(factor);
}

void PasteWindow::adjustOpacity(qreal delta)
{
    setWindowOpacity(qBound<qreal>(0.1, windowOpacity() + delta, 1.0));
}

void PasteWindow::setThumbnailMode(bool enabled)
{
    if (enabled != m_thumbnailMode)
        toggleThumbnailMode();
}

void PasteWindow::restoreTransform(int rotationQuarterTurns,
                                   bool flipHorizontal,
                                   bool flipVertical,
                                   bool grayscale,
                                   bool inverted,
                                   qreal scale,
                                   bool transparentBackground,
                                   bool alwaysOnTop)
{
    m_rotationQuarterTurns = ((rotationQuarterTurns % 4) + 4) % 4;
    m_flipHorizontal = flipHorizontal;
    m_flipVertical = flipVertical;
    m_grayscale = grayscale;
    m_inverted = inverted;
    m_scale = qBound<qreal>(0.1, scale, 5.0);
    setBackgroundMode(transparentBackground ? QStringLiteral("透明") : QStringLiteral("伪透明"));
    m_alwaysOnTop = alwaysOnTop;
    rebuildImage();
    Qt::WindowFlags flags = windowFlags();
    flags.setFlag(Qt::WindowStaysOnTopHint, m_alwaysOnTop);
    setWindowFlags(flags);
}

bool PasteWindow::event(QEvent *event)
{
    const bool handled = QWidget::event(event);
    if (event->type() == QEvent::DevicePixelRatioChange)
        updateDisplaySize();
    return handled;
}

void PasteWindow::closeEvent(QCloseEvent *event)
{
    // QWidget::close is permanent destruction; Esc/double-click use hidePaste.
    m_state = State::Destroyed;
    QWidget::closeEvent(event);
}

void PasteWindow::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    // At 100% the backing store already has one device pixel for every
    // source pixel.  Disabling interpolation in that case keeps fine text
    // and screenshot edges crisp; interpolation remains enabled for an
    // intentional zoom or a non-native size.
    const qreal ratio = devicePixelRatioF();
    const bool nativeSize = qAbs(qreal(width()) * ratio - qreal(m_image.width())) < 0.5
        && qAbs(qreal(height()) * ratio - qreal(m_image.height())) < 0.5;
    painter.setRenderHint(QPainter::SmoothPixmapTransform, m_smoothScale && !nativeSize);
    if (m_backgroundMode == QStringLiteral("白色"))
        painter.fillRect(rect(), QColor(255, 255, 255, 245));
    else if (m_backgroundMode == QStringLiteral("黑色"))
        painter.fillRect(rect(), QColor(0, 0, 0, 245));
    else if (m_backgroundMode == QStringLiteral("自定义颜色"))
        painter.fillRect(rect(), m_customBackgroundColor);
    else if (m_backgroundMode != QStringLiteral("透明"))
        painter.fillRect(rect(), QColor(255, 255, 255, 245));
    painter.drawImage(rect(), m_image);

    if (m_selectionThumbnailDragging && m_selectionThumbnailMoved) {
        const QRect selection = QRect(m_selectionThumbnailOrigin,
                                      m_selectionThumbnailCurrent).normalized();
        QPen selectionPen(QColor(255, 210, 70, 235), 1, Qt::DashLine);
        painter.setPen(selectionPen);
        painter.setBrush(Qt::NoBrush);
        const QRect clipped = selection.intersected(rect());
        if (!clipped.isEmpty())
            painter.drawRect(clipped.adjusted(0, 0, -1, -1));
    }

    if (m_borderEnabled) {
        QPen border(m_borderColor);
        border.setWidth(m_borderWidth);
        painter.setPen(border);
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
    }
}

void PasteWindow::setSourceImage(const QImage &image)
{
    if (image.isNull())
        return;

    m_sourceImage = image;
    m_sourceText.clear();

    m_rotationQuarterTurns = 0;
    m_flipHorizontal = false;
    m_flipVertical = false;
    m_grayscale = false;
    m_inverted = false;
    m_scale = 1.0;
    rebuildImage();
}

void PasteWindow::rebuildImage()
{
    if (m_sourceImage.isNull())
        return;

    QImage image = m_sourceImage;
    if (m_rotationQuarterTurns != 0)
        image = image.transformed(QTransform().rotate(90 * m_rotationQuarterTurns),
                                  Qt::SmoothTransformation);
    if (m_flipHorizontal || m_flipVertical) {
        Qt::Orientations orientations;
        if (m_flipHorizontal)
            orientations |= Qt::Horizontal;
        if (m_flipVertical)
            orientations |= Qt::Vertical;
        image = image.flipped(orientations);
    }
    if (m_grayscale)
        image = image.convertToFormat(QImage::Format_Grayscale8);
    if (m_inverted)
        image.invertPixels();

    // Keep the displayed buffer in physical pixel coordinates.  The source
    // image may carry clipboard DPI metadata, but that metadata must not make
    // QPainter reinterpret the pixels and resample a native-size pin.
    image.setDevicePixelRatio(1);
    m_image = std::move(image);
    updateDisplaySize();
    update();
}

void PasteWindow::updateDisplaySize()
{
    if (m_image.isNull())
        return;
    // Image dimensions are physical pixels, QWidget geometry is in logical
    // pixels. Clipboard/PNG round trips may discard the image's DPR metadata.
    // 100% always means one image pixel per display pixel on the current screen.
    const qreal ratio = devicePixelRatioF();
    resize(qMax(1, qRound(m_image.width() * m_scale / ratio)),
           qMax(1, qRound(m_image.height() * m_scale / ratio)));
}

void PasteWindow::hidePaste()
{
    if (m_state != State::Active)
        return;
    // Thumbnail mode is a transient viewing aid.  Returning a pin to the
    // hidden/closed state must keep its normal transform so that reviving it
    // does not unexpectedly change its size.
    if (m_thumbnailMode) {
        m_thumbnailMode = false;
        m_scale = qBound<qreal>(0.1, m_scaleBeforeThumbnail, 5.0);
        rebuildImage();
    }
    m_pressed = false;
    m_dragging = false;
    setCursor(Qt::ArrowCursor);
    m_state = State::Closed;
    hide();
    emit pasteClosed(this);
}

void PasteWindow::changeScale(qreal factor)
{
    if (m_thumbnailMode)
        m_thumbnailMode = false;
    m_scale = qBound<qreal>(0.1, m_scale * factor, 5.0);
    rebuildImage();
}

void PasteWindow::toggleThumbnailMode()
{
    if (m_sourceImage.isNull())
        return;
    if (m_thumbnailMode) {
        m_thumbnailMode = false;
        m_scale = qBound<qreal>(0.1, m_scaleBeforeThumbnail, 5.0);
    } else {
        m_scaleBeforeThumbnail = m_scale;
        const qreal longest = qMax(m_image.width(), m_image.height());
        const qreal target = 240.0 * devicePixelRatioF();
        m_scale = longest > 0 ? qBound<qreal>(0.1, target / longest, 5.0) : 1.0;
        m_thumbnailMode = true;
    }
    rebuildImage();
}

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

    const qreal ratio = devicePixelRatioF();
    const QRect imageRect(qRound(clipped.left() * ratio),
                          qRound(clipped.top() * ratio),
                          qRound(clipped.width() * ratio),
                          qRound(clipped.height() * ratio));
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
    const qreal ratio = devicePixelRatioF();
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
    QObject::connect(width, qOverload<int>(&QSpinBox::valueChanged), &dialog, [this, width, height, updateScaleFromWidth] {
        const int targetHeight = qMax(1, qRound(qreal(width->value()) * m_image.height() / qMax(1, m_image.width())));
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
    for (int y = 0; y < m_sourceImage.height() && uniform; y += stepY) {
        for (int x = 0; x < m_sourceImage.width(); x += stepX) {
            if (m_sourceImage.pixelColor(x, y) != initial) {
                uniform = false;
                break;
            }
        }
    }
    if (!uniform)
        return;
    const QColor selected = QColorDialog::getColor(initial, this, QStringLiteral("编辑颜色卡片"),
                                                    QColorDialog::ShowAlphaChannel);
    if (!selected.isValid())
        return;
    QImage replacement(m_sourceImage.size(), QImage::Format_ARGB32_Premultiplied);
    replacement.fill(selected);
    m_sourceImage = std::move(replacement);
    rebuildImage();
}

void PasteWindow::setAlwaysOnTop(bool enabled)
{
    if (m_alwaysOnTop == enabled)
        return;
    m_alwaysOnTop = enabled;
    Qt::WindowFlags flags = windowFlags();
    flags.setFlag(Qt::WindowStaysOnTopHint, enabled);
    setWindowFlags(flags);
    showExisting();
}

void PasteWindow::toggleBackgroundMode()
{
    setBackgroundMode(m_transparentBackground ? QStringLiteral("伪透明") : QStringLiteral("透明"));
}

void PasteWindow::mousePressEvent(QMouseEvent *event)
{
    if (mouseModifierMatches(mouseAction(10, QStringLiteral("无")), event->modifiers())
        && mouseButtonMatches(mouseAction(11, QStringLiteral("中键单击")),
                              event->button(), false)) {
        // The default middle-click action resets a pin's transform and
        // opacity while preserving its position and background mode.
        m_rotationQuarterTurns = 0;
        m_flipHorizontal = false;
        m_flipVertical = false;
        m_grayscale = false;
        m_inverted = false;
        m_thumbnailMode = false;
        m_scale = 1.0;
        rebuildImage();
        setWindowOpacity(1.0);
        emit activated(this, event->modifiers());
        event->accept();
        return;
    }
    if (mouseModifierMatches(mouseAction(14, QStringLiteral("无")), event->modifiers())
        && mouseAction(15, QStringLiteral("鼠标右键拖拽")) == QStringLiteral("鼠标右键拖拽")
        && event->button() == Qt::RightButton) {
        m_selectionThumbnailCandidate = true;
        m_selectionThumbnailDragging = false;
        m_selectionThumbnailMoved = false;
        m_selectionThumbnailOrigin = event->position().toPoint();
        m_selectionThumbnailCurrent = m_selectionThumbnailOrigin;
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton) {
        emit activated(this, event->modifiers());
        m_pressed = true;
        m_dragging = false;
        m_pressPosition = event->globalPosition().toPoint();
        m_resizeEdges = m_resizeEnabled ? resizeEdgesAt(event->position().toPoint()) : Qt::Edges{};
        // An empty edge set means a normal drag.  The previous boolean
        // conversion inverted that meaning, so clicks in the middle of a
        // paste window were treated as resize gestures and never moved it.
        m_resizing = m_resizeEdges != Qt::Edges{};
        m_resizeStartGeometry = frameGeometry();
        m_resizeStartGlobal = m_pressPosition;
        m_dragOffset = m_pressPosition - frameGeometry().topLeft();
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void PasteWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (m_selectionThumbnailCandidate && event->buttons().testFlag(Qt::RightButton)) {
        m_selectionThumbnailCurrent = event->position().toPoint();
        if ((m_selectionThumbnailCurrent - m_selectionThumbnailOrigin).manhattanLength()
            >= QApplication::startDragDistance()) {
            m_selectionThumbnailDragging = true;
            m_selectionThumbnailMoved = true;
        }
        if (m_selectionThumbnailDragging) {
            update();
            event->accept();
            return;
        }
    }
    if (!m_pressed) {
        updateResizeCursor(event->position().toPoint());
        QWidget::mouseMoveEvent(event);
        return;
    }
    if (m_resizing && event->buttons().testFlag(Qt::LeftButton)) {
        resizeFromPointer(event->globalPosition().toPoint());
        event->accept();
        return;
    }
    if (m_pressed && event->buttons().testFlag(Qt::LeftButton)) {
        const QPoint globalPosition = event->globalPosition().toPoint();
        if (!m_dragging
            && (globalPosition - m_pressPosition).manhattanLength() >= QApplication::startDragDistance()) {
            m_dragging = true;
        }
        if (m_dragging)
            move(globalPosition - m_dragOffset);
        event->accept();
        return;
    }

    QWidget::mouseMoveEvent(event);
}

void PasteWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (mouseModifierMatches(mouseAction(12, QStringLiteral("Shift")), event->modifiers())
        && mouseButtonMatches(mouseAction(13, QStringLiteral("左键双击")),
                              event->button(), true)) {
        toggleThumbnailMode();
        event->accept();
        return;
    }
    if (mouseModifierMatches(mouseAction(8, QStringLiteral("无")), event->modifiers())
        && mouseButtonMatches(mouseAction(9, QStringLiteral("左键双击")),
                              event->button(), true)) {
        hidePaste();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void PasteWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton && m_selectionThumbnailCandidate) {
        const bool dragged = m_selectionThumbnailDragging && m_selectionThumbnailMoved;
        const QRect selection = QRect(m_selectionThumbnailOrigin,
                                      m_selectionThumbnailCurrent).normalized();
        m_selectionThumbnailCandidate = false;
        m_selectionThumbnailDragging = false;
        m_selectionThumbnailMoved = false;
        if (dragged) {
            applySelectionThumbnail(selection);
            update();
            event->accept();
            return;
        }
        update();
        // A right click without movement remains a normal context-menu click.
    }
    if (event->button() == Qt::LeftButton && m_pressed) {
        const bool wasDragging = m_dragging;
        m_pressed = false;
        m_dragging = false;
        m_resizing = false;
        m_resizeEdges = {};
        setCursor(Qt::ArrowCursor);
        QScreen *screen = QGuiApplication::screenAt(frameGeometry().center());
        // A click near the screen edge must not move the window between the
        // first and second clicks of a double-click. Snap only after a drag.
        if (wasDragging && screen) {
            const QRect area = screen->availableGeometry();
            QPoint snapped = pos();
            constexpr int threshold = 12;
            if (qAbs(snapped.x() - area.left()) <= threshold)
                snapped.setX(area.left());
            if (qAbs(snapped.y() - area.top()) <= threshold)
                snapped.setY(area.top());
            if (qAbs(frameGeometry().right() - area.right()) <= threshold)
                snapped.setX(area.right() - width() + 1);
            if (qAbs(frameGeometry().bottom() - area.bottom()) <= threshold)
                snapped.setY(area.bottom() - height() + 1);
            move(snapped);
        }
        if (wasDragging)
            emit positionChanged(this);
    }
    QWidget::mouseReleaseEvent(event);
}

Qt::Edges PasteWindow::resizeEdgesAt(const QPoint &position) const
{
    if (!m_resizeEnabled || m_image.isNull())
        return {};
    constexpr int margin = 7;
    Qt::Edges edges;
    if (position.x() <= margin)
        edges |= Qt::LeftEdge;
    else if (position.x() >= width() - margin - 1)
        edges |= Qt::RightEdge;
    if (position.y() <= margin)
        edges |= Qt::TopEdge;
    else if (position.y() >= height() - margin - 1)
        edges |= Qt::BottomEdge;
    return edges;
}

void PasteWindow::updateResizeCursor(const QPoint &position)
{
    const Qt::Edges edges = resizeEdgesAt(position);
    if ((edges & (Qt::LeftEdge | Qt::RightEdge))
        && (edges & (Qt::TopEdge | Qt::BottomEdge))) {
        const bool nwse = (edges.testFlag(Qt::LeftEdge) == edges.testFlag(Qt::TopEdge));
        setCursor(nwse ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor);
    } else if (edges & (Qt::LeftEdge | Qt::RightEdge)) {
        setCursor(Qt::SizeHorCursor);
    } else if (edges & (Qt::TopEdge | Qt::BottomEdge)) {
        setCursor(Qt::SizeVerCursor);
    } else {
        setCursor(Qt::ArrowCursor);
    }
}

void PasteWindow::resizeFromPointer(const QPoint &globalPosition)
{
    if (m_resizeStartGeometry.isEmpty() || m_image.isNull())
        return;
    const QPoint delta = globalPosition - m_resizeStartGlobal;
    QRect geometry = m_resizeStartGeometry;
    if (m_resizeEdges.testFlag(Qt::LeftEdge))
        geometry.setLeft(geometry.left() + delta.x());
    if (m_resizeEdges.testFlag(Qt::RightEdge))
        geometry.setRight(geometry.right() + delta.x());
    if (m_resizeEdges.testFlag(Qt::TopEdge))
        geometry.setTop(geometry.top() + delta.y());
    if (m_resizeEdges.testFlag(Qt::BottomEdge))
        geometry.setBottom(geometry.bottom() + delta.y());

    constexpr int minimum = 24;
    if (geometry.width() < minimum) {
        if (m_resizeEdges.testFlag(Qt::LeftEdge))
            geometry.setLeft(geometry.right() - minimum + 1);
        else
            geometry.setRight(geometry.left() + minimum - 1);
    }
    if (geometry.height() < minimum) {
        if (m_resizeEdges.testFlag(Qt::TopEdge))
            geometry.setTop(geometry.bottom() - minimum + 1);
        else
            geometry.setBottom(geometry.top() + minimum - 1);
    }

    const qreal ratio = devicePixelRatioF();
    const bool horizontal = m_resizeEdges.testFlag(Qt::LeftEdge)
                            || m_resizeEdges.testFlag(Qt::RightEdge);
    const bool vertical = m_resizeEdges.testFlag(Qt::TopEdge)
                          || m_resizeEdges.testFlag(Qt::BottomEdge);
    qreal scale = m_scale;
    if (horizontal)
        scale = qMax<qreal>(0.1, qreal(geometry.width()) * ratio / m_image.width());
    if (vertical)
        scale = qMax<qreal>(scale, qreal(geometry.height()) * ratio / m_image.height());
    scale = qBound<qreal>(0.1, scale, 5.0);
    const QSize target(qMax(1, qRound(m_image.width() * scale / ratio)),
                       qMax(1, qRound(m_image.height() * scale / ratio)));
    if (m_resizeEdges.testFlag(Qt::LeftEdge))
        geometry.setLeft(geometry.right() - target.width() + 1);
    else
        geometry.setWidth(target.width());
    if (m_resizeEdges.testFlag(Qt::TopEdge))
        geometry.setTop(geometry.bottom() - target.height() + 1);
    else
        geometry.setHeight(target.height());
    m_scale = scale;
    setGeometry(geometry);
}

void PasteWindow::keyPressEvent(QKeyEvent *event)
{
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    if (event->key() == Qt::Key_Escape && modifiers.testFlag(Qt::ShiftModifier)) {
        if (m_shiftEscapeAction == QStringLiteral("关闭贴图"))
            hidePaste();
        else if (m_shiftEscapeAction == QStringLiteral("销毁贴图"))
            close();
        return;
    }

    if (event->key() == Qt::Key_Escape
        || (event->key() == Qt::Key_W
            && modifiers.testFlag(snipaste::platform::primaryModifier()))) {
        if (m_escapeAction == QStringLiteral("关闭贴图"))
            hidePaste();
        else if (m_escapeAction == QStringLiteral("销毁贴图"))
            close();
        return;
    }

    if (modifiers.testFlag(snipaste::platform::primaryModifier())) {
        if (event->key() == Qt::Key_A) {
            emit selectAllRequested(this);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_C && modifiers.testFlag(Qt::ShiftModifier)) {
            if (!m_sourceText.isEmpty())
                QApplication::clipboard()->setText(m_sourceText);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_C || event->key() == Qt::Key_Insert) {
            emit copyRequested(this);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_S) {
            const bool quick = modifiers.testFlag(Qt::ShiftModifier);
            QString path;
            if (quick) {
                const QString directory = QStandardPaths::writableLocation(
                    QStandardPaths::PicturesLocation);
                path = QDir(directory).filePath(QStringLiteral("snipaste-%1.png")
                    .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))));
            } else {
                path = QFileDialog::getSaveFileName(this,
                                                    QStringLiteral("保存贴图"),
                                                    QStringLiteral("snipaste.png"),
                                                    QStringLiteral("PNG 图片 (*.png);;JPEG 图片 (*.jpg *.jpeg);;所有文件 (*.*)"));
            }
            if (!path.isEmpty()) {
                if (QFileInfo(path).suffix().isEmpty())
                    path += QStringLiteral(".png");
                m_sourceImage.save(path);
            }
            event->accept();
            return;
        }
    }

    const bool plus = event->key() == Qt::Key_Plus
                      || (event->key() == Qt::Key_Equal
                          && modifiers.testFlag(Qt::ShiftModifier));
    const bool minus = event->key() == Qt::Key_Minus
                       || event->key() == Qt::Key_Underscore;
    if (plus || minus) {
        if (modifiers.testFlag(snipaste::platform::primaryModifier()))
            adjustOpacity(plus ? 0.05 : -0.05);
        else
            changeScale(plus ? 1.1 : 1.0 / 1.1);
        event->accept();
        return;
    }

    QPoint movement;
    switch (event->key()) {
    case Qt::Key_Left: movement = QPoint(-1, 0); break;
    case Qt::Key_Right: movement = QPoint(1, 0); break;
    case Qt::Key_Up: movement = QPoint(0, -1); break;
    case Qt::Key_Down: movement = QPoint(0, 1); break;
    case Qt::Key_W: movement = QPoint(0, -1); break;
    case Qt::Key_S: movement = QPoint(0, 1); break;
    case Qt::Key_A: movement = QPoint(-1, 0); break;
    case Qt::Key_D: movement = QPoint(1, 0); break;
    default: break;
    }
    if (!movement.isNull() && modifiers == Qt::NoModifier) {
        if (m_exchangeNavigationKeys) {
            const bool arrow = event->key() == Qt::Key_Left || event->key() == Qt::Key_Right
                               || event->key() == Qt::Key_Up || event->key() == Qt::Key_Down;
            if (arrow)
                emit moveRequested(this, movement);
            else
                move(pos() + movement);
        } else {
            move(pos() + movement);
        }
        event->accept();
        return;
    }

    switch (event->key()) {
    case Qt::Key_1:
        m_rotationQuarterTurns = (m_rotationQuarterTurns + 1) % 4;
        rebuildImage();
        return;
    case Qt::Key_2:
        m_rotationQuarterTurns = (m_rotationQuarterTurns + 3) % 4;
        rebuildImage();
        return;
    case Qt::Key_3:
        m_flipHorizontal = !m_flipHorizontal;
        rebuildImage();
        return;
    case Qt::Key_4:
        m_flipVertical = !m_flipVertical;
        rebuildImage();
        return;
    case Qt::Key_5:
        m_grayscale = !m_grayscale;
        rebuildImage();
        return;
    case Qt::Key_6:
        m_inverted = !m_inverted;
        rebuildImage();
        return;
    case Qt::Key_V:
        if (modifiers.testFlag(snipaste::platform::primaryModifier())) {
            const QImage image = QApplication::clipboard()->image();
            if (!image.isNull())
                setSourceImage(image);
            return;
        }
        break;
    default:
        break;
    }

    QWidget::keyPressEvent(event);
}

void PasteWindow::wheelEvent(QWheelEvent *event)
{
    const bool configured = !m_mouseActions.isEmpty();
    const bool scaleAction = mouseModifierMatches(mouseAction(0, QStringLiteral("无")), event->modifiers())
        && mouseAction(2, QStringLiteral("鼠标滚轮")) == QStringLiteral("鼠标滚轮");
    // Alt+wheel remains a compatibility shortcut when no custom mouse table
    // has been configured.
    if (scaleAction || (!configured && event->modifiers().testFlag(Qt::AltModifier))) {
        const qreal factor = event->angleDelta().y() > 0 ? 1.1 : 1.0 / 1.1;
        changeScale(factor);
        emit wheelTransformRequested(this, factor, 0.0);
        event->accept();
        return;
    }
    const bool opacityAction = mouseModifierMatches(mouseAction(4, QStringLiteral("Ctrl")), event->modifiers())
        && mouseAction(6, QStringLiteral("鼠标滚轮")) == QStringLiteral("鼠标滚轮");
    if (opacityAction) {
        const qreal delta = event->angleDelta().y() > 0 ? 0.05 : -0.05;
        setWindowOpacity(qBound(0.1, windowOpacity() + delta, 1.0));
        emit wheelTransformRequested(this, 1.0, delta);
        event->accept();
        return;
    }

    QWidget::wheelEvent(event);
}

void PasteWindow::contextMenuEvent(QContextMenuEvent *event)
{
    if (mouseModifierMatches(mouseAction(16, QStringLiteral("Shift")), event->modifiers())
        && mouseButtonMatches(mouseAction(17, QStringLiteral("\u53f3\u952e\u5355\u51fb")), Qt::RightButton, false)
        && !m_sourceText.isEmpty()) {
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
    const auto addOutputRow = [this, outputMenu, colorButtonStyle](
                                  const QString &label, bool checked, int value, const QColor &color,
                                  const std::function<void(bool)> &setChecked,
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
        QObject::connect(button, &QPushButton::clicked, outputMenu,
                         [this, button, colorButtonStyle, setColor] {
                             const QColor selected = QColorDialog::getColor(
                                 Qt::black, this, QStringLiteral("\u9009\u62e9\u989c\u8272"),
                                 QColorDialog::ShowAlphaChannel);
                             if (!selected.isValid())
                                 return;
                             colorButtonStyle(button, selected);
                             setColor(selected);
                         });
    };
    addOutputRow(QStringLiteral("\u9634\u5f71"), m_shadowEnabled, m_shadowSize, m_shadowColor,
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
    addOutputRow(QStringLiteral("\u8fb9\u6846"), m_borderEnabled, m_borderWidth, m_borderColor,
                 [this](bool enabled) { m_borderEnabled = enabled; update(); },
                 [this](int width) { m_borderWidth = qMax(0, width); update(); },
                 [this](const QColor &color) { m_borderColor = color; update(); });

    QAction *extractTextAction = menu.addAction(QStringLiteral("\u63d0\u53d6\u6587\u5b57\u5e76\u590d\u5236"));
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
    QAction *transparentAction = addBackground(QStringLiteral("\u900f\u660e"), QStringLiteral("\u900f\u660e"));
    QAction *fakeTransparentAction = addBackground(QStringLiteral("\u4f2a\u900f\u660e"), QStringLiteral("\u4f2a\u900f\u660e"));
    QAction *darkCheckerAction = addBackground(QStringLiteral("\u6697\u8272\u68cb\u76d8"), QStringLiteral("\u9ed1\u8272"));
    QAction *lightCheckerAction = addBackground(QStringLiteral("\u6d45\u8272\u68cb\u76d8"), QStringLiteral("\u767d\u8272"));
    QAction *customBackgroundAction = addBackground(QStringLiteral("\u81ea\u5b9a\u4e49\u989c\u8272"), QStringLiteral("\u81ea\u5b9a\u4e49\u989c\u8272"));

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
    if (m_pasteManager) {
        const QStringList groupNames = m_pasteManager->groupNames();
        const int currentGroup = m_pasteManager->groupIndexOf(this);
        QActionGroup *groupActions = new QActionGroup(groupMenu);
        groupActions->setExclusive(true);
        for (int index = 0; index < groupNames.size(); ++index) {
            QAction *action = groupMenu->addAction(
                QStringLiteral("%1  %2").arg(groupNames.at(index)).arg(index + 1));
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
    if (m_pasteManager) {
        const int group = m_pasteManager->groupIndexOf(this);
        const QStringList names = m_pasteManager->groupNames();
        const QString name = group >= 0 && group < names.size() ? names.at(group) : QStringLiteral("Default");
        infoMenu->addAction(QStringLiteral("\u6240\u5728\u7ec4: %1").arg(name))->setEnabled(false);
    } else {
        infoMenu->addAction(QStringLiteral("\u6240\u5728\u7ec4: Default"))->setEnabled(false);
    }
    infoMenu->addAction(QStringLiteral("\u7f29\u653e: %1%").arg(QString::number(m_scale * 100.0, 'f', 1)))->setEnabled(false);
    infoMenu->addAction(QStringLiteral("\u65cb\u8f6c: %1\u00b0").arg(m_rotationQuarterTurns * 90))->setEnabled(false);
    infoMenu->addAction(QStringLiteral("\u4e0d\u900f\u660e\u5ea6: %1%").arg(qRound(windowOpacity() * 100.0)))->setEnabled(false);
    infoMenu->addAction(QStringLiteral("\u6c34\u5e73\u955c\u50cf: %1").arg(m_flipHorizontal ? QStringLiteral("\u662f") : QStringLiteral("\u5426")))->setEnabled(false);
    infoMenu->addAction(QStringLiteral("\u5782\u76f4\u955c\u50cf: %1").arg(m_flipVertical ? QStringLiteral("\u662f") : QStringLiteral("\u5426")))->setEnabled(false);
    infoMenu->addAction(QStringLiteral("\u7070\u5ea6\u663e\u793a: %1").arg(m_grayscale ? QStringLiteral("\u662f") : QStringLiteral("\u5426")))->setEnabled(false);
    infoMenu->addAction(QStringLiteral("\u53cd\u8272\u663e\u793a: %1").arg(m_inverted ? QStringLiteral("\u662f") : QStringLiteral("\u5426")))->setEnabled(false);

    QAction *selected = menu.exec(event->globalPos());
    if (selected == copyAction) {
        QApplication::clipboard()->setImage(m_image);
    } else if (selected == saveAction) {
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("\u56fe\u50cf\u53e6\u5b58\u4e3a"), QString(),
                                                           QStringLiteral("PNG \u56fe\u50cf (*.png);;\u6240\u6709\u6587\u4ef6 (*)"));
        if (!path.isEmpty())
            m_image.save(path);
    } else if (selected == extractTextAction) {
        if (!m_sourceText.isEmpty())
            QApplication::clipboard()->setText(m_sourceText);
    } else if (selected == toolbarAction) {
        m_showToolbar = toolbarAction->isChecked();
    } else if (selected == annotationsAction) {
        m_showAnnotations = annotationsAction->isChecked();
        update();
    } else if (selected == shadowAction) {
        setShadowEnabled(shadowAction->isChecked());
    } else if (selected == alwaysOnTopAction) {
        setAlwaysOnTop(alwaysOnTopAction->isChecked());
    } else if (selected == scale333) {
        m_scale = 0.333; rebuildImage();
    } else if (selected == scale50) {
        m_scale = 0.5; rebuildImage();
    } else if (selected == scale80) {
        m_scale = 0.8; rebuildImage();
    } else if (selected == scale100) {
        m_scale = 1.0; rebuildImage();
    } else if (selected == scale200) {
        m_scale = 2.0; rebuildImage();
    } else if (selected == customScaleAction) {
        bool ok = false;
        const double percentage = QInputDialog::getDouble(this, QStringLiteral("\u81ea\u5b9a\u4e49\u7f29\u653e"),
                                                           QStringLiteral("\u7f29\u653e\u767e\u5206\u6bd4:"), m_scale * 100.0,
                                                           10.0, 500.0, 1, &ok);
        if (ok) {
            m_scale = percentage / 100.0;
            rebuildImage();
        }
    } else if (selected == smoothAction) {
        setSmoothScale(smoothAction->isChecked());
    } else if (selected == transparentAction) {
        setBackgroundMode(QStringLiteral("\u900f\u660e"));
    } else if (selected == fakeTransparentAction) {
        setBackgroundMode(QStringLiteral("\u4f2a\u900f\u660e"));
    } else if (selected == darkCheckerAction) {
        setBackgroundMode(QStringLiteral("\u9ed1\u8272"));
    } else if (selected == lightCheckerAction) {
        setBackgroundMode(QStringLiteral("\u767d\u8272"));
    } else if (selected == customBackgroundAction) {
        const QColor color = QColorDialog::getColor(m_customBackgroundColor, this,
                                                     QStringLiteral("\u81ea\u5b9a\u4e49\u80cc\u666f\u989c\u8272"),
                                                     QColorDialog::ShowAlphaChannel);
        if (color.isValid()) {
            m_customBackgroundColor = color;
            setBackgroundMode(QStringLiteral("\u81ea\u5b9a\u4e49\u989c\u8272"));
        }
    } else if (selected == rotateLeftAction) {
        m_rotationQuarterTurns = (m_rotationQuarterTurns + 3) % 4; rebuildImage();
    } else if (selected == rotateRightAction) {
        m_rotationQuarterTurns = (m_rotationQuarterTurns + 1) % 4; rebuildImage();
    } else if (selected == flipHorizontalAction) {
        m_flipHorizontal = !m_flipHorizontal; rebuildImage();
    } else if (selected == flipVerticalAction) {
        m_flipVertical = !m_flipVertical; rebuildImage();
    } else if (selected == grayscaleAction) {
        m_grayscale = grayscaleAction->isChecked(); rebuildImage();
    } else if (selected == invertedAction) {
        m_inverted = invertedAction->isChecked(); rebuildImage();
    } else if (selected == pasteAction) {
        const QImage image = QApplication::clipboard()->image();
        if (!image.isNull())
            setSourceImage(image);
    } else if (selected == replaceFileAction) {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("\u66ff\u6362\u4e3a\u6587\u4ef6"), QString(),
                                                          QStringLiteral("\u56fe\u50cf\u6587\u4ef6 (*.png *.jpg *.jpeg *.bmp *.gif);;\u6240\u6709\u6587\u4ef6 (*)"));
        QImage image;
        if (!path.isEmpty() && image.load(path))
            setSourceImage(image);
    } else if (selected == newGroupAction && m_pasteManager) {
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("\u65b0\u5206\u7ec4"), QStringLiteral("\u5206\u7ec4\u540d\u79f0:"),
                                                   QLineEdit::Normal, QString(), &ok);
        if (ok) {
            const int index = m_pasteManager->createGroup(name);
            m_pasteManager->moveWindowToGroup(this, index);
        }
    } else if (selected && selected->parent() == groupMenu && m_pasteManager
               && selected->data().isValid()) {
        m_pasteManager->moveWindowToGroup(this, selected->data().toInt());
    } else if (selected == closeAction) {
        hidePaste();
    } else if (selected == destroyAction) {
        close();
    }
}

void PasteWindow::dragEnterEvent(QDragEnterEvent *event)
{
    const QMimeData *mime = event->mimeData();
    if (mime && (mime->hasImage() || mime->hasUrls()))
        event->acceptProposedAction();
    else
        event->ignore();
}

void PasteWindow::dropEvent(QDropEvent *event)
{
    const QMimeData *mime = event->mimeData();
    if (!mime) {
        event->ignore();
        return;
    }
    QImage image;
    if (mime->hasImage())
        image = qvariant_cast<QImage>(mime->imageData());
    // Browsers usually expose dragged images as text/html instead of a
    // file URL.  Resolve the first image source so dragging directly from a
    // page behaves like copying the image and pasting it.
    if (image.isNull() && mime->hasHtml()) {
        static const QRegularExpression imageSource(
            QStringLiteral("<img[^>]+src\\s*=\\s*['\"]([^'\"]+)"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch match = imageSource.match(mime->html());
        if (match.hasMatch()) {
            const QUrl sourceUrl(match.captured(1));
            if (sourceUrl.isValid() && sourceUrl.scheme() == QStringLiteral("data")) {
                const QByteArray encoded = sourceUrl.toString().section(QChar(','), 1).toUtf8();
                image = QImage::fromData(QByteArray::fromBase64(encoded));
            } else if (sourceUrl.isValid()
                       && (sourceUrl.scheme() == QStringLiteral("http")
                           || sourceUrl.scheme() == QStringLiteral("https"))) {
                auto *network = new QNetworkAccessManager(this);
                QNetworkReply *reply = network->get(QNetworkRequest(sourceUrl));
                connect(reply, &QNetworkReply::finished, this, [this, reply, network] {
                    const QByteArray data = reply->readAll();
                    if (m_maxNetworkImageBytes <= 0 || data.size() <= m_maxNetworkImageBytes) {
                        const QImage downloaded = QImage::fromData(data);
                        if (!downloaded.isNull())
                            setSourceImage(downloaded);
                    }
                    reply->deleteLater();
                    network->deleteLater();
                });
                event->acceptProposedAction();
                return;
            } else if (sourceUrl.isLocalFile()) {
                image.load(sourceUrl.toLocalFile());
            }
        }
    }
    if (image.isNull() && mime->hasUrls()) {
        for (const QUrl &url : mime->urls()) {
            if (url.isLocalFile()) {
                QImage candidate(url.toLocalFile());
                if (!candidate.isNull()) {
                    image = std::move(candidate);
                    break;
                }
            } else if (url.scheme() == QStringLiteral("http")
                       || url.scheme() == QStringLiteral("https")) {
                auto *network = new QNetworkAccessManager(this);
                QNetworkReply *reply = network->get(QNetworkRequest(url));
                connect(reply, &QNetworkReply::finished, this, [this, reply, network] {
                    const QByteArray data = reply->readAll();
                    if (m_maxNetworkImageBytes <= 0 || data.size() <= m_maxNetworkImageBytes) {
                        const QImage downloaded = QImage::fromData(data);
                        if (!downloaded.isNull())
                            setSourceImage(downloaded);
                    }
                    reply->deleteLater();
                    network->deleteLater();
                });
                event->acceptProposedAction();
                return;
            }
        }
    }
    if (image.isNull()) {
        event->ignore();
        return;
    }
    setSourceImage(image);
    event->acceptProposedAction();
}

} // namespace snipaste::ui
