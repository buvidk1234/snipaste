#include "ui/snip/window_capture_overlay.h"
#include "platform/windows/capture_target.h"
#include "platform/screen_capture.h"
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
#include "platform/macos/capture.h"
#endif

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>

#ifdef Q_OS_WIN
#include <windows.h>
#include <oleacc.h>
#include <oleauto.h>
#endif

namespace snipaste::ui {

namespace {

#ifdef Q_OS_WIN
struct WindowSearchContext {
    POINT point{};
    HWND ignored = nullptr;
    HWND result = nullptr;
    RECT resultRect{};
};

BOOL CALLBACK findWindowCallback(HWND window, LPARAM parameter)
{
    auto *context = reinterpret_cast<WindowSearchContext *>(parameter);
    if (window == context->ignored || !IsWindowVisible(window) || IsIconic(window))
        return TRUE;

    RECT rect{};
    if (!GetWindowRect(window, &rect) || !PtInRect(&rect, context->point))
        return TRUE;
    if (GetWindowLongPtr(window, GWL_EXSTYLE) & WS_EX_TRANSPARENT)
        return TRUE;

    context->result = window;
    context->resultRect = rect;
    return FALSE;
}

bool findAccessibleElement(const POINT &point, RECT *resultRect)
{
    if (!resultRect)
        return false;
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninitialize = SUCCEEDED(init);
    IAccessible *accessible = nullptr;
    VARIANT child;
    VariantInit(&child);
    const HRESULT found = AccessibleObjectFromPoint(point, &accessible, &child);
    bool success = false;
    if (SUCCEEDED(found) && accessible) {
        LONG left = 0;
        LONG top = 0;
        LONG width = 0;
        LONG height = 0;
        if (SUCCEEDED(accessible->accLocation(&left, &top, &width, &height, child))
            && width > 0 && height > 0) {
            resultRect->left = left;
            resultRect->top = top;
            resultRect->right = left + width;
            resultRect->bottom = top + height;
            success = true;
        }
        accessible->Release();
    }
    VariantClear(&child);
    if (shouldUninitialize)
        CoUninitialize();
    return success;
}
#endif

} // namespace

WindowCaptureOverlay::WindowCaptureOverlay(QScreen *screen, bool elementMode, QWidget *parent)
    : QWidget(parent)
    , m_screen(screen)
    , m_elementMode(elementMode)
{
    Q_ASSERT(screen);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    m_screenGeometry = screen->geometry();
    m_background = platform::captureScreen(screen);
    m_background.setDevicePixelRatio(1);
    setGeometry(m_screenGeometry);
}

void WindowCaptureOverlay::updateTarget()
{
#ifdef Q_OS_WIN
    WindowSearchContext context;
    const QPoint global = QCursor::pos();
    context.point = POINT{global.x(), global.y()};
    context.ignored = reinterpret_cast<HWND>(winId());
    EnumWindows(findWindowCallback, reinterpret_cast<LPARAM>(&context));
    if (context.result) {
        HWND target = context.result;
        RECT targetRect = context.resultRect;
        if (m_elementMode) {
            RECT accessibleRect{};
            if (findAccessibleElement(context.point, &accessibleRect))
                targetRect = accessibleRect;
            POINT clientPoint = context.point;
            ScreenToClient(target, &clientPoint);
            HWND child = ChildWindowFromPointEx(target,
                                                clientPoint,
                                                CWP_SKIPINVISIBLE | CWP_SKIPDISABLED);
            while (child && child != target) {
                RECT childRect{};
                if (!GetWindowRect(child, &childRect))
                    break;
                const LONG width = childRect.right - childRect.left;
                const LONG height = childRect.bottom - childRect.top;
                if (width <= 0 || height <= 0)
                    break;
                target = child;
                targetRect = childRect;
                POINT nestedPoint = context.point;
                ScreenToClient(target, &nestedPoint);
                HWND nested = ChildWindowFromPointEx(target,
                                                     nestedPoint,
                                                     CWP_SKIPINVISIBLE | CWP_SKIPDISABLED);
                if (!nested || nested == target)
                    break;
                child = nested;
            }
        }
        m_targetWindow = reinterpret_cast<quintptr>(target);
        m_targetGeometry = QRect(targetRect.left,
                                 targetRect.top,
                                 targetRect.right - targetRect.left,
                                 targetRect.bottom - targetRect.top);
    } else {
        m_targetWindow = 0;
        m_targetGeometry = {};
    }
#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    const QPoint localPosition = QCursor::pos() - m_screenGeometry.topLeft();
    quintptr nativeWindow = 0;
    const QRect localTarget = platform::captureTargetAt(localPosition, size(), winId(), m_elementMode,
                                                        {}, {}, &nativeWindow);
    if (!localTarget.isEmpty() && localTarget != rect() && nativeWindow) {
        m_targetWindow = nativeWindow;
        m_targetGeometry = localTarget.translated(m_screenGeometry.topLeft());
    } else {
        m_targetWindow = 0;
        m_targetGeometry = {};
    }
#else
    m_targetWindow = 0;
    m_targetGeometry = {};
#endif
    update();
}

void WindowCaptureOverlay::captureTarget()
{
    if (!m_screen || !m_targetWindow || m_targetGeometry.isEmpty())
        return;

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    QImage image = platform::captureMacWindow(m_targetWindow);
    if (m_elementMode && !image.isNull()) {
        const QRect windowBounds = platform::macWindowBounds(m_targetWindow);
        const QRect crop = m_targetGeometry.translated(-windowBounds.topLeft());
        if (!windowBounds.isEmpty() && !crop.isEmpty()) {
            const qreal scaleX = qreal(image.width()) / qreal(windowBounds.width());
            const qreal scaleY = qreal(image.height()) / qreal(windowBounds.height());
            const QRect pixels(qRound(crop.left() * scaleX),
                               qRound(crop.top() * scaleY),
                               qMax(1, qRound(crop.width() * scaleX)),
                               qMax(1, qRound(crop.height() * scaleY)));
            const QRect clipped = pixels.intersected(image.rect());
            image = clipped.isEmpty() ? QImage() : image.copy(clipped);
        } else {
            image = {};
        }
    }
    if (image.isNull()) {
        const QRect local = m_targetGeometry.translated(-m_screenGeometry.topLeft())
                                .intersected(rect());
        const qreal scaleX = width() > 0 ? qreal(m_background.width()) / width() : 1.0;
        const qreal scaleY = height() > 0 ? qreal(m_background.height()) / height() : 1.0;
        const QRect pixels(qRound(local.left() * scaleX),
                           qRound(local.top() * scaleY),
                           qMax(1, qRound(local.width() * scaleX)),
                           qMax(1, qRound(local.height() * scaleY)));
        image = local.isEmpty() ? QImage() : m_background.copy(pixels.intersected(m_background.rect()));
    }
#else
    QImage image = m_screen->grabWindow(static_cast<WId>(m_targetWindow)).toImage();
    image.setDevicePixelRatio(1);
    if (image.isNull()) {
        const QRect local = m_targetGeometry.translated(-m_screenGeometry.topLeft())
                                .intersected(rect());
        image = m_background.copy(local);
    }
#endif
    if (!image.isNull()) {
        emit screenshotCaptured(image);
        close();
    }
}

void WindowCaptureOverlay::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.drawImage(rect(), m_background);
    painter.fillRect(rect(), QColor(0, 0, 0, 120));

