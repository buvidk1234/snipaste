#pragma once

#include "core/recognition/ocr_service.h"
#include <QRegion>
#include <QWidget>

class QFrame;
class QLabel;
class QToolButton;

namespace snipaste::ui {

// Selection is a view over the recognized source pixels. It never inserts
// text or masks into the annotation document or exported screenshot.
class OcrOverlay final : public QWidget
{
    Q_OBJECT
public:
    OcrOverlay(QImage image, QRectF imageFrame, QWidget *parent = nullptr);
    void startRecognition(const QString &engine = {}, const QString &language = {});
    void setResult(core::OcrResult result);
    QString selectedText() const;
    const QImage &sourceImage() const { return m_image; }
    void selectAll();
    void copySelection();
    void copyAll();

signals:
    void exitRequested();
    void recognitionCompleted();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    bool focusNextPrevChild(bool) override { return false; }

private:
    struct Word {
        core::OcrTextBox box;
        QVector<qreal> positions;
        QVector<int> boundaries;
    };
    void applyResult(core::OcrResult result);
    void updateSelection(qsizetype anchor, qsizetype cursor);
    void layoutToolbar();
    int wordAt(QPointF point, bool nearest) const;
    qsizetype cursorAt(QPointF point, bool nearest) const;
    QRectF wordRect(const Word &word) const;
    QRegion selectionRegion(qsizetype anchor, qsizetype cursor) const;
    QImage m_image;
    QRectF m_imageFrame;
    core::OcrResult m_result;
    QVector<Word> m_words;
    QRegion m_mask;
    QFrame *m_toolbar = nullptr;
    QLabel *m_status = nullptr;
    QToolButton *m_copyAll = nullptr;
    QToolButton *m_copySelection = nullptr;
    qsizetype m_anchor = -1;
    qsizetype m_cursor = -1;
    bool m_selecting = false;
    quint64 m_generation = 0;
};

} // namespace snipaste::ui
