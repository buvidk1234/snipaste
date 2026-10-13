#include "platform/windows/capture_target.h"
#include <QtMath>
#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#include <oleacc.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#endif

namespace snipaste::platform {

#ifdef Q_OS_WIN
namespace {
using Microsoft::WRL::ComPtr;

struct WindowSearch {
    POINT point;
    HWND ignored;
    HWND found = nullptr;
    RECT bounds{};
};

BOOL CALLBACK findUnderlyingWindow(HWND window, LPARAM data)
{
    auto &search = *reinterpret_cast<WindowSearch *>(data);
    if (window == search.ignored || !IsWindowVisible(window) || IsIconic(window)
        || (GetWindowLongPtr(window, GWL_EXSTYLE) & WS_EX_TRANSPARENT))
        return TRUE;
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)))
        && cloaked)
        return TRUE;
    RECT bounds{};
    if (FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS,
                                     &bounds, sizeof(bounds)))
        && !GetWindowRect(window, &bounds))
        return TRUE;
    if (!PtInRect(&bounds, search.point))
        return TRUE;
    search.found = window;
    search.bounds = bounds;
    return FALSE;
}

bool accessibleBounds(HWND window, POINT point, RECT &bounds, QList<RECT> *hierarchy = nullptr)
{
    // AccessibleObjectFromPoint would find our topmost overlay. Start from the
    // underlying HWND instead, then hit-test its client accessibility tree.
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool found = false;
    {
        ComPtr<IAccessible> current;
        if (SUCCEEDED(AccessibleObjectFromWindow(window, static_cast<DWORD>(OBJID_CLIENT),
                                                IID_PPV_ARGS(current.GetAddressOf())))) {
            for (int depth = 0; depth < 16; ++depth) {
                VARIANT hit;
                VariantInit(&hit);
                if (FAILED(current->accHitTest(point.x, point.y, &hit))) {
                    VariantClear(&hit);
                    break;
                }
                ComPtr<IAccessible> next;
                if (hit.vt == VT_DISPATCH && hit.pdispVal) {
                    hit.pdispVal->QueryInterface(IID_PPV_ARGS(next.GetAddressOf()));
                } else if (hit.vt == VT_I4) {
                    LONG x = 0, y = 0, width = 0, height = 0;
                    if (SUCCEEDED(current->accLocation(&x, &y, &width, &height, hit))
                        && width > 0 && height > 0) {
                        RECT candidate{x, y, x + width, y + height};
                        if (PtInRect(&candidate, point)) {
                            bounds = candidate;
                            if (hierarchy) hierarchy->append(candidate);
                            found = true;
                        }
                    }
                    if (hit.lVal != CHILDID_SELF) {
                        ComPtr<IDispatch> child;
                        if (SUCCEEDED(current->get_accChild(hit, child.GetAddressOf())) && child)
                            child.As(&next);
                    }
                }
                VariantClear(&hit);
                if (!next || next.Get() == current.Get())
                    break;
                current = next;
            }
        }
    }
    if (SUCCEEDED(initialized))
        CoUninitialize();
    return found;
}

bool uiAutomationBounds(POINT point, RECT &bounds, QList<RECT> *hierarchy = nullptr)
{
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool found = false;
    ComPtr<IUIAutomation> automation;
    if (SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(automation.GetAddressOf())))) {
        ComPtr<IUIAutomationTreeWalker> walker;
        automation->get_ControlViewWalker(walker.GetAddressOf());
        ComPtr<IUIAutomationElement> current;
        if (SUCCEEDED(automation->ElementFromPoint(POINT{point.x, point.y},
                                                   current.GetAddressOf())) && current) {
            for (int depth = 0; depth < 16 && current; ++depth) {
                RECT candidate{};
                if (SUCCEEDED(current->get_CurrentBoundingRectangle(&candidate))
                    && candidate.right > candidate.left && candidate.bottom > candidate.top
                    && PtInRect(&candidate, point)) {
                    bounds = candidate;
                    if (hierarchy)
                        hierarchy->append(candidate);
                    found = true;
                }
                if (!walker)
                    break;
                ComPtr<IUIAutomationElement> parent;
                if (FAILED(walker->GetParentElement(current.Get(), parent.GetAddressOf())))
                    break;
                current = parent;
            }
        }
    }
    if (SUCCEEDED(initialized))
        CoUninitialize();
    return found;
}

QString processExecutablePath(HWND window)
{
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (!processId)
        return {};
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process)
        return {};
    wchar_t buffer[32768]{};
    DWORD length = 32768;
    const BOOL ok = QueryFullProcessImageNameW(process, 0, buffer, &length);
    CloseHandle(process);
    if (!ok || length == 0)
        return {};
    return QString::fromWCharArray(buffer, static_cast<int>(length));
}

bool wildcardMatch(const QString &value, const QString &pattern, int vi = 0, int pi = 0)
{
    if (pi == pattern.size())
        return vi == value.size();
    if (pattern.at(pi) == QChar('*'))
        return wildcardMatch(value, pattern, vi, pi + 1)
            || (vi < value.size() && wildcardMatch(value, pattern, vi + 1, pi));
    return vi < value.size()
        && (pattern.at(pi) == QChar('?') || pattern.at(pi) == value.at(vi))
        && wildcardMatch(value, pattern, vi + 1, pi + 1);
}

