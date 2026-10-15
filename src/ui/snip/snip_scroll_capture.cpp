#include "ui/snip/snip_overlay.h"
#include "core/command/argument_parser.h"
#include "platform/screen_capture.h"
#include "platform/windows/capture_target.h"
#include "ui/snip/scroll_capture_panel.h"
#include "ui/snip/scroll_capture_preview.h"

#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDateTime>
#include <QEventLoop>
#include <QFileDialog>
#include <QFrame>
#include <QGuiApplication>
#include <QMessageBox>
#include <QMetaObject>
#include <QScreen>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace snipaste::ui {

namespace {

#ifdef Q_OS_WIN
// Qt only receives mouse messages for its own windows. During scrolling
// capture the target belongs to another process, so a low-level mouse hook is
// used solely to notice a wheel or completed scrollbar drag.
QPointer<SnipOverlay> g_scrollInputOverlay;
HWND g_scrollInputTarget = nullptr;
HHOOK g_scrollInputHook = nullptr;
bool g_scrollMouseDown = false;
bool g_scrollMouseDragged = false;
POINT g_scrollMouseDownPoint{};
QRect g_scrollSelectionRect{};

bool isScrollTargetWindow(HWND window)
{
    if (!window || !g_scrollInputTarget)
        return false;
    return window == g_scrollInputTarget || IsChild(g_scrollInputTarget, window)
        || GetAncestor(window, GA_ROOT) == g_scrollInputTarget;
}

LRESULT CALLBACK scrollInputHookProc(int code, WPARAM message, LPARAM data)
{
    if (code == HC_ACTION && g_scrollInputOverlay && data) {
        const auto *mouse = reinterpret_cast<const MSLLHOOKSTRUCT *>(data);
        const HWND window = WindowFromPoint(mouse->pt);
        const bool insideSelection = !g_scrollSelectionRect.isEmpty()
            && g_scrollSelectionRect.contains(QPoint(mouse->pt.x, mouse->pt.y));
        const bool isTarget = isScrollTargetWindow(window) || insideSelection;

        if (message == WM_LBUTTONDOWN) {
            g_scrollMouseDown = isTarget;
            g_scrollMouseDragged = false;
            g_scrollMouseDownPoint = mouse->pt;
        } else if (message == WM_MOUSEMOVE && g_scrollMouseDown) {
            const int dx = mouse->pt.x - g_scrollMouseDownPoint.x;
            const int dy = mouse->pt.y - g_scrollMouseDownPoint.y;
            if (qAbs(dx) >= 4 || qAbs(dy) >= 4)
                g_scrollMouseDragged = true;
        }

        const bool wheel = message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL;
        const bool dragRelease = message == WM_LBUTTONUP && g_scrollMouseDragged;
        if ((wheel && isTarget) || dragRelease) {
            QPointer<SnipOverlay> overlay = g_scrollInputOverlay;
            QMetaObject::invokeMethod(overlay, "scheduleManualScrollCapture",
                                      Qt::QueuedConnection);
        }
        if (message == WM_LBUTTONUP) {
            g_scrollMouseDown = false;
            g_scrollMouseDragged = false;
        }
    }
    return CallNextHookEx(g_scrollInputHook, code, message, data);
}

void installScrollInputHook(SnipOverlay *overlay, HWND target, const QRect &selection)
{
    g_scrollInputOverlay = overlay;
    g_scrollInputTarget = target;
    g_scrollSelectionRect = selection;
    g_scrollInputHook = SetWindowsHookExW(WH_MOUSE_LL, scrollInputHookProc,
                                          nullptr, 0);
    g_scrollMouseDown = false;
    g_scrollMouseDragged = false;
}

void uninstallScrollInputHook(SnipOverlay *overlay)
{
    if (g_scrollInputOverlay != overlay)
        return;
    if (g_scrollInputHook) {
        UnhookWindowsHookEx(g_scrollInputHook);
        g_scrollInputHook = nullptr;
    }
    g_scrollInputTarget = nullptr;
    g_scrollInputOverlay = nullptr;
    g_scrollSelectionRect = {};
    g_scrollMouseDown = false;
    g_scrollMouseDragged = false;
}
#endif

QString captureStatus(const snipaste::core::VerticalScrollStitcher &stitcher)
{
    const QSize size = stitcher.size();
    if (size.isEmpty())
        return QStringLiteral("等待采集");
    return QStringLiteral("已采集 %1 次，长图 %2 × %3 px")
        .arg(stitcher.frameCount())
        .arg(size.width())
        .arg(size.height());
}

} // namespace

