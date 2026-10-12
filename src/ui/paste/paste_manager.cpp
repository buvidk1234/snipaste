#include "ui/paste/paste_manager.h"

#include "ui/paste/paste_window.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>

namespace snipate::ui {

PasteManager::PasteManager(QObject *parent)
    : QObject(parent)
{
    m_groups.append(Group{QStringLiteral("默认组"), {}});
}

PasteManager::~PasteManager()
{
    // Top-level widgets have no QWidget parent; the manager owns their lifetime.
    for (const Group &group : m_groups) {
        for (const QPointer<PasteWindow> &window : group.windows) {
            if (window) {
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
    window->setPasteManager(this);
    window->setResizeEnabled(m_resizeEnabled);
    window->setExchangeNavigationKeys(m_exchangeNavigationKeys);
    window->setMaxNetworkImageBytes(m_maxNetworkImageBytes);
    window->setShadowEnabled(m_shadowEnabled);
    window->setBackgroundMode(m_backgroundMode);
    window->setSmoothScale(m_smoothScale);
    window->setEscapeActions(m_escapeAction, m_shiftEscapeAction);
    window->setMouseActions(m_mouseActions);
    window->setVirtualDesktopPinned(m_virtualDesktopPinned);
    window->setWindowOpacity(m_defaultOpacity);
    m_groups[m_activeGroup].windows.append(window);
    connectWindow(window);
    QImage pixels = image;
    pixels.setDevicePixelRatio(1);
    const bool fromCapture = m_captureRegion.isValid() && pixels == m_captureImage;
    if (fromCapture)
        m_captureAwaitingPaste = false;
    if (fromCapture && m_useCapturePosition)
        window->showAtPosition(m_captureRegion.topLeft());
    else
        window->showAtCursor();
    return window;
}

PasteWindow *PasteManager::createPasteAtPosition(const QImage &image, const QPoint &globalPosition)
{
    PasteWindow *window = createPaste(image);
    if (window)
        window->showAtPosition(globalPosition);
    return window;
}

void PasteManager::rememberCapture(const QImage &image, const QRect &globalRegion)
{
    m_captureImage = image;
    m_captureImage.setDevicePixelRatio(1);
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
    prune();
    for (Group &group : m_groups)
        for (const QPointer<PasteWindow> &window : group.windows)
            if (window)
                window->setShadowEnabled(enabled);
}

void PasteManager::setResizeEnabled(bool enabled)
{
    m_resizeEnabled = enabled;
    prune();
    for (Group &group : m_groups)
        for (const QPointer<PasteWindow> &window : group.windows)
            if (window)
                window->setResizeEnabled(enabled);
}

void PasteManager::setExchangeNavigationKeys(bool enabled)
{
    m_exchangeNavigationKeys = enabled;
    prune();
    for (Group &group : m_groups)
        for (const QPointer<PasteWindow> &window : group.windows)
            if (window)
                window->setExchangeNavigationKeys(enabled);
}

void PasteManager::setMaxNetworkImageBytes(qint64 bytes)
{
    m_maxNetworkImageBytes = qBound<qint64>(0, bytes, 1024LL * 1024 * 1024);
    prune();
    for (Group &group : m_groups)
        for (const QPointer<PasteWindow> &window : group.windows)
            if (window)
                window->setMaxNetworkImageBytes(m_maxNetworkImageBytes);
}

void PasteManager::setMagneticAttachThreshold(int pixels)
{
    m_magneticAttachThreshold = qBound(0, pixels, 500);
}

void PasteManager::setCopyAsFile(bool enabled)
{
    m_copyAsFile = enabled;
}

void PasteManager::setBackgroundMode(const QString &mode)
{
    m_backgroundMode = mode.trimmed().isEmpty() ? QStringLiteral("伪透明") : mode.trimmed();
    prune();
    for (Group &group : m_groups)
        for (const QPointer<PasteWindow> &window : group.windows)
            if (window)
                window->setBackgroundMode(m_backgroundMode);
}

void PasteManager::setSmoothScale(bool enabled)
{
    m_smoothScale = enabled;
    prune();
    for (Group &group : m_groups)
        for (const QPointer<PasteWindow> &window : group.windows)
            if (window)
                window->setSmoothScale(enabled);
}

void PasteManager::setEscapeActions(const QString &escapeAction, const QString &shiftEscapeAction)
{
    m_escapeAction = escapeAction;
    m_shiftEscapeAction = shiftEscapeAction;
    prune();
    for (Group &group : m_groups)
        for (const QPointer<PasteWindow> &window : group.windows)
            if (window)
                window->setEscapeActions(m_escapeAction, m_shiftEscapeAction);
}

void PasteManager::setMouseActions(const QStringList &actions)
{
    m_mouseActions = actions;
    prune();
    for (Group &group : m_groups)
        for (const QPointer<PasteWindow> &window : group.windows)
            if (window)
                window->setMouseActions(m_mouseActions);
}

void PasteManager::setVirtualDesktopPinned(bool enabled)
{
    m_virtualDesktopPinned = enabled;
    prune();
    for (Group &group : m_groups)
        for (const QPointer<PasteWindow> &window : group.windows)
            if (window)
                window->setVirtualDesktopPinned(enabled);
}

void PasteManager::setHiddenForFileDialog(bool hidden)
{
    if (hidden == m_hiddenForFileDialog)
        return;
    m_hiddenForFileDialog = hidden;
    if (hidden) {
        m_fileDialogVisible.clear();
        prune();
        for (const Group &group : m_groups) {
            for (const QPointer<PasteWindow> &window : group.windows) {
                if (window && window->isVisible() && !window->isSoloHidden()) {
                    m_fileDialogVisible.append(window);
                    window->hide();
                }
            }
        }
    } else {
        for (const QPointer<PasteWindow> &window : m_fileDialogVisible)
            if (window && window->state() == PasteWindow::State::Active)
                window->showExisting();
        m_fileDialogVisible.clear();
    }
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
        const QImage image = source->displayedImage();
        if (!m_copyAsFile) {
            QApplication::clipboard()->setImage(image);
            return;
        }
        const QString directory = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        const QString path = QDir(directory).filePath(
            QStringLiteral("snipate-%1.png").arg(QUuid::createUuid().toString(QUuid::Id128)));
        if (!image.save(path, "PNG"))
            return;
        auto *mime = new QMimeData;
        mime->setImageData(image);
        mime->setUrls({QUrl::fromLocalFile(path)});
        QApplication::clipboard()->setMimeData(mime);
    });
    QObject::connect(window, &PasteWindow::moveRequested, this,
                     [this](PasteWindow *source, const QPoint &delta) {
                         if (!source)
                             return;
                         if (m_selected.contains(source) && m_selected.size() > 1)
                             moveSelectedBy(delta);
                         else {
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
                         for (const QPointer<PasteWindow> &selected : m_selected) {
                             if (!selected || selected == source
                                 || selected->state() != PasteWindow::State::Active)
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
    const auto snap = [threshold](int first, int second) {
        return qAbs(first - second) <= threshold;
    };
    for (const Group &group : m_groups) {
        for (const QPointer<PasteWindow> &other : group.windows) {
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
    QImage pixels = image;
    pixels.setDevicePixelRatio(1);
    // A new capture is a new paste even if its pixels happen to equal an older
    // closed pin (for example, two identical icons at different positions).
    if (m_captureAwaitingPaste && pixels == m_captureImage) {
        m_lastClipboardImage = pixels;
        return createPaste(image);
    }
    if (!pixels.isNull() && pixels != m_lastClipboardImage) {
        m_lastClipboardImage = pixels;
        // A freshly captured/copied image takes precedence over older closed
        // pins. If it is itself a closed pin, recover that same window.
        for (qsizetype index = m_closed.size() - 1; index >= 0; --index) {
            QImage source = m_closed.at(index)->sourceImage();
            source.setDevicePixelRatio(1);
            if (source == pixels)
                return restoreClosedAt(index);
        }
        return createPaste(image);
    }
    if (!m_closed.isEmpty())
        return restoreClosedAt(m_closed.size() - 1);
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
    while (m_closed.size() > m_restoreCount) {
        const QPointer<PasteWindow> window = m_closed.takeFirst();
        if (window)
            window->close();
    }
}

void PasteManager::toggleAll()
{
    prune();
    const bool anyVisible = visibleCount() > 0;
    for (Group &group : m_groups) {
        for (const QPointer<PasteWindow> &window : group.windows) {
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
    prune();
    for (Group &group : m_groups)
        for (const QPointer<PasteWindow> &window : group.windows)
            if (window)
                window->showExisting();
}

void PasteManager::hideAll()
{
    prune();
    for (Group &group : m_groups)
        for (const QPointer<PasteWindow> &window : group.windows)
            if (window)
                window->hide();
}

void PasteManager::switchGroup()
{
    prune();
    if (m_groups.size() < 2)
        m_groups.append(Group{QStringLiteral("组 2"), {}});

    setGroupVisible(m_activeGroup, false);
    m_activeGroup = (m_activeGroup + 1) % m_groups.size();
    setGroupVisible(m_activeGroup, true);
}

void PasteManager::switchGroup(int index)
{
    prune();
    if (index < 0 || index >= m_groups.size() || index == m_activeGroup)
        return;
    setGroupVisible(m_activeGroup, false);
    m_activeGroup = index;
    setGroupVisible(m_activeGroup, true);
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
    if (from < 0 || from >= m_groups.size() || to < 0 || to >= m_groups.size()
        || from == to)
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
    if (m_soloMode) {
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
    for (const Group &group : m_groups) {
        for (const QPointer<PasteWindow> &window : group.windows) {
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
    for (const QPointer<PasteWindow> &window : m_selected) {
        if (!window)
            continue;
        for (Group &group : m_groups) {
            const int at = group.windows.indexOf(window);
            if (at >= 0) {
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
    for (qsizetype groupIndex = m_groups.size() - 1; groupIndex >= 0 && !underCursor; --groupIndex) {
        const Group &group = m_groups.at(groupIndex);
        for (qsizetype index = group.windows.size() - 1; index >= 0; --index) {
            PasteWindow *window = group.windows.at(index);
            if (window && window->state() == PasteWindow::State::Active
                && window->isVisible() && !window->isSoloHidden()
                && window->frameGeometry().contains(globalPosition)) {
                underCursor = window;
                break;
            }
        }
    }
    if (underCursor) {
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
    for (Group &group : m_groups) {
        for (const QPointer<PasteWindow> &window : group.windows) {
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

void PasteManager::loadState(const QString &directory)
{
    QFile stateFile(QDir(directory).filePath(QStringLiteral("state.json")));
    if (!stateFile.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument document = QJsonDocument::fromJson(stateFile.readAll());
    if (!document.isObject())
        return;

    const QJsonObject root = document.object();
    const QJsonArray groups = root.value(QStringLiteral("groups")).toArray();
    if (groups.isEmpty())
        return;
    destroyAll();
    m_groups.clear();
    for (const QJsonValue &groupValue : groups) {
        Group group;
        const QJsonObject groupObject = groupValue.toObject();
        const bool namedFormat = groupValue.isObject();
        const QJsonArray windowArray = namedFormat
                                           ? groupObject.value(QStringLiteral("windows")).toArray()
                                           : groupValue.toArray();
        group.name = namedFormat
                         ? groupObject.value(QStringLiteral("name")).toString()
                         : QStringLiteral("组 %1").arg(m_groups.size() + 1);
        if (group.name.isEmpty())
            group.name = QStringLiteral("组 %1").arg(m_groups.size() + 1);
        for (const QJsonValue &windowValue : windowArray) {
            const QJsonObject windowObject = windowValue.toObject();
            const QString imagePath = QDir(directory).filePath(
                windowObject.value(QStringLiteral("image")).toString());
            QImage image;
            if (!image.load(imagePath))
                continue;
            auto *window = new PasteWindow(image);
            window->setPasteManager(this);
            window->setResizeEnabled(m_resizeEnabled);
            window->setExchangeNavigationKeys(m_exchangeNavigationKeys);
            window->setWindowOpacity(m_defaultOpacity);
            window->setShadowEnabled(m_shadowEnabled);
            window->setBackgroundMode(m_backgroundMode);
            window->setSmoothScale(m_smoothScale);
            window->setEscapeActions(m_escapeAction, m_shiftEscapeAction);
            window->setMouseActions(m_mouseActions);
            window->setVirtualDesktopPinned(m_virtualDesktopPinned);
            window->move(windowObject.value(QStringLiteral("x")).toInt(),
                         windowObject.value(QStringLiteral("y")).toInt());
            window->setWindowOpacity(windowObject.value(QStringLiteral("opacity")).toDouble(1.0));
            window->restoreTransform(windowObject.value(QStringLiteral("rotation")).toInt(),
                                     windowObject.value(QStringLiteral("flipHorizontal")).toBool(),
                                     windowObject.value(QStringLiteral("flipVertical")).toBool(),
                                     windowObject.value(QStringLiteral("grayscale")).toBool(),
                                     windowObject.value(QStringLiteral("inverted")).toBool(),
                                     windowObject.value(QStringLiteral("scale")).toDouble(1.0),
                                     windowObject.value(QStringLiteral("transparentBackground")).toBool(),
                                     windowObject.value(QStringLiteral("alwaysOnTop")).toBool(true));
            window->setThumbnailMode(windowObject.value(QStringLiteral("thumbnailMode")).toBool(false));
            if (windowObject.contains(QStringLiteral("backgroundMode")))
                window->setBackgroundMode(windowObject.value(QStringLiteral("backgroundMode")).toString());
            if (windowObject.contains(QStringLiteral("virtualDesktop")))
                window->setVirtualDesktopPinned(windowObject.value(QStringLiteral("virtualDesktop")).toBool());
            window->setClickThrough(windowObject.value(QStringLiteral("clickThrough")).toBool());
            window->setShadowEnabled(windowObject.contains(QStringLiteral("shadow"))
                                         ? windowObject.value(QStringLiteral("shadow")).toBool()
                                         : m_shadowEnabled);
            group.windows.append(window);
            connectWindow(window);
            if (windowObject.value(QStringLiteral("visible")).toBool(true))
                window->showExisting();
            else
                window->hide();
        }
        m_groups.append(std::move(group));
    }
    if (m_groups.isEmpty())
        m_groups.append(Group{QStringLiteral("默认组"), {}});
    m_activeGroup = qBound(0, root.value(QStringLiteral("activeGroup")).toInt(), m_groups.size() - 1);
}

void PasteManager::saveState(const QString &directory) const
{
    QDir dir(directory);
    if (!dir.mkpath(QStringLiteral(".")))
        return;

    QJsonArray groups;
    for (int groupIndex = 0; groupIndex < m_groups.size(); ++groupIndex) {
        QJsonArray windows;
        int windowIndex = 0;
        for (const QPointer<PasteWindow> &window : m_groups.at(groupIndex).windows) {
            if (!window || window->state() != PasteWindow::State::Active
                || window->sourceImage().isNull())
                continue;
            const QString fileName = QStringLiteral("paste-%1-%2.png")
                                         .arg(groupIndex, 2, 10, QChar('0'))
                                         .arg(windowIndex++, 3, 10, QChar('0'));
            if (!window->sourceImage().save(dir.filePath(fileName), "PNG"))
                continue;
            QJsonObject object;
            object.insert(QStringLiteral("image"), fileName);
            object.insert(QStringLiteral("visible"), window->isVisible());
            object.insert(QStringLiteral("x"), window->x());
            object.insert(QStringLiteral("y"), window->y());
            object.insert(QStringLiteral("opacity"), window->windowOpacity());
            object.insert(QStringLiteral("rotation"), window->rotationQuarterTurns());
            object.insert(QStringLiteral("flipHorizontal"), window->flipHorizontal());
            object.insert(QStringLiteral("flipVertical"), window->flipVertical());
            object.insert(QStringLiteral("grayscale"), window->grayscale());
            object.insert(QStringLiteral("inverted"), window->inverted());
            object.insert(QStringLiteral("scale"), window->scale());
            object.insert(QStringLiteral("transparentBackground"), window->transparentBackground());
            object.insert(QStringLiteral("backgroundMode"), window->backgroundMode());
            object.insert(QStringLiteral("alwaysOnTop"), window->alwaysOnTop());
            object.insert(QStringLiteral("clickThrough"), window->clickThrough());
            object.insert(QStringLiteral("shadow"), window->shadowEnabled());
            object.insert(QStringLiteral("virtualDesktop"), window->virtualDesktopPinned());
            object.insert(QStringLiteral("thumbnailMode"), window->thumbnailMode());
            windows.append(object);
        }
        QJsonObject groupObject;
        groupObject.insert(QStringLiteral("name"), m_groups.at(groupIndex).name);
        groupObject.insert(QStringLiteral("windows"), windows);
        groups.append(groupObject);
    }

    QJsonObject root;
    root.insert(QStringLiteral("activeGroup"), m_activeGroup);
    root.insert(QStringLiteral("groups"), groups);
    QFile stateFile(dir.filePath(QStringLiteral("state.json")));
    if (stateFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
        stateFile.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

int PasteManager::visibleCount() const
{
    int count = 0;
    for (const Group &group : m_groups) {
        for (const QPointer<PasteWindow> &window : group.windows) {
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
    for (Group &group : m_groups) {
        for (qsizetype index = group.windows.size() - 1; index >= 0; --index) {
            if (!group.windows.at(index)
                || group.windows.at(index)->state() == PasteWindow::State::Destroyed)
                group.windows.removeAt(index);
        }
    }
    for (qsizetype index = m_selected.size() - 1; index >= 0; --index) {
        if (!m_selected.at(index)
            || m_selected.at(index)->state() != PasteWindow::State::Active)
            m_selected.removeAt(index);
    }
    for (qsizetype index = m_closed.size() - 1; index >= 0; --index) {
        if (!m_closed.at(index)
            || m_closed.at(index)->state() != PasteWindow::State::Closed)
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
    for (const QPointer<PasteWindow> &window : m_groups[groupIndex].windows) {
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
    if (modifiers.testFlag(Qt::ControlModifier)) {
        const int index = m_selected.indexOf(window);
        if (index >= 0)
            m_selected.removeAt(index);
        else
            m_selected.append(window);
    } else {
        m_selected.clear();
        m_selected.append(window);
    }
}

} // namespace snipate::ui
