#pragma once

#include <QImage>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QSize>
#include <QString>
#include <QTimer>
#include <functional>

namespace snipaste::ui
{

class PasteWindow;

class PasteManager final : public QObject
{
    Q_OBJECT

  public:
    explicit PasteManager(QObject *parent = nullptr);
    ~PasteManager() override;
    void prepareEditorCache();

    PasteWindow *createPaste(const QImage &image);
    PasteWindow *pasteClipboard(const QImage &image);
    void rememberCapture(const QImage &image, const QRect &globalRegion);
    void setUseCapturePosition(bool enabled);
    void setDefaultOpacity(qreal opacity);
    void setShadowEnabled(bool enabled);
    void setResizeEnabled(bool enabled);
    void setExchangeNavigationKeys(bool enabled);
    void setMaxNetworkImageBytes(qint64 bytes);
    void setMaxSize(int pixels);
    void setThumbnailSize(const QSize &size);
    void setWheelCenter(bool enabled);
    void setResetTransform(bool enabled);
    void setDisableDpiScaling(bool enabled);
    void setHelpKeys(bool enabled);
    void setCloseConfirm(bool enabled);
    void setDestroyConfirm(bool enabled);
    void setMagneticAttachThreshold(int pixels);
    void setCopyAsFileMode(int mode);
    void setBackgroundMode(const QString &mode);
    void setSmoothScale(bool enabled);
    void setEscapeActions(const QString &escapeAction, const QString &shiftEscapeAction);
    void setMouseActions(const QStringList &actions);
    void setVirtualDesktopPinned(bool enabled);
    void setHiddenForFileDialog(bool hidden);
    void setFlashNew(bool enabled);
    void setActivateNew(bool enabled);
    void setFlashTransparency(bool enabled);
    void setFlashGroupSwitch(bool enabled);
    void setHideAllOnPasteKey(bool enabled);
    void setKeepOnDrag(bool enabled);
    PasteWindow *createPasteAtPosition(const QImage &image, const QPoint &globalPosition);
    void setRestoreCount(int count);
    void toggleAll();
    void showAll();
    void hideAll();
    void switchGroup();
    void switchGroup(int index);
    int createGroup(const QString &name = {});
    void renameGroup(int index, const QString &name);
    void moveGroup(int from, int to);
    void moveWindowInGroup(int groupIndex, int from, int to);
    void moveWindowToGroup(PasteWindow *window, int index);
    QList<PasteWindow *> windowsInGroup(int index) const;
    QStringList groupNames() const;
    int groupIndexOf(PasteWindow *window) const;
    int activeGroup() const;
    void showGroup(int index);
    void emptyCurrentGroup();
    void toggleSolo();
    bool soloMode() const;
    void clearSelection();
    void selectAllVisible();
    int selectedCount() const;
    void destroySelected();
    void moveSelectedToGroup(int index);
    void toggleClickThroughSelected();
    // Toggle the pin directly under the mouse.  If no pin is under the
    // cursor, clear click-through on every pin as in Snipaste's global action.
    void toggleClickThroughAtCursor(const QPoint &globalPosition);
    void disableClickThroughAll();
    void moveSelectedBy(const QPoint &delta);
    void destroyAll();
    void loadState(const QString &directory);
    void saveState(const QString &directory) const;
    int visibleCount() const;
    int groupCount() const;

  private:
    struct Group
    {
        QString name;
        QList<QPointer<PasteWindow>> windows;
    };

    void applyWindowSettings(PasteWindow *window);
    PasteWindow *createWindow(const QImage &image);
    void forEachWindow(const std::function<void(PasteWindow *)> &action);
    void prune();
    void setGroupVisible(int groupIndex, bool visible);
    void onWindowActivated(PasteWindow *window, Qt::KeyboardModifiers modifiers);
    void connectWindow(PasteWindow *window);
    void snapWindowToNearby(PasteWindow *window);
    void onWindowClosed(PasteWindow *window);
    void trimClosed();
    PasteWindow *restoreClosedAt(qsizetype index);

    QList<Group> m_groups;
    QPointer<PasteWindow> m_preparedWindow;
    QTimer m_prepareEditorTimer;
    int m_activeGroup = 0;
    QList<QPointer<PasteWindow>> m_selected;
    QList<QPointer<PasteWindow>> m_closed;
    // Keep only a signature of the last clipboard image.  The full image is
    // already owned by the active/closed pin and need not stay resident in
    // the manager just to detect a repeated paste.
    quint64 m_lastClipboardFingerprint = 0;
    QSize m_lastClipboardSize;
    QRect m_captureRegion;
    quint64 m_captureFingerprint = 0;
    qint64 m_captureImageKey = 0;
    QSize m_captureSize;
    bool m_captureAwaitingPaste = false;
    bool m_useCapturePosition = true;
    qreal m_defaultOpacity = 1.0;
    bool m_shadowEnabled = true;
    bool m_resizeEnabled = true;
    bool m_exchangeNavigationKeys = false;
    qint64 m_maxNetworkImageBytes = 20LL * 1024 * 1024;
    int m_maxSize = 12000;
    QSize m_thumbnailSize = QSize(62, 62);
    bool m_wheelCenter = false;
    bool m_resetTransform = true;
    bool m_disableDpiScaling = false;
    bool m_helpKeys = false;
    bool m_closeConfirm = true;
    bool m_destroyConfirm = true;
    int m_magneticAttachThreshold = 0;
    int m_copyAsFileMode = 2;
    QString m_backgroundMode = QStringLiteral("伪透明");
    bool m_smoothScale = true;
    QString m_escapeAction = QStringLiteral("关闭贴图");
    QString m_shiftEscapeAction = QStringLiteral("销毁贴图");
    QStringList m_mouseActions;
    int m_restoreCount = 1;
    bool m_soloMode = false;
    bool m_virtualDesktopPinned = false;
    bool m_hiddenForFileDialog = false;
    bool m_flashNew = true;
    bool m_activateNew = true;
    bool m_flashTransparency = true;
    bool m_flashGroupSwitch = true;
    bool m_hideAllOnPasteKey = false;
    bool m_keepOnDrag = false;
    QList<QPointer<PasteWindow>> m_fileDialogVisible;
};

} // namespace snipaste::ui
