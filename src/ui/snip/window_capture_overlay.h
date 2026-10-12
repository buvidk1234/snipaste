#pragma once

#include <QImage>
#include <QRect>
#include <QWidget>

class QScreen;

namespace snipate::ui {

class WindowCaptureOverlay final : public QWidget
{
    Q_OBJECT

public:
    explicit WindowCaptureOverlay(QScreen *screen,
                                  bool elementMode = false,
                                  QWidget *parent = nullptr);

signals:
    void screenshotCaptured(const QImage &image);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void updateTarget();
    void captureTarget();

    QScreen *m_screen = nullptr;
    QImage m_background;
    QRect m_screenGeometry;
    QRect m_targetGeometry;
    quintptr m_targetWindow = 0;
    bool m_elementMode = false;
    bool m_cursorOverridden = false;
};

} // namespace snipate::ui
