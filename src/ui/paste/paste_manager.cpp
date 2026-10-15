#include "ui/paste/paste_manager.h"
#include "platform/keyboard.h"
#include "ui/clipboard_output.h"

#include "ui/paste/paste_window.h"

#include <QApplication>
#include <QClipboard>

namespace snipaste::ui
{
void PasteManager::forEachWindow(const std::function<void(PasteWindow *)> &action)
{
    prune();
    for (const auto &group : m_groups)
        for (const auto &window : group.windows)
            if (window)
                action(window.data());
}

void PasteManager::applyWindowSettings(PasteWindow *window)
{
    window->setPasteManager(this);
    window->setResizeEnabled(m_resizeEnabled);
    window->setExchangeNavigationKeys(m_exchangeNavigationKeys);
    window->setMaxNetworkImageBytes(m_maxNetworkImageBytes);
    window->setMaxSize(m_maxSize);
    window->setThumbnailSize(m_thumbnailSize);
    window->setWheelCenter(m_wheelCenter);
    window->setResetTransform(m_resetTransform);
    window->setDisableDpiScaling(m_disableDpiScaling);
    window->setHelpKeys(m_helpKeys);
    window->setCloseConfirm(m_closeConfirm);
    window->setDestroyConfirm(m_destroyConfirm);
    window->setShadowEnabled(m_shadowEnabled);
    window->setBackgroundMode(m_backgroundMode);
    window->setSmoothScale(m_smoothScale);
    window->setEscapeActions(m_escapeAction, m_shiftEscapeAction);
    window->setMouseActions(m_mouseActions);
    window->setVirtualDesktopPinned(m_virtualDesktopPinned);
    window->setActivateOnShow(m_activateNew);
    window->setFlashTransparency(m_flashTransparency);
    window->setKeepOnDrag(m_keepOnDrag);
    window->setWindowOpacity(m_defaultOpacity);
    window->setCopyAsFileShortcutEnabled(m_copyAsFileMode != static_cast<int>(CopyAsFileMode::Disabled));
}

namespace
{

quint64 imageFingerprint(const QImage &image)
{
    if (image.isNull())
        return 0;
    // FNV-1a keeps only a 64-bit signature instead of retaining the entire
    // captured image solely to recognize the next paste.  Hash rows directly
    // so the comparison does not allocate another image-sized QByteArray.
    quint64 hash = 1469598103934665603ULL;
    const auto mix = [&hash](quint64 value) {
        hash ^= value;
        hash *= 1099511628211ULL;
    };
    mix(static_cast<quint64>(image.width()));
    mix(static_cast<quint64>(image.height()));
    // QClipboard may hand the same pixels back in a different QImage format
    // (for example RGB32 instead of ARGB32_Premultiplied).  Hash the logical
    // QRgb value returned by QImage::pixel(), rather than the storage bytes,
    // so capture-position detection survives that conversion without keeping
    // a second, canonical full-size image resident.
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
            mix(static_cast<quint64>(image.pixel(x, y)));
    }
    return hash;
}

} // namespace

PasteManager::PasteManager(QObject *parent) : QObject(parent)
{
    m_groups.append(Group{QStringLiteral("默认组"), {}});
}

PasteManager::~PasteManager()
{
    // Top-level widgets have no QWidget parent; the manager owns their lifetime.
    for (const Group &group : m_groups)
    {
        for (const QPointer<PasteWindow> &window : group.windows)
        {
            if (window)
            {
                window->disconnect(this);
                delete window.data();
            }
        }
    }
}

PasteWindow *PasteManager::createPaste(const QImage &image)
{
    if (image.isNull())
        return nullptr;

    prune();
    if (m_groups.isEmpty())
        m_groups.append(Group{QStringLiteral("默认组"), {}});

    auto *window = new PasteWindow(image);
    applyWindowSettings(window);
    m_groups[m_activeGroup].windows.append(window);
    connectWindow(window);
    QImage pixels = image;
    pixels.setDevicePixelRatio(1);
    // Keep the last capture region available for direct createPaste() calls;
    // the awaiting flag only controls clipboard-priority handling below.
    const bool fromCapture =
        m_captureRegion.isValid() && pixels.size() == m_captureSize &&
        (image.cacheKey() == m_captureImageKey || imageFingerprint(pixels) == m_captureFingerprint);
    const QPoint capturePosition = m_captureRegion.topLeft();
    if (fromCapture)
    {
        m_captureAwaitingPaste = false;
    }
    if (fromCapture && m_useCapturePosition)
        window->showAtPosition(capturePosition);
    else
        window->showAtCursor();
    window->setActivateOnShow(true);
    if (m_flashNew)
        window->flash();
    return window;
}