void SnipOverlay::startScrollCapture()
{
#ifndef Q_OS_WIN
    QMessageBox::information(this, QStringLiteral("滚动截图"),
                             QStringLiteral("滚动截图目前仅支持 Windows。"));
    return;
#else
    if (m_scrollCaptureActive || !m_selectionConfirmed || m_selection.isEmpty())
        return;

    quintptr targetWindow = 0;
    const QRect target = platform::captureTargetAt(
        m_selection.center(), size(), winId(), false,
        m_excludedDetectionProcesses, m_unstableDetectionProcesses, &targetWindow);
    if (!targetWindow || target.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("滚动截图"),
                                 QStringLiteral("没有找到可滚动的目标窗口，请先选中窗口内容。"));
        return;
    }

    m_scrollScreen = QGuiApplication::screenAt(globalSelection().center());
    if (!m_scrollScreen)
        m_scrollScreen = QGuiApplication::primaryScreen();
    if (!m_scrollScreen)
        return;

    m_scrollWindow = targetWindow;
    m_scrollGlobalSelection = globalSelection();
    m_scrollOriginalCursor = QCursor::pos();
    m_scrollStitcher.clear();
    m_scrollCaptureCount = 0;
    m_scrollUnchangedAttempts = 0;
    m_scrollAutoRunning = false;
    m_scrollCaptureBusy = false;
    m_scrollCaptureActive = true;

    m_scrollManualTimer = new QTimer(this);
    m_scrollManualTimer->setSingleShot(true);
    m_scrollManualTimer->setInterval(60);
    connect(m_scrollManualTimer, &QTimer::timeout,
            this, &SnipOverlay::captureScrollFrame);

    // Bottom floating toolbar with Edit, Save, Cancel, Finish actions
    m_scrollPanel = new ScrollCapturePanel();
    connect(m_scrollPanel, &ScrollCapturePanel::editRequested,
            this, &SnipOverlay::editScrollCapture);
    connect(m_scrollPanel, &ScrollCapturePanel::saveRequested,
            this, &SnipOverlay::saveScrollCapture);
    connect(m_scrollPanel, &ScrollCapturePanel::finishRequested,
            this, &SnipOverlay::finishScrollCapture);
    connect(m_scrollPanel, &ScrollCapturePanel::cancelRequested,
            this, &SnipOverlay::cancelScrollCapture);
    connect(m_scrollPanel, &ScrollCapturePanel::captureRequested,
            this, &SnipOverlay::captureScrollFrame);
    connect(m_scrollPanel, &ScrollCapturePanel::autoScrollToggled,
            this, &SnipOverlay::requestAutoScroll);

    // Right-side live preview panel
    m_scrollPreview = new ScrollCapturePreview();
    connect(m_scrollPanel, &ScrollCapturePanel::statusChanged,
            m_scrollPreview, &ScrollCapturePreview::setStatus);

    // Hide normal selection toolbars and border resize handles
    m_toolbar->hide();
    if (m_sizeToolbar)
        m_sizeToolbar->hide();
    if (m_toolOptions)
        m_toolOptions->hide();
    if (m_selectionBorder)
        m_selectionBorder->hide();

    // Keep overlay active and update layout
    show();
    raise();
    updateScrollCaptureLayout();

    // Punch out the viewport hole so the underlying window receives mouse input
    applyScrollCaptureWindowRegion();

    m_scrollPreview->show();
    m_scrollPreview->raise();
    m_scrollPanel->show();
    m_scrollPanel->raise();

    installScrollInputHook(this, reinterpret_cast<HWND>(m_scrollWindow), m_scrollGlobalSelection);

    // Capture initial frame
    QTimer::singleShot(60, this, &SnipOverlay::captureScrollFrame);
#endif
}

