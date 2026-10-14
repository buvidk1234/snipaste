#pragma once

#include <QDialog>
#include <QImage>

namespace snipaste::ui {

class AnnotationCanvas;

class AnnotationDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit AnnotationDialog(const QImage &image, QWidget *parent = nullptr);

    void setAnnotationOptions(bool rememberColors, bool freeSelect, bool disableEraserEditing);
    QImage resultImage() const;

private:
    AnnotationCanvas *m_canvas = nullptr;
};

} // namespace snipaste::ui