PasteWindow *PasteManager::createPasteAtPosition(const QImage &image, const QPoint &globalPosition)
{
    PasteWindow *window = createPaste(image);
    if (window)
    {
        window->setActivateOnShow(m_activateNew);
        window->showAtPosition(globalPosition);
        window->setActivateOnShow(true);
    }
    return window;
}

void PasteManager::rememberCapture(const QImage &image, const QRect &globalRegion)
{
    QImage pixels = image;
    pixels.setDevicePixelRatio(1);
    m_captureFingerprint = imageFingerprint(pixels);
    m_captureImageKey = image.cacheKey();
    m_captureSize = pixels.size();
    m_captureRegion = globalRegion;
    m_captureAwaitingPaste = !image.isNull() && globalRegion.isValid();
}

void PasteManager::setUseCapturePosition(bool enabled)
{
    m_useCapturePosition = enabled;
}

void PasteManager::setDefaultOpacity(qreal opacity)
{
    m_defaultOpacity = qBound<qreal>(0.1, opacity, 1.0);
}

void PasteManager::setShadowEnabled(bool enabled)
{
    m_shadowEnabled = enabled;
    forEachWindow([&](PasteWindow *window) { window->setShadowEnabled(enabled); });
}

void PasteManager::setResizeEnabled(bool enabled)
{
    m_resizeEnabled = enabled;
    forEachWindow([&](PasteWindow *window) { window->setResizeEnabled(enabled); });
}

void PasteManager::setExchangeNavigationKeys(bool enabled)
{
    m_exchangeNavigationKeys = enabled;
    forEachWindow([&](PasteWindow *window) { window->setExchangeNavigationKeys(enabled); });
}

void PasteManager::setMaxNetworkImageBytes(qint64 bytes)
{
    m_maxNetworkImageBytes = qBound<qint64>(0, bytes, 1024LL * 1024 * 1024);
    forEachWindow([&](PasteWindow *window) { window->setMaxNetworkImageBytes(m_maxNetworkImageBytes); });
}

void PasteManager::setMagneticAttachThreshold(int pixels)
{
    m_magneticAttachThreshold = qBound(0, pixels, 500);
}

void PasteManager::setMaxSize(int pixels)
{
    m_maxSize = qBound(0, pixels, 100000);
    forEachWindow([&](PasteWindow *window) { window->setMaxSize(m_maxSize); });
}

void PasteManager::setThumbnailSize(const QSize &size)
{
    m_thumbnailSize = QSize(qBound(1, size.width(), 4096), qBound(1, size.height(), 4096));
    forEachWindow([&](PasteWindow *window) { window->setThumbnailSize(m_thumbnailSize); });
}

void PasteManager::setWheelCenter(bool enabled)
{
    m_wheelCenter = enabled;
    forEachWindow([&](PasteWindow *window) { window->setWheelCenter(enabled); });
}

void PasteManager::setResetTransform(bool enabled)
{
    m_resetTransform = enabled;
    forEachWindow([&](PasteWindow *window) { window->setResetTransform(enabled); });
}

void PasteManager::setHelpKeys(bool enabled)
{
    m_helpKeys = enabled;
    forEachWindow([&](PasteWindow *window) { window->setHelpKeys(enabled); });
}

void PasteManager::setDisableDpiScaling(bool enabled)
{
    m_disableDpiScaling = enabled;
    forEachWindow([&](PasteWindow *window) { window->setDisableDpiScaling(enabled); });
}

void PasteManager::setCloseConfirm(bool enabled)
{
    m_closeConfirm = enabled;
    forEachWindow([&](PasteWindow *window) { window->setCloseConfirm(enabled); });
}

