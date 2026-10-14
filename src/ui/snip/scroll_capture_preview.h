#pragma once

#include <QImage>
#include <QWidget>

class QLabel;
class QScrollArea;

namespace snipaste::ui {

class ScrollCapturePreview final : public QWidget
{
    Q_OBJECT

public:
    explicit ScrollCapturePreview(QWidget *parent = nullptr);

    void setImage(const QImage &image);
    void clear();
    QImage image() const { return m_stitchedImage; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QScrollArea *m_scrollArea = nullptr;
    QLabel *m_imageLabel = nullptr;
    QImage m_stitchedImage;
};

} // namespace snipaste::ui
