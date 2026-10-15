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

struct AutomationContext {
    HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ComPtr<IUIAutomation> automation;
    ComPtr<IUIAutomationTreeWalker> walker;
    ComPtr<IUIAutomationCacheRequest> cache;
    HWND accessibleWindow = nullptr;
    DWORD accessibleThread = 0;
    ComPtr<IAccessible> accessibleRoot;

    void ensureAutomation()
    {
        if (automation)
            return;
        if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE)
            return;
        if (FAILED(CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER,
                                     IID_PPV_ARGS(automation.GetAddressOf()))))
            CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                               IID_PPV_ARGS(automation.GetAddressOf()));
        if (!automation)
            return;
        ComPtr<IUIAutomation2> options;
        if (SUCCEEDED(automation.As(&options))) {
            options->put_ConnectionTimeout(30);
            options->put_TransactionTimeout(30);
        }
        automation->get_ControlViewWalker(walker.GetAddressOf());
        if (SUCCEEDED(automation->CreateCacheRequest(cache.GetAddressOf()))) {
            cache->put_TreeScope(TreeScope_Element);
            cache->AddProperty(UIA_BoundingRectanglePropertyId);
            cache->AddProperty(UIA_NativeWindowHandlePropertyId);
            cache->AddProperty(UIA_ProcessIdPropertyId);
        }
    }

    ~AutomationContext()
    {
        cache.Reset();
        walker.Reset();
        automation.Reset();
        accessibleRoot.Reset();
        if (SUCCEEDED(initialized))
            CoUninitialize();
    }
};