void PasteManager::setDestroyConfirm(bool enabled)
{
    m_destroyConfirm = enabled;
    forEachWindow([&](PasteWindow *window) { window->setDestroyConfirm(enabled); });
}

void PasteManager::setCopyAsFileMode(int mode)
{
    m_copyAsFileMode = static_cast<int>(copyAsFileModeFromInt(mode));
    forEachWindow([&](PasteWindow *window) {
        window->setCopyAsFileShortcutEnabled(m_copyAsFileMode != static_cast<int>(CopyAsFileMode::Disabled));
    });
}

void PasteManager::setBackgroundMode(const QString &mode)
{
    m_backgroundMode = mode.trimmed().isEmpty() ? QStringLiteral("伪透明") : mode.trimmed();
    forEachWindow([&](PasteWindow *window) { window->setBackgroundMode(m_backgroundMode); });
}

void PasteManager::setSmoothScale(bool enabled)
{
    m_smoothScale = enabled;
    forEachWindow([&](PasteWindow *window) { window->setSmoothScale(enabled); });
}

void PasteManager::setEscapeActions(const QString &escapeAction, const QString &shiftEscapeAction)
{
    m_escapeAction = escapeAction;
    m_shiftEscapeAction = shiftEscapeAction;
    forEachWindow(
        [&](PasteWindow *window) { window->setEscapeActions(m_escapeAction, m_shiftEscapeAction); });
}

void PasteManager::setMouseActions(const QStringList &actions)
{
    m_mouseActions = actions;
    forEachWindow([&](PasteWindow *window) { window->setMouseActions(m_mouseActions); });
}

void PasteManager::setVirtualDesktopPinned(bool enabled)
{
    m_virtualDesktopPinned = enabled;
    forEachWindow([&](PasteWindow *window) { window->setVirtualDesktopPinned(enabled); });
}

void PasteManager::setHiddenForFileDialog(bool hidden)
{
    if (hidden == m_hiddenForFileDialog)
        return;
    m_hiddenForFileDialog = hidden;
    if (hidden)
    {
        m_fileDialogVisible.clear();
        prune();
        for (const Group &group : m_groups)
        {
            for (const QPointer<PasteWindow> &window : group.windows)
            {
                if (window && window->isVisible() && !window->isSoloHidden())
                {
                    m_fileDialogVisible.append(window);
                    window->hide();
                }
            }
        }
    }
    else
    {
        for (const QPointer<PasteWindow> &window : m_fileDialogVisible)
            if (window && window->state() == PasteWindow::State::Active)
                window->showExisting();
        m_fileDialogVisible.clear();
    }
}

void PasteManager::setFlashNew(bool enabled)
{
    m_flashNew = enabled;
}

void PasteManager::setActivateNew(bool enabled)
{
    m_activateNew = enabled;
}

void PasteManager::setFlashTransparency(bool enabled)
{
    m_flashTransparency = enabled;
    forEachWindow([&](PasteWindow *window) { window->setFlashTransparency(enabled); });
}

void PasteManager::setFlashGroupSwitch(bool enabled)
{
    m_flashGroupSwitch = enabled;
}

void PasteManager::setHideAllOnPasteKey(bool enabled)
{
    m_hideAllOnPasteKey = enabled;
}

void PasteManager::setKeepOnDrag(bool enabled)
{
    m_keepOnDrag = enabled;
    forEachWindow([&](PasteWindow *window) { window->setKeepOnDrag(enabled); });
}

