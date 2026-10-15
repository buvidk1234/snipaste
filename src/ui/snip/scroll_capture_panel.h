#pragma once

#include <QWidget>

class QLabel;
class QToolButton;

namespace snipaste::ui {

class ScrollCapturePanel final : public QWidget
{
    Q_OBJECT

public:
    explicit ScrollCapturePanel(QWidget *parent = nullptr);

    void setStatus(const QString &status);
    void setAutoRunning(bool running);
    void setBusy(bool busy);

signals:
    void statusChanged(const QString &status);
    void editRequested();
    void saveRequested();
    void finishRequested();
    void cancelRequested();
    void captureRequested();
    void autoScrollToggled(bool enabled);

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    QLabel *m_title = nullptr;
    QToolButton *m_edit = nullptr;
    QToolButton *m_save = nullptr;
    QToolButton *m_cancel = nullptr;
    QToolButton *m_finish = nullptr;
    QToolButton *m_capture = nullptr;
    QToolButton *m_auto = nullptr;
};

} // namespace snipaste::ui
