#pragma once

#include <QDialog>
#include <QImage>

class QListWidget;
class QLabel;
class QPushButton;

namespace snipate::core {
class ScreenshotHistory;
}

namespace snipate::ui {

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
    QImage selectedImage() const;

    core::ScreenshotHistory &m_history;
    QListWidget *m_list = nullptr;
    QLabel *m_preview = nullptr;
    QPushButton *m_copyButton = nullptr;
    QPushButton *m_pasteButton = nullptr;
    QPushButton *m_saveButton = nullptr;
    QPushButton *m_clearButton = nullptr;
};

} // namespace snipate::ui