void PasteManager::connectWindow(PasteWindow *window)
{
    QObject::connect(window, &QObject::destroyed, this, [this] { prune(); });
    QObject::connect(window, &PasteWindow::activated, this, &PasteManager::onWindowActivated);
    QObject::connect(window, &PasteWindow::pasteClosed, this, &PasteManager::onWindowClosed);
    QObject::connect(window, &PasteWindow::selectAllRequested, this,
                     [this](PasteWindow *) { selectAllVisible(); });
    QObject::connect(window, &PasteWindow::copyRequested, this, [this](PasteWindow *source) {
        if (!source)
            return;
        copyImageToClipboard(source->displayedImage(), m_copyAsFileMode, false, ClipboardImageSource::Paste);
    });
    QObject::connect(window, &PasteWindow::copyAsFileRequested, this, [this](PasteWindow *source) {
        if (source)
            copyImageToClipboard(source->displayedImage(), m_copyAsFileMode, true,
                                 ClipboardImageSource::Paste);
    });
    QObject::connect(window, &PasteWindow::moveRequested, this,
                     [this](PasteWindow *source, const QPoint &delta) {
                         if (!source)
                             return;
                         if (m_selected.contains(source) && m_selected.size() > 1)
                             moveSelectedBy(delta);
                         else
                         {
                             source->move(source->pos() + delta);
                             snapWindowToNearby(source);
                         }
                     });
    QObject::connect(window, &PasteWindow::positionChanged, this,
                     [this](PasteWindow *source) { snapWindowToNearby(source); });
    QObject::connect(window, &PasteWindow::wheelTransformRequested, this,
                     [this](PasteWindow *source, qreal scaleFactor, qreal opacityDelta) {
                         if (!source || !m_selected.contains(source) || m_selected.size() < 2)
                             return;
                         for (const QPointer<PasteWindow> &selected : m_selected)
                         {
                             if (!selected || selected == source ||
                                 selected->state() != PasteWindow::State::Active)
                                 continue;
                             if (scaleFactor != qreal(1.0))
                                 selected->scaleBy(scaleFactor);
                             if (opacityDelta != qreal(0.0))
                                 selected->adjustOpacity(opacityDelta);
                         }
                     });
}

void PasteManager::snapWindowToNearby(PasteWindow *window)
{
    if (!window || m_magneticAttachThreshold <= 0)
        return;
    prune();
    QRect candidate = window->frameGeometry();
    const int threshold = m_magneticAttachThreshold;
    const auto snap = [threshold](int first, int second) { return qAbs(first - second) <= threshold; };
    for (const Group &group : m_groups)
    {
        for (const QPointer<PasteWindow> &other : group.windows)
        {
            if (!other || other == window || !other->isVisible())
                continue;
            const QRect target = other->frameGeometry();
            if (snap(candidate.left(), target.right() + 1))
                candidate.moveLeft(target.right() + 1);
            else if (snap(candidate.right() + 1, target.left()))
                candidate.moveLeft(target.left() - candidate.width());
            if (snap(candidate.top(), target.bottom() + 1))
                candidate.moveTop(target.bottom() + 1);
            else if (snap(candidate.bottom() + 1, target.top()))
                candidate.moveTop(target.top() - candidate.height());
        }
    }
    if (candidate.topLeft() != window->frameGeometry().topLeft())
        window->move(candidate.topLeft());
}

PasteWindow *PasteManager::pasteClipboard(const QImage &image)
{
    prune();
    if (m_hideAllOnPasteKey)
    {
        for (const Group &group : m_groups)
        {
            for (const QPointer<PasteWindow> &window : group.windows)
            {
                if (window && window->isVisible() && window->isFlashing())
                {
                    hideAll();
                    return window;
                }
            }
        }
    }
    QImage pixels = image;
    pixels.setDevicePixelRatio(1);
    // Direct capture-to-pin calls share the same QImage. Its content key is
    // sufficient there; clipboard conversions still verify every pixel.
    const quint64 fingerprint =
        image.cacheKey() == m_captureImageKey ? m_captureFingerprint : imageFingerprint(pixels);
    // A new capture is a new paste even if its pixels happen to equal an older
    // closed pin (for example, two identical icons at different positions).
    if (m_captureAwaitingPaste && m_captureRegion.isValid() && pixels.size() == m_captureSize &&
        fingerprint == m_captureFingerprint)
    {
        m_lastClipboardFingerprint = fingerprint;
        m_lastClipboardSize = pixels.size();
        return createPaste(image);
    }
    if (!pixels.isNull())
    {
        const bool repeated =
            fingerprint == m_lastClipboardFingerprint && pixels.size() == m_lastClipboardSize;
        if (!repeated)
        {
            m_lastClipboardFingerprint = fingerprint;
            m_lastClipboardSize = pixels.size();
        }
        else if (!m_closed.isEmpty())
        {
            PasteWindow *window = restoreClosedAt(m_closed.size() - 1);
            if (window && m_flashNew)
                window->flash();
            return window;
        }
        for (qsizetype index = m_closed.size() - 1; index >= 0; --index)
        {
            QImage source = m_closed.at(index)->sourceImage();
            source.setDevicePixelRatio(1);
            if (source == pixels)
            {
                PasteWindow *window = restoreClosedAt(index);
                if (window && m_flashNew)
                    window->flash();
                return window;
            }
        }
        if (repeated)
        {
            for (const Group &group : m_groups)
            {
                for (const QPointer<PasteWindow> &window : group.windows)
                {
                    if (!window || !window->isVisible())
                        continue;
                    QImage source = window->sourceImage();
                    source.setDevicePixelRatio(1);
                    if (source == pixels)
                    {
                        if (m_flashNew)
                            window->flash();
                        return window;
                    }
                }
            }
        }
        return createPaste(image);
    }
    if (!m_closed.isEmpty())
    {
        PasteWindow *window = restoreClosedAt(m_closed.size() - 1);
        if (window && m_flashNew)
            window->flash();
        return window;
    }
    return createPaste(image);
}

