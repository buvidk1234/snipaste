#include "ui/paste/paste_window.h"
#include "core/paste/image_transform.h"
#include "platform/desktop_integration.h"

#include <QCloseEvent>
#include <QCursor>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QPainter>
#include <QScreen>

namespace snipaste::ui
{

PasteWindow::PasteWindow(QImage image, QWidget *parent) : QWidget(parent)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_QuitOnClose, false);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::ArrowCursor);
    setAcceptDrops(true);
    // Keep the native window fully opaque for a newly created pin.  The
    // manager may apply a user-selected opacity immediately afterwards, while
    // this explicit initialization prevents a platform/window-style default
    // from making the first frame look translucent.
    setWindowOpacity(1.0);

    connect(&m_flashTimer, &QTimer::timeout, this, [this] {
        if (!m_flashActive)
        {
            m_flashTimer.stop();
            return;
        }
        ++m_flashStep;
        if (m_flashStep >= 6)
        {
            m_flashTimer.stop();
            m_flashActive = false;
            m_flashBorder = false;
            update();
            return;
        }
        m_flashBorder = (m_flashStep % 2) == 0;
        update();
    });

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

    if (screen)
    {
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
    const bool showWithoutActivating = !m_activateOnShow;
    if (showWithoutActivating)
        setAttribute(Qt::WA_ShowWithoutActivating, true);
    show();
    if (showWithoutActivating)
        setAttribute(Qt::WA_ShowWithoutActivating, false);
#ifdef Q_OS_WIN
    if (m_virtualDesktopPinned)
        platform::setWindowPinned(winId(), true);
#elif defined(Q_OS_MACOS)
    // A newly shown QWidget may own a different NSWindow after flags or
    // screen changes, so apply the Space policy after show() each time.
    platform::setWindowPinned(winId(), m_virtualDesktopPinned);
#endif
    updateDisplaySize();
    if (m_activateOnShow)
    {
        activateWindow();
        raise();
        setFocus();
    }
}

void PasteWindow::flash()
{
    if (m_state != State::Active)
        return;
    if (!isVisible())
        showExisting();
    m_flashStep = 0;
    m_flashActive = true;
    m_flashBorder = true;
    // Keep the synchronous state of a restored pin intact.  The first timer
    // tick applies the optional transparency pulse after the caller has had a
    // chance to finish its restore/activation work.
    m_flashTimer.start(80);
    update();
}