bool accessibleBounds(AutomationContext &context, HWND window, POINT point, RECT &bounds, QList<RECT> &hierarchy,
                       ComPtr<IAccessible> &leaf, LONG &leafChild, ULONGLONG deadline)
{
    // AccessibleObjectFromPoint would find our topmost overlay. Start from the
    // underlying HWND instead, then hit-test its client accessibility tree.
    bool found = false;
    ComPtr<IAccessible> current;
    const DWORD thread = GetWindowThreadProcessId(window, nullptr);
    if (context.accessibleWindow == window && context.accessibleThread == thread)
        current = context.accessibleRoot;
    DWORD_PTR objectResult = 0;
    // Bound the initial cross-process WM_GETOBJECT request. A provider can
    // still stall a later MSAA call, so this function belongs on the worker.
    if (!current && !SendMessageTimeoutW(window, WM_GETOBJECT, 0, OBJID_CLIENT,
                              SMTO_ABORTIFHUNG | SMTO_BLOCK, 30, &objectResult))
        return false;
    if (objectResult)
        ObjectFromLresult(static_cast<LRESULT>(objectResult), IID_IAccessible, 0,
                            reinterpret_cast<void **>(current.GetAddressOf()));
    if (!current)
        CreateStdAccessibleObject(window, OBJID_CLIENT,
                                     IID_PPV_ARGS(current.GetAddressOf()));
    context.accessibleWindow = window;
    context.accessibleThread = thread;
    context.accessibleRoot = current;
    if (current) {
        for (int depth = 0; depth < 12 && GetTickCount64() < deadline; ++depth) {
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
                        hierarchy.append(candidate);
                        leaf = current;
                        leafChild = hit.lVal;
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
    return found;
}

bool uiAutomationBounds(AutomationContext &context, HWND window, POINT point,
                         IAccessible *accessible, LONG childId, RECT &bounds,
                         QList<RECT> &hierarchy, ULONGLONG deadline)
{
    context.ensureAutomation();
    if (!context.automation || !context.cache || !context.walker)
        return false;
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    ComPtr<IUIAutomationElement> current;
    if (accessible) {
        // Preserve the MSAA hit beneath the overlay rather than asking UIA to
        // hit-test our own topmost capture window.
        context.automation->ElementFromIAccessibleBuildCache(accessible, childId,
                                        context.cache.Get(), current.GetAddressOf());
    }
    if (!current) {
        context.automation->ElementFromPointBuildCache(point, context.cache.Get(),
                                                        current.GetAddressOf());
    }
    QList<RECT> candidates;
    bool belongsToWindow = false;
    for (int depth = 0; depth < 12 && current && GetTickCount64() < deadline; ++depth) {
        int elementProcess = 0;
        UIA_HWND handle = nullptr;
        if (FAILED(current->get_CachedProcessId(&elementProcess))
            || static_cast<DWORD>(elementProcess) != processId)
            break;
        if (SUCCEEDED(current->get_CachedNativeWindowHandle(&handle)) && handle) {
            const HWND native = reinterpret_cast<HWND>(handle);
            if (GetAncestor(native, GA_ROOT) != window)
                break;
            belongsToWindow = true;
        }
        RECT candidate{};
        if (SUCCEEDED(current->get_CachedBoundingRectangle(&candidate))
            && candidate.right > candidate.left && candidate.bottom > candidate.top
            && PtInRect(&candidate, point)) {
            candidates.append(candidate);
        }
        if (reinterpret_cast<HWND>(handle) == window)
            break;
        ComPtr<IUIAutomationElement> parent;
        if (FAILED(context.walker->GetParentElementBuildCache(current.Get(),
                                       context.cache.Get(), parent.GetAddressOf())))
            break;
        current = parent;
    }
    if (!belongsToWindow || candidates.isEmpty())
        return false;
    bounds = candidates.first();
    hierarchy.append(candidates);
    return true;
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

CaptureTargetQuery prepareCaptureTargetQuery(QPoint localPosition, QSize logicalScreenSize,
                                             WId overlayWindow, bool elementMode,
                                             const QStringList &excludedProcesses,
                                             const QStringList &unstableProcesses)
{
    CaptureTargetQuery query;
    query.localPosition = localPosition;
    query.logicalScreenSize = logicalScreenSize;
    query.elementMode = elementMode;
    query.excludedProcesses = excludedProcesses;
    query.unstableProcesses = unstableProcesses;
#ifdef Q_OS_WIN
    if (logicalScreenSize.isEmpty())
        return query;
    const HWND overlay = reinterpret_cast<HWND>(overlayWindow);
    MONITORINFO monitor{sizeof(MONITORINFO)};
    if (!GetMonitorInfo(MonitorFromWindow(overlay, MONITOR_DEFAULTTONEAREST), &monitor))
        return query;
    query.overlayWindow = reinterpret_cast<quintptr>(overlay);
    query.nativeOrigin = QPoint(monitor.rcMonitor.left, monitor.rcMonitor.top);
    query.nativeScreenSize = QSize(monitor.rcMonitor.right - monitor.rcMonitor.left,
                                     monitor.rcMonitor.bottom - monitor.rcMonitor.top);
    query.valid = !query.nativeScreenSize.isEmpty();
#else
    Q_UNUSED(overlayWindow)
#endif
    return query;
}

QList<QRect> captureTargetHierarchy(const CaptureTargetQuery &query, quintptr *nativeWindow)
{
    if (nativeWindow)
        *nativeWindow = 0;
#ifdef Q_OS_WIN
    if (!query.valid || query.logicalScreenSize.isEmpty() || query.nativeScreenSize.isEmpty())
        return {};
    const QPoint localPosition = query.localPosition;
    const QSize logicalScreenSize = query.logicalScreenSize;
    const qreal sx = qreal(query.nativeScreenSize.width()) / logicalScreenSize.width();
    const qreal sy = qreal(query.nativeScreenSize.height()) / logicalScreenSize.height();
    const POINT point{query.nativeOrigin.x() + qRound(localPosition.x() * sx),
                      query.nativeOrigin.y() + qRound(localPosition.y() * sy)};
    WindowSearch search{point, reinterpret_cast<HWND>(query.overlayWindow)};
    EnumWindows(findUnderlyingWindow, reinterpret_cast<LPARAM>(&search));
    if (!search.found)
        return {};
    if (nativeWindow)
        *nativeWindow = reinterpret_cast<quintptr>(search.found);
    const QRect screenRect(QPoint(), logicalScreenSize);
    if (processMatches(search.found, query.excludedProcesses))
        return {screenRect};
    RECT target = search.bounds;
    QList<RECT> nativeTargets{search.bounds};
    if (query.elementMode) {
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
        const bool unstable = processMatches(search.found, query.unstableProcesses);
        // UIA can synchronously call a provider on the target's UI thread. A
        // compatibility call on that same thread must retain the HWND result.
        if (query.accessibility && !unstable
            && GetWindowThreadProcessId(child, nullptr) != GetCurrentThreadId()) {
            thread_local AutomationContext context;
            if (SUCCEEDED(context.initialized) || context.initialized == RPC_E_CHANGED_MODE) {
                const ULONGLONG deadline = GetTickCount64() + 45;
                ComPtr<IAccessible> leaf;
                LONG leafChild = CHILDID_SELF;
                const bool found = accessibleBounds(context, child, point, element, nativeTargets,
                                                      leaf, leafChild, deadline);
                if (found)
                    target = element;
                // MSAA already found a specific control. Do not add UIA
                // initialization and ancestor RPCs to every successful hit.
                RECT client{};
                GetClientRect(child, &client);
                MapWindowPoints(child, nullptr, reinterpret_cast<POINT *>(&client), 2);
                const bool specific = found && (element.left > client.left || element.top > client.top
                    || element.right < client.right || element.bottom < client.bottom);
                if (!specific && GetTickCount64() < deadline
                    && uiAutomationBounds(context, search.found, point, leaf.Get(), leafChild,
                                             element, nativeTargets, deadline))
                    target = element;
            }
        }
        RECT clipped{};
        if (IntersectRect(&clipped, &target, &search.bounds))
            target = clipped;
    }
    nativeTargets.append(target);
    QList<QRect> targets;
    for (const RECT &native : nativeTargets) {
        RECT clipped{};
        if (!IntersectRect(&clipped, &native, &search.bounds)) continue;
        const int left = qFloor((clipped.left - query.nativeOrigin.x()) / sx);
        const int top = qFloor((clipped.top - query.nativeOrigin.y()) / sy);
        const int right = qCeil((clipped.right - query.nativeOrigin.x()) / sx);
        const int bottom = qCeil((clipped.bottom - query.nativeOrigin.y()) / sy);
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
    Q_UNUSED(query)
    return {};
#endif
}

QList<QRect> captureTargetHierarchyAt(QPoint localPosition, QSize logicalScreenSize,
                                      WId overlayWindow, bool elementMode,
                                      const QStringList &excludedProcesses,
                                      const QStringList &unstableProcesses,
                                      quintptr *nativeWindow)
{
    return captureTargetHierarchy(prepareCaptureTargetQuery(localPosition, logicalScreenSize,
                                       overlayWindow, elementMode, excludedProcesses,
                                       unstableProcesses), nativeWindow);
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