PasteWindow *PasteManager::restoreClosedAt(qsizetype index)
{
    PasteWindow *window = m_closed.takeAt(index);
    window->restoreClosed();
    return window;
}

void PasteManager::setRestoreCount(int count)
{
    m_restoreCount = qBound(0, count, 99);
    prune();
    trimClosed();
}

void PasteManager::onWindowClosed(PasteWindow *window)
{
    m_selected.removeAll(window);
    m_closed.removeAll(window);
    m_closed.append(window);
    trimClosed();
}

void PasteManager::trimClosed()
{
    while (m_closed.size() > m_restoreCount)
    {
        const QPointer<PasteWindow> window = m_closed.takeFirst();
        if (window)
            window->close();
    }
}

void PasteManager::toggleAll()
{
    prune();
    const bool anyVisible = visibleCount() > 0;
    for (Group &group : m_groups)
    {
        for (const QPointer<PasteWindow> &window : group.windows)
        {
            if (!window)
                continue;
            if (anyVisible)
                window->hide();
            else
                window->showExisting();
        }
    }
}

void PasteManager::showAll()
{
    forEachWindow([&](PasteWindow *window) { window->showExisting(); });
}

void PasteManager::hideAll()
{
    forEachWindow([&](PasteWindow *window) { window->hide(); });
}

void PasteManager::switchGroup()
{
    prune();
    if (m_groups.size() < 2)
        m_groups.append(Group{QStringLiteral("组 2"), {}});

    setGroupVisible(m_activeGroup, false);
    m_activeGroup = (m_activeGroup + 1) % m_groups.size();
    setGroupVisible(m_activeGroup, true);
    if (m_flashGroupSwitch)
    {
        for (const QPointer<PasteWindow> &window : m_groups[m_activeGroup].windows)
            if (window && window->isVisible())
                window->flash();
    }
}

void PasteManager::switchGroup(int index)
{
    prune();
    if (index < 0 || index >= m_groups.size() || index == m_activeGroup)
        return;
    setGroupVisible(m_activeGroup, false);
    m_activeGroup = index;
    setGroupVisible(m_activeGroup, true);
    if (m_flashGroupSwitch)
    {
        for (const QPointer<PasteWindow> &window : m_groups[m_activeGroup].windows)
            if (window && window->isVisible())
                window->flash();
    }
}

int PasteManager::createGroup(const QString &name)
{
    prune();
    QString groupName = name.trimmed();
    if (groupName.isEmpty())
        groupName = QStringLiteral("组 %1").arg(m_groups.size() + 1);
    m_groups.append(Group{groupName, {}});
    return m_groups.size() - 1;
}

void PasteManager::renameGroup(int index, const QString &name)
{
    if (index < 0 || index >= m_groups.size())
        return;
    if (!name.trimmed().isEmpty())
        m_groups[index].name = name.trimmed();
}

void PasteManager::moveGroup(int from, int to)
{
    prune();
    if (from < 0 || from >= m_groups.size() || to < 0 || to >= m_groups.size() || from == to)
        return;
    Group group = m_groups.takeAt(from);
    m_groups.insert(to, std::move(group));
    if (m_activeGroup == from)
        m_activeGroup = to;
    else if (from < m_activeGroup && m_activeGroup <= to)
        --m_activeGroup;
    else if (to <= m_activeGroup && m_activeGroup < from)
        ++m_activeGroup;
}

