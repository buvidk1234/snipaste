#pragma once

#include <QImage>
#include <QWidget>

class QScrollArea;
class QLabel;

namespace snipaste::core { class VerticalScrollStitcher; }

namespace snipaste::ui {

class ScrollCaptureImage;

class ScrollCapturePreview final : public QWidget
{
    Q_OBJECT

public:
    explicit ScrollCapturePreview(QWidget *parent = nullptr);

    void setCapture(const core::VerticalScrollStitcher &capture);
    void setStatus(const QString &status);
    void clear();
    QSize imageSize() const;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QScrollArea *m_scrollArea = nullptr;
    ScrollCaptureImage *m_imageWidget = nullptr;
    QLabel *m_status = nullptr;
};

} // namespace snipaste::ui