    if (m_targetGeometry.isEmpty())
        return;
    const QRect local = m_targetGeometry.translated(-m_screenGeometry.topLeft()).intersected(rect());
    if (local.isEmpty())
        return;
    painter.save();
    painter.setClipRect(local);
    painter.drawImage(rect(), m_background);
    painter.restore();
    QPen pen(QColor(70, 170, 255));
    pen.setWidth(2);
    painter.setPen(pen);
    painter.drawRect(local.adjusted(0, 0, -1, -1));
    const QString label = QStringLiteral("%1 %2 × %3")
                              .arg(m_elementMode ? QStringLiteral("元素") : QStringLiteral("窗口"))
                              .arg(local.width())
                              .arg(local.height());
    const QRect labelRect(local.left(), qMax(0, local.top() - 28), 150, 24);
    painter.fillRect(labelRect, QColor(20, 80, 150, 220));
    painter.setPen(Qt::white);
    painter.drawText(labelRect, Qt::AlignCenter, label);
}

void WindowCaptureOverlay::mouseMoveEvent(QMouseEvent *)
{
    updateTarget();
}

void WindowCaptureOverlay::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        captureTarget();
    else if (event->button() == Qt::RightButton)
        close();
}

void WindowCaptureOverlay::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape)
        close();
    else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        captureTarget();
    else
        QWidget::keyPressEvent(event);
}

void WindowCaptureOverlay::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (!m_cursorOverridden) {
        QApplication::setOverrideCursor(Qt::CrossCursor);
        m_cursorOverridden = true;
    }
    activateWindow();
    raise();
    setFocus();
    updateTarget();
}

void WindowCaptureOverlay::closeEvent(QCloseEvent *event)
{
    if (m_cursorOverridden) {
        QApplication::restoreOverrideCursor();
        m_cursorOverridden = false;
    }
    QWidget::closeEvent(event);
}

} // namespace snipaste::ui