void PasteManager::moveWindowInGroup(int groupIndex, int from, int to)
{
    prune();
    if (groupIndex < 0 || groupIndex >= m_groups.size())
        return;
    auto &windows = m_groups[groupIndex].windows;
    if (from < 0 || from >= windows.size() || to < 0 || to >= windows.size() || from == to)
        return;
    QPointer<PasteWindow> window = windows.takeAt(from);
    windows.insert(to, window);
}

void PasteManager::moveWindowToGroup(PasteWindow *window, int index)
{
    prune();
    if (!window || index < 0 || index >= m_groups.size())
        return;
    const int current = groupIndexOf(window);
    if (current < 0 || current == index)
        return;
    m_groups[current].windows.removeAll(window);
    m_groups[index].windows.append(window);
    const bool shouldShow = index == m_activeGroup && !m_soloMode;
    if (shouldShow)
        window->showExisting();
    else
        window->hide();
}

QList<PasteWindow *> PasteManager::windowsInGroup(int index) const
{
    QList<PasteWindow *> result;
    if (index < 0 || index >= m_groups.size())
        return result;
    for (const QPointer<PasteWindow> &window : m_groups.at(index).windows)
        if (window)
            result.append(window.data());
    return result;
}

QStringList PasteManager::groupNames() const
{
    QStringList result;
    for (const Group &group : m_groups)
        result.append(group.name);
    return result;
}

int PasteManager::groupIndexOf(PasteWindow *window) const
{
    if (!window)
        return -1;
    for (int index = 0; index < m_groups.size(); ++index)
        if (m_groups.at(index).windows.contains(window))
            return index;
    return -1;
}

int PasteManager::activeGroup() const
{
    return m_activeGroup;
}

void PasteManager::showGroup(int index)
{
    switchGroup(index);
}

void PasteManager::emptyCurrentGroup()
{
    prune();
    if (m_activeGroup < 0 || m_activeGroup >= m_groups.size())
        return;
    for (const QPointer<PasteWindow> &window : m_groups[m_activeGroup].windows)
        if (window)
            window->close();
    m_groups[m_activeGroup].windows.clear();
    clearSelection();
}

void PasteManager::toggleSolo()
{
    prune();
    if (m_selected.isEmpty())
        return;
    m_soloMode = !m_soloMode;
    for (Group &group : m_groups)
        for (const QPointer<PasteWindow> &window : group.windows)
            if (window)
                window->setSoloHidden(m_soloMode && !m_selected.contains(window));
}

bool PasteManager::soloMode() const
{
    return m_soloMode;
}

void PasteManager::clearSelection()
{
    m_selected.clear();
    if (m_soloMode)
    {
        m_soloMode = false;
        for (Group &group : m_groups)
            for (const QPointer<PasteWindow> &window : group.windows)
                if (window)
                    window->setSoloHidden(false);
    }
}

void PasteManager::selectAllVisible()
{
    prune();
    m_selected.clear();
    for (const Group &group : m_groups)
    {
        for (const QPointer<PasteWindow> &window : group.windows)
        {
            if (window && window->isVisible() && !window->isSoloHidden())
                m_selected.append(window);
        }
    }
}

int PasteManager::selectedCount() const
{
    int count = 0;
    for (const QPointer<PasteWindow> &window : m_selected)
        if (window)
            ++count;
    return count;
}

void PasteManager::destroySelected()
{
    for (const QPointer<PasteWindow> &window : m_selected)
        if (window)
            window->close();
    clearSelection();
    prune();
}

void PasteManager::moveSelectedToGroup(int index)
{
    prune();
    if (index < 0 || index >= m_groups.size() || m_selected.isEmpty())
        return;
    for (const QPointer<PasteWindow> &window : m_selected)
    {
        if (!window)
            continue;
        for (Group &group : m_groups)
        {
            const int at = group.windows.indexOf(window);
            if (at >= 0)
            {
                group.windows.removeAt(at);
                break;
            }
        }
        m_groups[index].windows.append(window);
    }
    clearSelection();
}