bool processMatches(HWND window, const QStringList &patterns)
{
    if (patterns.isEmpty())
        return false;
    const QString path = processExecutablePath(window).toCaseFolded();
    const int slash = qMax(path.lastIndexOf(QChar('/')), path.lastIndexOf(QChar('\\')));
    const QString fileName = slash >= 0 ? path.mid(slash + 1) : path;
    for (const QString &raw : patterns) {
        const QString pattern = raw.trimmed().toCaseFolded();
        if (pattern.isEmpty())
            continue;
        if (pattern.contains(QChar('*')) || pattern.contains(QChar('?'))) {
            if (wildcardMatch(path, pattern) || wildcardMatch(fileName, pattern))
                return true;
        } else if (path.contains(pattern) || fileName == pattern) {
            return true;
        }
    }
    return false;
}
} // namespace
#endif

QList<QRect> captureTargetHierarchyAt(QPoint localPosition, QSize logicalScreenSize,
                                      WId overlayWindow, bool elementMode,
                                      const QStringList &excludedProcesses,
                                      const QStringList &unstableProcesses,
                                      quintptr *nativeWindow)
{
#ifdef Q_OS_WIN
    if (nativeWindow)
        *nativeWindow = 0;
    if (logicalScreenSize.isEmpty())
        return {};
    const HWND overlay = reinterpret_cast<HWND>(overlayWindow);
    MONITORINFO monitor{sizeof(MONITORINFO)};
    if (!GetMonitorInfo(MonitorFromWindow(overlay, MONITOR_DEFAULTTONEAREST), &monitor))
        return {};
    const qreal sx = qreal(monitor.rcMonitor.right - monitor.rcMonitor.left) / logicalScreenSize.width();
    const qreal sy = qreal(monitor.rcMonitor.bottom - monitor.rcMonitor.top) / logicalScreenSize.height();
    const POINT point{monitor.rcMonitor.left + qRound(localPosition.x() * sx),
                      monitor.rcMonitor.top + qRound(localPosition.y() * sy)};
    WindowSearch search{point, overlay};
    EnumWindows(findUnderlyingWindow, reinterpret_cast<LPARAM>(&search));
    if (!search.found)
        return {};
    if (nativeWindow)
        *nativeWindow = reinterpret_cast<quintptr>(search.found);
    const QRect screenRect(QPoint(), logicalScreenSize);
    if (processMatches(search.found, excludedProcesses))
        return {screenRect};
    RECT target = search.bounds;
    QList<RECT> nativeTargets{search.bounds};
    if (elementMode) {
        HWND child = search.found;
        for (int depth = 0; depth < 32; ++depth) {
            POINT clientPoint = point;
            ScreenToClient(child, &clientPoint);
            HWND next = ChildWindowFromPointEx(child, clientPoint,
                CWP_SKIPINVISIBLE | CWP_SKIPDISABLED | CWP_SKIPTRANSPARENT);
            if (!next || next == child)
                break;
            child = next;
            RECT bounds{};
            if (GetWindowRect(child, &bounds)) nativeTargets.append(bounds);
        }
        RECT element{};
        if (GetWindowRect(child, &element) && PtInRect(&element, point))
            target = element;
        const bool unstable = processMatches(search.found, unstableProcesses);
        if (!unstable && accessibleBounds(child, point, element, &nativeTargets))
            target = element;
        if (!unstable && uiAutomationBounds(point, element, &nativeTargets))
            target = element;
        RECT clipped{};
        if (IntersectRect(&clipped, &target, &search.bounds))
            target = clipped;
    }
    nativeTargets.append(target);
    QList<QRect> targets;
    for (const RECT &native : nativeTargets) {
        RECT clipped{};
        if (!IntersectRect(&clipped, &native, &search.bounds)) continue;
        const int left = qFloor((clipped.left - monitor.rcMonitor.left) / sx);
        const int top = qFloor((clipped.top - monitor.rcMonitor.top) / sy);
        const int right = qCeil((clipped.right - monitor.rcMonitor.left) / sx);
        const int bottom = qCeil((clipped.bottom - monitor.rcMonitor.top) / sy);
        const QRect candidate = QRect(left, top, right - left, bottom - top)
            .intersected(QRect(QPoint(), logicalScreenSize));
        if (candidate.contains(localPosition) && !targets.contains(candidate)) targets.append(candidate);
    }
    if (!targets.contains(screenRect)) targets.append(screenRect);
    std::sort(targets.begin(), targets.end(), [](QRect a, QRect b) {
        return qint64(a.width()) * a.height() < qint64(b.width()) * b.height();
    });
    return targets;
#else
    Q_UNUSED(localPosition)
    Q_UNUSED(logicalScreenSize)
    Q_UNUSED(overlayWindow)
    Q_UNUSED(elementMode)
    Q_UNUSED(excludedProcesses)
    Q_UNUSED(unstableProcesses)
    Q_UNUSED(nativeWindow)
    return {};
#endif
}

QRect captureTargetAt(QPoint localPosition, QSize logicalScreenSize, WId overlayWindow, bool elementMode,
                      const QStringList &excludedProcesses,
                      const QStringList &unstableProcesses,
                      quintptr *nativeWindow)
{
    const auto hierarchy = captureTargetHierarchyAt(localPosition, logicalScreenSize, overlayWindow,
                                                    elementMode, excludedProcesses, unstableProcesses,
                                                    nativeWindow);
    return hierarchy.isEmpty() ? QRect() : hierarchy.first();
}

} // namespace snipaste::platform