void PasteWindow::setFlashTransparency(bool enabled)
{
    if (m_flashTransparency == enabled)
        return;
    m_flashTransparency = enabled;
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

void PasteWindow::setMaxSize(int pixels)
{
    m_maxSize = qBound(0, pixels, 100000);
    updateDisplaySize();
}

void PasteWindow::setThumbnailSize(const QSize &size)
{
    m_thumbnailSize = QSize(qBound(1, size.width(), 4096), qBound(1, size.height(), 4096));
}

void PasteWindow::setWheelCenter(bool enabled)
{
    m_wheelCenter = enabled;
}

void PasteWindow::setResetTransform(bool enabled)
{
    m_resetTransform = enabled;
}

void PasteWindow::setDisableDpiScaling(bool enabled)
{
    m_disableDpiScaling = enabled;
    updateDisplaySize();
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

int PasteWindow::rotationQuarterTurns() const
{
    return m_rotationQuarterTurns;
}
bool PasteWindow::flipHorizontal() const
{
    return m_flipHorizontal;
}
bool PasteWindow::flipVertical() const
{
    return m_flipVertical;
}
bool PasteWindow::grayscale() const
{
    return m_grayscale;
}
bool PasteWindow::inverted() const
{
    return m_inverted;
}
qreal PasteWindow::scale() const
{
    return m_scale;
}
bool PasteWindow::transparentBackground() const
{
    return m_transparentBackground;
}
QString PasteWindow::backgroundMode() const
{
    return m_backgroundMode;
}

void PasteWindow::setBackgroundMode(const QString &mode)
{
    const QString normalized = mode.trimmed();
    if (normalized == QStringLiteral("透明") || normalized == QStringLiteral("白色") ||
        normalized == QStringLiteral("黑色") || normalized == QStringLiteral("伪透明") ||
        normalized == QStringLiteral("自定义颜色"))
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

void PasteWindow::setVirtualDesktopPinned(bool enabled)
{
    m_virtualDesktopPinned = enabled;
    if (isVisible())
        platform::setWindowPinned(winId(), enabled);
}

bool PasteWindow::virtualDesktopPinned() const
{
    return m_virtualDesktopPinned;
}

void PasteWindow::setPasteManager(PasteManager *manager)
{
    m_pasteManager = manager;
}

bool PasteWindow::alwaysOnTop() const
{
    return m_alwaysOnTop;
}

bool PasteWindow::thumbnailMode() const
{
    return m_thumbnailMode;
}

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

void PasteWindow::restoreTransform(int rotationQuarterTurns, bool flipHorizontal, bool flipVertical,
                                   bool grayscale, bool inverted, qreal scale, bool transparentBackground,
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
    const qreal ratio = m_disableDpiScaling ? 1.0 : devicePixelRatioF();
    const bool nativeSize = qAbs(qreal(width()) * ratio - qreal(m_image.width())) < 0.5 &&
                            qAbs(qreal(height()) * ratio - qreal(m_image.height())) < 0.5;
    painter.setRenderHint(QPainter::SmoothPixmapTransform, m_smoothScale && !nativeSize);
    const qreal flashOpacity = m_flashActive && m_flashTransparency && !m_flashBorder ? 0.45 : 1.0;
    painter.save();
    painter.setOpacity(flashOpacity);
    if (m_backgroundMode == QStringLiteral("白色"))
        painter.fillRect(rect(), QColor(255, 255, 255, 245));
    else if (m_backgroundMode == QStringLiteral("黑色"))
        painter.fillRect(rect(), QColor(0, 0, 0, 245));
    else if (m_backgroundMode == QStringLiteral("自定义颜色"))
        painter.fillRect(rect(), m_customBackgroundColor);
    else if (m_backgroundMode != QStringLiteral("透明"))
        painter.fillRect(rect(), QColor(255, 255, 255, 245));
    painter.drawImage(rect(), m_image);
    painter.restore();

    if (m_selectionThumbnailDragging && m_selectionThumbnailMoved)
    {
        const QRect selection = QRect(m_selectionThumbnailOrigin, m_selectionThumbnailCurrent).normalized();
        QPen selectionPen(QColor(255, 210, 70, 235), 1, Qt::DashLine);
        painter.setPen(selectionPen);
        painter.setBrush(Qt::NoBrush);
        const QRect clipped = selection.intersected(rect());
        if (!clipped.isEmpty())
            painter.drawRect(clipped.adjusted(0, 0, -1, -1));
    }

    if (m_borderEnabled || m_flashBorder)
    {
        QPen border(m_flashBorder ? QColor(255, 205, 60, 255) : m_borderColor);
        border.setWidth(m_borderWidth);
        painter.setPen(border);
        painter.setBrush(Qt::NoBrush);
        // Keep the entire stroke inside the window. Integer QRect endpoints
        // leave the right/bottom edge uncovered after fractional-DPI resizing.
        const qreal inset = (border.isCosmetic() ? 1.0 / devicePixelRatioF() : border.widthF()) / 2.0;
        painter.drawRect(QRectF(rect()).adjusted(inset, inset, -inset, -inset));
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

    m_image = core::transformImage(
        m_sourceImage, {m_rotationQuarterTurns, m_flipHorizontal, m_flipVertical, m_grayscale, m_inverted});
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
    const qreal ratio = m_disableDpiScaling ? 1.0 : devicePixelRatioF();
    qreal scale = m_scale;
    if (m_maxSize > 0)
    {
        const qreal maxPhysical = qMax(m_image.width(), m_image.height()) * scale;
        if (maxPhysical > m_maxSize)
            scale = qMax<qreal>(0.1, scale * m_maxSize / maxPhysical);
    }
    if (!qFuzzyCompare(scale, m_scale))
        m_scale = scale;
    resize(qMax(1, qRound(m_image.width() * scale / ratio)),
           qMax(1, qRound(m_image.height() * scale / ratio)));
}

void PasteWindow::hidePaste()
{
    if (m_state != State::Active)
        return;
    // Thumbnail mode is a transient viewing aid.  Returning a pin to the
    // hidden/closed state must keep its normal transform so that reviving it
    // does not unexpectedly change its size.
    if (m_thumbnailMode)
    {
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
    const QPoint cursor = QCursor::pos();
    const QRect oldGeometry = frameGeometry();
    const qreal oldWidth = qMax(1, oldGeometry.width());
    const qreal oldHeight = qMax(1, oldGeometry.height());
    const qreal relativeX = qBound<qreal>(0.0, (cursor.x() - oldGeometry.left()) / oldWidth, 1.0);
    const qreal relativeY = qBound<qreal>(0.0, (cursor.y() - oldGeometry.top()) / oldHeight, 1.0);
    m_scale = qBound<qreal>(0.1, m_scale * factor, 5.0);
    rebuildImage();
    if (m_wheelCenter)
        move(cursor - QPoint(qRound(relativeX * width()), qRound(relativeY * height())));
}

void PasteWindow::toggleThumbnailMode()
{
    if (m_sourceImage.isNull())
        return;
    if (m_thumbnailMode)
    {
        m_thumbnailMode = false;
        m_scale = qBound<qreal>(0.1, m_scaleBeforeThumbnail, 5.0);
    }
    else
    {
        m_scaleBeforeThumbnail = m_scale;
        const qreal ratio = m_disableDpiScaling ? 1.0 : devicePixelRatioF();
        const QSize target(qMax(1, qRound(m_thumbnailSize.width() * ratio)),
                           qMax(1, qRound(m_thumbnailSize.height() * ratio)));
        const qreal scaleX = m_image.width() > 0 ? qreal(target.width()) / m_image.width() : 1.0;
        const qreal scaleY = m_image.height() > 0 ? qreal(target.height()) / m_image.height() : 1.0;
        // Thumbnail dimensions are explicit user preferences and may be
        // smaller than the normal 10% interactive zoom floor.
        m_scale = qBound<qreal>(0.01, qMin(scaleX, scaleY), 5.0);
        m_thumbnailMode = true;
    }
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

} // namespace snipaste::ui
