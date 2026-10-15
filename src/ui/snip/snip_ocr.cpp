#include "ui/snip/snip_overlay.h"
#include "ui/annotation/annotation_canvas.h"
#include "ui/annotation/annotation_options.h"
#include "ui/recognition/ocr_overlay.h"
#include <QFrame>

namespace snipaste::ui {

void SnipOverlay::setOcrOptions(const QString &engine, const QString &language)
{
    m_ocrEngine = engine;
    m_ocrLanguage = language;
}

void SnipOverlay::ocrSelection()
{
    if (m_ocrOverlay)
        return;
    if (m_canvas)
        m_canvas->finishTextInput();
    // Output borders/shadows are added only when exporting, and must not
    // resize the visible source or alter OCR coordinates.
    const QImage image = m_canvas ? m_canvas->renderedImage() : m_background.copy(imageSelection());
    if (image.isNull())
        return;
    m_ocrOverlay = new OcrOverlay(image, m_selection, this);
    m_ocrOverlay->setGeometry(rect());
    connect(m_ocrOverlay, &OcrOverlay::exitRequested, this, &SnipOverlay::leaveOcrMode);
    connect(m_ocrOverlay, &OcrOverlay::recognitionCompleted, this, &SnipOverlay::ocrCompleted);
    if (m_annotationOptions)
        m_annotationOptions->closePopups();
    m_toolbar->hide();
    m_sizeToolbar->hide();
    if (m_selectionBorder)
        m_selectionBorder->hide();
    m_ocrOverlay->show();
    m_ocrOverlay->raise();
    m_ocrOverlay->setFocus(Qt::OtherFocusReason);
    m_ocrOverlay->startRecognition(m_ocrEngine, m_ocrLanguage);
    emit ocrRequested(image);
}

void SnipOverlay::leaveOcrMode()
{
    if (!m_ocrOverlay)
        return;
    m_ocrOverlay->disconnect(this);
    m_ocrOverlay->hide();
    m_ocrOverlay->deleteLater();
    m_ocrOverlay = nullptr;
    if (m_selectionBorder)
        m_selectionBorder->show();
    updateToolbarGeometry();
    setFocus(Qt::OtherFocusReason);
    refreshAnnotationCursor();
    update();
}

void SnipOverlay::clearOcrOverlay()
{
    delete m_ocrOverlay;
    m_ocrOverlay = nullptr;
    if (m_selectionBorder)
        m_selectionBorder->show();
}

} // namespace snipaste::ui