void PasteManager::toggleClickThroughSelected()
{
    for (const QPointer<PasteWindow> &window : m_selected)
        if (window)
            window->setClickThrough(!window->clickThrough());
}

void PasteManager::toggleClickThroughAtCursor(const QPoint &globalPosition)
{
    prune();
    // The last visible window in the manager's list is the most recently
    // activated candidate.  Prefer it when several pins overlap, then fall
    // back through the remaining groups/windows.
    PasteWindow *underCursor = nullptr;
    for (qsizetype groupIndex = m_groups.size() - 1; groupIndex >= 0 && !underCursor; --groupIndex)
    {
        const Group &group = m_groups.at(groupIndex);
        for (qsizetype index = group.windows.size() - 1; index >= 0; --index)
        {
            PasteWindow *window = group.windows.at(index);
            if (window && window->state() == PasteWindow::State::Active && window->isVisible() &&
                !window->isSoloHidden() && window->frameGeometry().contains(globalPosition))
            {
                underCursor = window;
                break;
            }
        }
    }
    if (underCursor)
    {
        underCursor->setClickThrough(!underCursor->clickThrough());
        return;
    }
    disableClickThroughAll();
}

void PasteManager::disableClickThroughAll()
{
    prune();
    for (Group &group : m_groups)
        for (const QPointer<PasteWindow> &window : group.windows)
            if (window && window->clickThrough())
                window->setClickThrough(false);
}

void PasteManager::moveSelectedBy(const QPoint &delta)
{
    if (delta.isNull())
        return;
    for (const QPointer<PasteWindow> &window : m_selected)
        if (window && window->state() == PasteWindow::State::Active)
            window->move(window->pos() + delta);
}

void PasteManager::destroyAll()
{
    prune();
    for (Group &group : m_groups)
    {
        for (const QPointer<PasteWindow> &window : group.windows)
        {
            if (window)
                window->close();
        }
    }
    m_groups.clear();
    m_groups.append(Group{QStringLiteral("默认组"), {}});
    m_closed.clear();
    m_activeGroup = 0;
    clearSelection();
}

int PasteManager::visibleCount() const
{
    int count = 0;
    for (const Group &group : m_groups)
    {
        for (const QPointer<PasteWindow> &window : group.windows)
        {
            if (window && window->isVisible())
                ++count;
        }
    }
    return count;
}

int PasteManager::groupCount() const
{
    return m_groups.size();
}

void PasteManager::prune()
{
    for (Group &group : m_groups)
    {
        for (qsizetype index = group.windows.size() - 1; index >= 0; --index)
        {
            if (!group.windows.at(index) || group.windows.at(index)->state() == PasteWindow::State::Destroyed)
                group.windows.removeAt(index);
        }
    }
    for (qsizetype index = m_selected.size() - 1; index >= 0; --index)
    {
        if (!m_selected.at(index) || m_selected.at(index)->state() != PasteWindow::State::Active)
            m_selected.removeAt(index);
    }
    for (qsizetype index = m_closed.size() - 1; index >= 0; --index)
    {
        if (!m_closed.at(index) || m_closed.at(index)->state() != PasteWindow::State::Closed)
            m_closed.removeAt(index);
    }
    if (m_groups.isEmpty())
        m_groups.append(Group{QStringLiteral("默认组"), {}});
    if (m_activeGroup >= m_groups.size())
        m_activeGroup = m_groups.size() - 1;
}

void PasteManager::setGroupVisible(int groupIndex, bool visible)
{
    if (groupIndex < 0 || groupIndex >= m_groups.size())
        return;
    for (const QPointer<PasteWindow> &window : m_groups[groupIndex].windows)
    {
        if (!window)
            continue;
        if (visible)
            window->showExisting();
        else
            window->hide();
    }
}

void PasteManager::onWindowActivated(PasteWindow *window, Qt::KeyboardModifiers modifiers)
{
    if (!window)
        return;
    if (modifiers.testFlag(snipaste::platform::primaryModifier()))
    {
        const int index = m_selected.indexOf(window);
        if (index >= 0)
            m_selected.removeAt(index);
        else
            m_selected.append(window);
    }
    else
    {
        m_selected.clear();
        m_selected.append(window);
    }
}

} // namespace snipaste::ui