void SnipOverlay::updateScrollCaptureLayout()
{
    QScreen *screen = m_scrollScreen;
    if (!screen)
        screen = QGuiApplication::screenAt(m_scrollGlobalSelection.center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QRect bounds = screen ? screen->availableGeometry() : geometry();
    const QRect globalSel = m_scrollGlobalSelection.isEmpty() ? globalSelection() : m_scrollGlobalSelection;

    if (m_scrollPreview) {
        const int prevW = 260;
        const int prevH = qBound(260, globalSel.height(), qMax(260, bounds.height() - 140));
        m_scrollPreview->setFixedSize(prevW, prevH);

        int prevX = globalSel.right() + 18;
        if (prevX + prevW > bounds.right() - 8) {
            prevX = globalSel.left() - prevW - 18;
            if (prevX < bounds.left() + 8)
                prevX = qBound(bounds.left() + 8, globalSel.right() + 18, bounds.right() - prevW - 8);
        }
        const int prevY = qBound(bounds.top() + 8, globalSel.top(), bounds.bottom() - prevH - 8);
        m_scrollPreview->move(prevX, prevY);
    }

    if (m_scrollPanel) {
        m_scrollPanel->adjustSize();
        const int tbW = m_scrollPanel->width();
        const int tbH = m_scrollPanel->height();
        const int tbX = qBound(bounds.left() + 8,
                               globalSel.center().x() - tbW / 2,
                               bounds.right() - tbW - 8);
        int tbY = globalSel.bottom() + 16;
        if (tbY + tbH > bounds.bottom() - 8)
            tbY = bounds.bottom() - tbH - 8;
        m_scrollPanel->move(tbX, tbY);
    }
}

void SnipOverlay::applyScrollCaptureWindowRegion()
{
#ifdef Q_OS_WIN
    const qreal dpr = devicePixelRatioF();
    const int l = qRound(m_selection.left() * dpr);
    const int t = qRound(m_selection.top() * dpr);
    const int r = qRound((m_selection.right() + 1) * dpr);
    const int b = qRound((m_selection.bottom() + 1) * dpr);
    const int w = qRound(width() * dpr);
    const int h = qRound(height() * dpr);

    HRGN fullRgn = CreateRectRgn(0, 0, w, h);
    HRGN holeRgn = CreateRectRgn(l, t, r, b);
    CombineRgn(fullRgn, fullRgn, holeRgn, RGN_DIFF);
    SetWindowRgn(reinterpret_cast<HWND>(winId()), fullRgn, TRUE);
    DeleteObject(holeRgn);
#endif
}

void SnipOverlay::restoreWindowRegion()
{
#ifdef Q_OS_WIN
    SetWindowRgn(reinterpret_cast<HWND>(winId()), nullptr, TRUE);
#endif
}

void SnipOverlay::scheduleManualScrollCapture()
{
#ifndef Q_OS_WIN
    return;
#else
    if (!m_scrollCaptureActive || m_scrollAutoRunning
        || !m_scrollManualTimer || !m_scrollPanel)
        return;
    if (m_scrollCaptureBusy) {
        // Input during an in-flight capture must delay acceptance, rather
        // than disappear and leave a frame from the middle of a gesture.
        m_scrollStability.reset();
    } else if (!m_scrollManualTimer->isActive()) {
        m_scrollManualTimer->start();
    }
#endif
}

void SnipOverlay::stopScrollInputMonitoring()
{
    ++m_scrollCaptureGeneration;
    m_scrollStability.reset();
#ifdef Q_OS_WIN
    uninstallScrollInputHook(this);
#endif
    if (m_scrollManualTimer) {
        m_scrollManualTimer->stop();
        m_scrollManualTimer->deleteLater();
        m_scrollManualTimer = nullptr;
    }
}

QImage SnipOverlay::captureScrollViewport() const
{
#ifndef Q_OS_WIN
    return {};
#else
    QScreen *screen = m_scrollScreen;
    if (!screen)
        screen = QGuiApplication::screenAt(m_scrollGlobalSelection.center());
    if (!screen)
        return {};

    return platform::captureScreenRegion(screen, m_scrollGlobalSelection);
#endif
}

void SnipOverlay::captureScrollFrame()
{
#ifndef Q_OS_WIN
    return;
#else
    if (!m_scrollCaptureActive || m_scrollCaptureBusy || !m_scrollPanel)
        return;
    if (!IsWindow(reinterpret_cast<HWND>(m_scrollWindow))) {
        requestAutoScroll(false);
        m_scrollPanel->setStatus(QStringLiteral("目标窗口已关闭。"));
        return;
    }

    m_scrollCaptureBusy = true;
    m_scrollPanel->setBusy(true);
    m_scrollStability.reset();
    m_scrollSettleElapsed.start();
    const quint64 generation = ++m_scrollCaptureGeneration;

    const HWND target = reinterpret_cast<HWND>(m_scrollWindow);
    SetForegroundWindow(target);
    BringWindowToTop(target);
    QTimer::singleShot(40, this, [this, generation] { pollScrollCaptureFrame(generation); });
#endif
}

void SnipOverlay::pollScrollCaptureFrame(quint64 generation)
{
    if (generation != m_scrollCaptureGeneration || !m_scrollCaptureActive || !m_scrollPanel)
        return;
    const QImage frame = captureScrollViewport();
    const qint64 elapsed = m_scrollSettleElapsed.elapsed();
    if (frame.isNull() || m_scrollStability.observe(frame, elapsed)) {
        completeScrollCaptureFrame(frame, true);
    } else if (elapsed >= 1000) {
        completeScrollCaptureFrame({}, false);
    } else {
        QTimer::singleShot(40, this, [this, generation] { pollScrollCaptureFrame(generation); });
    }
}

void SnipOverlay::completeScrollCaptureFrame(const QImage &frame, bool settled)
{
    if (!m_scrollCaptureActive || !m_scrollPanel)
        return;
    m_scrollCaptureBusy = false;
    m_scrollStability.reset();
    m_scrollPanel->setBusy(false);
    if (!settled || frame.isNull()) {
        requestAutoScroll(false);
        m_scrollPanel->setStatus(settled ? QStringLiteral("采集画面失败，请重试。")
                                        : QStringLiteral("画面仍在滚动或变化，请停稳后重试；已有长图已保留。"));
        return;
    }
    ++m_scrollCaptureCount;
    const bool added = m_scrollStitcher.append(frame);
    if (added && m_scrollPreview)
        m_scrollPreview->setCapture(m_scrollStitcher);
    m_scrollPanel->setStatus(captureStatus(m_scrollStitcher));

    using Result = core::VerticalScrollStitcher::AppendResult;
    const auto result = m_scrollStitcher.lastResult();
    if (result == Result::Added) {
        m_scrollUnchangedAttempts = 0;
    } else if (result == Result::Unchanged) {
        ++m_scrollUnchangedAttempts;
        if (m_scrollAutoRunning && m_scrollUnchangedAttempts >= 3) {
            requestAutoScroll(false);
            m_scrollPanel->setStatus(QStringLiteral("内容连续未变化，已暂停；若页面继续加载，可再次开启自动滚动。"));
        }
    } else {
        requestAutoScroll(false);
        if (result == Result::HeightLimit) {
            m_scrollPanel->setStatus(QStringLiteral("已达到长图高度上限，请完成或保存。"));
        } else if (result == Result::SizeChanged) {
            m_scrollPanel->setStatus(QStringLiteral("采集区域尺寸发生变化，请重新开始滚动截图。"));
        } else if (result == Result::Ambiguous) {
            m_scrollPanel->setStatus(QStringLiteral("内容重复或特征不足，无法可靠拼接；已有长图已保留。"));
        } else {
            m_scrollPanel->setStatus(QStringLiteral("没有匹配到重叠内容；请向上滚回已采集区域，再缓慢向下继续。"));
        }
    }
    if (!m_scrollAutoRunning)
        return;
    const quint64 generation = m_scrollCaptureGeneration;
    const bool retryViewport = result == Result::Unchanged;
    QTimer::singleShot(140, this, [this, generation, retryViewport] {
        if (generation != m_scrollCaptureGeneration || !m_scrollAutoRunning)
            return;
        // At the bottom, re-sample without sending more wheel input. This
        // allows a short scroll animation or delayed repaint to complete.
        if (retryViewport)
            captureScrollFrame();
        else
            autoScrollStep();
    });
}

void SnipOverlay::requestAutoScroll(bool enabled)
{
#ifndef Q_OS_WIN
    Q_UNUSED(enabled)
#else
    if (!m_scrollCaptureActive || !m_scrollPanel)
        return;
    m_scrollAutoRunning = enabled;
    if (enabled)
        m_scrollUnchangedAttempts = 0;
    if (enabled && m_scrollManualTimer)
        m_scrollManualTimer->stop();
    m_scrollPanel->setAutoRunning(enabled);
    if (enabled) {
        m_scrollPanel->setStatus(QStringLiteral("自动滚动中；可点击“停止自动”暂停。"));
        if (!m_scrollCaptureBusy) {
            if (m_scrollStitcher.size().isEmpty())
                captureScrollFrame();
            else
                autoScrollStep();
        }
    } else if (!m_scrollCaptureBusy) {
        m_scrollPanel->setStatus(captureStatus(m_scrollStitcher));
    }
#endif
}

void SnipOverlay::autoScrollStep()
{
#ifndef Q_OS_WIN
    return;
#else
    if (!m_scrollCaptureActive || !m_scrollAutoRunning || m_scrollCaptureBusy)
        return;
    if (m_scrollCaptureCount >= 80) {
        requestAutoScroll(false);
        m_scrollPanel->setStatus(QStringLiteral("已达到自动滚动上限，请手动完成。"));
        return;
    }
    if (!sendScrollStep()) {
        requestAutoScroll(false);
        m_scrollPanel->setStatus(QStringLiteral("无法向目标窗口发送滚动操作。"));
        return;
    }
    captureScrollFrame();
#endif
}

bool SnipOverlay::sendScrollStep() const
{
#ifndef Q_OS_WIN
    return false;
#else
    const HWND target = reinterpret_cast<HWND>(m_scrollWindow);
    if (!IsWindow(target))
        return false;
    SetForegroundWindow(target);
    BringWindowToTop(target);
    const QPoint point = m_scrollGlobalSelection.center();
    SetCursorPos(point.x(), point.y());
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_WHEEL;
    input.mi.mouseData = static_cast<DWORD>(-WHEEL_DELTA);
    return SendInput(1, &input, sizeof(INPUT)) == 1;
#endif
}

void SnipOverlay::cleanupScrollCapture()
{
    m_scrollAutoRunning = false;
    m_scrollCaptureActive = false;
    m_scrollCaptureBusy = false;
    stopScrollInputMonitoring();
    restoreWindowRegion();

    if (m_scrollPanel) {
        m_scrollPanel->hide();
        m_scrollPanel->deleteLater();
        m_scrollPanel = nullptr;
    }
    if (m_scrollPreview) {
        m_scrollPreview->clear();
        m_scrollPreview->hide();
        m_scrollPreview->deleteLater();
        m_scrollPreview = nullptr;
    }
    restoreScrollCaptureCursor();
    m_scrollWindow = 0;
    m_scrollStitcher.clear();
}

void SnipOverlay::finishScrollCapture()
{
    if (!m_scrollCaptureActive)
        return;
    if (m_scrollPreview)
        m_scrollPreview->clear();
    const QImage result = m_scrollStitcher.takeImage();
    if (result.isNull()) {
        cancelScrollCapture();
        return;
    }

    const QRect savedSelection = m_scrollGlobalSelection;
    cleanupScrollCapture();

    emit selectionCaptured(savedSelection);
    emit screenshotCaptured(result);
    emit screenshotCapturedWithBackground(result, {});
    m_outputEmitted = true;
    QApplication::clipboard()->setImage(result);
    close();
}

void SnipOverlay::editScrollCapture()
{
    if (!m_scrollCaptureActive)
        return;
    if (m_scrollPreview)
        m_scrollPreview->clear();
    const QImage result = m_scrollStitcher.takeImage();
    if (result.isNull()) {
        cancelScrollCapture();
        return;
    }

    const QRect savedSelection = m_scrollGlobalSelection;
    cleanupScrollCapture();

    emit selectionCaptured(savedSelection);
    emit annotateRequested(result);
    close();
}

void SnipOverlay::saveScrollCapture()
{
    if (!m_scrollCaptureActive)
        return;
    if (m_scrollPreview)
        m_scrollPreview->clear();
    const QImage result = m_scrollStitcher.takeImage();
    if (result.isNull()) {
        cancelScrollCapture();
        return;
    }

    cleanupScrollCapture();

    QString defaultName = snipaste::core::expandOutputFilePattern(
        m_outputFileNamePattern.trimmed().isEmpty()
            ? QStringLiteral("Snipaste_$yyyy-MM-dd_HH-mm-ss$.png")
            : m_outputFileNamePattern,
        QDateTime::currentDateTime());
    if (defaultName.trimmed().isEmpty())
        defaultName = QStringLiteral("snipaste.png");

    QString path = QFileDialog::getSaveFileName(
        nullptr,
        QStringLiteral("保存长截图"),
        defaultName,
        QStringLiteral("PNG 图片 (*.png);;JPEG 图片 (*.jpg *.jpeg);;所有文件 (*.*)"));
    if (!path.isEmpty()) {
        if (result.save(path, nullptr, m_outputImageQuality)) {
            emit fileSaved(path);
            emit selectionCaptured(m_scrollGlobalSelection);
            emit screenshotCaptured(result);
            emit screenshotCapturedWithBackground(result, {});
            QApplication::clipboard()->setImage(result);
            m_outputEmitted = true;
        }
    }
    close();
}

void SnipOverlay::cancelScrollCapture()
{
    if (!m_scrollCaptureActive)
        return;
    cleanupScrollCapture();

    show();
    raise();
    activateWindow();
    setFocus();
    if (m_selectionBorder)
        m_selectionBorder->show();
    m_toolbar->show();
    updateToolbarGeometry();
    update();
}

void SnipOverlay::restoreScrollCaptureCursor()
{
#ifdef Q_OS_WIN
    if (m_scrollOriginalCursor != QPoint())
        SetCursorPos(m_scrollOriginalCursor.x(), m_scrollOriginalCursor.y());
#endif
}

} // namespace snipaste::ui
