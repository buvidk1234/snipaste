#pragma once

#include <QDialog>
#include <QImage>

class QListWidget;
class QLabel;
class QPushButton;
template <typename T> class QFutureWatcher;

namespace snipaste::core {
class ScreenshotHistory;
}

namespace snipaste::ui {

class ScreenshotHistoryDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit ScreenshotHistoryDialog(core::ScreenshotHistory &history, QWidget *parent = nullptr);

    void refresh();

signals:
    void copyRequested(const QImage &image);
    void pasteRequested(const QImage &image);
    void clearRequested();

private:
    void updatePreview();
    void startPreviewLoad();
    QImage selectedImage() const;

    core::ScreenshotHistory &m_history;
    QListWidget *m_list = nullptr;
    QLabel *m_preview = nullptr;
    QPushButton *m_copyButton = nullptr;
    QPushButton *m_pasteButton = nullptr;
    QPushButton *m_saveButton = nullptr;
    QPushButton *m_clearButton = nullptr;
    quint64 m_refreshGeneration = 0;
    quint64 m_previewGeneration = 0;
    QFutureWatcher<QImage> *m_previewWatcher = nullptr;
    bool m_previewPending = false;
};

} // namespace snipaste::ui
