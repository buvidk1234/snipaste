#include "ui/snip/snip_overlay.h"
#include "ui/ai/ai_dialog.h"
#include "ui/annotation/annotation_options.h"

#include <QEvent>
#include <QFrame>

namespace snipaste::ui
{
namespace
{
class AiLayer final : public QWidget
{
  public:
    using QWidget::QWidget;

  protected:
    bool event(QEvent *event) override
    {
        switch (event->type())
        {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick:
        case QEvent::MouseMove:
        case QEvent::Wheel:
        case QEvent::ContextMenu:
            event->accept();
            return true;
        default:
            return QWidget::event(event);
        }
    }
};
} // namespace

void SnipOverlay::showAiDialog(AiDialog *dialog)
{
    if (m_aiLayer)
    {
        delete dialog;
        return;
    }
    m_aiLayer = new AiLayer(this);
    m_aiLayer->setObjectName(QStringLiteral("aiLayer"));
    m_aiLayer->setCursor(Qt::ArrowCursor);
    m_aiLayer->setGeometry(rect());
    m_aiDialog = dialog;
    dialog->setParent(m_aiLayer);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->placeNear(m_selection, rect());
    connect(dialog, &AiDialog::closed, this, [this] {
        auto *layer = m_aiLayer;
        m_aiLayer = nullptr;
        m_aiDialog = nullptr;
        layer->hide();
        layer->deleteLater();
        updateToolbarGeometry();
        setFocus(Qt::OtherFocusReason);
        refreshAnnotationCursor();
        update();
    });
    if (m_annotationOptions)
        m_annotationOptions->closePopups();
    m_aiLayer->show();
    m_aiLayer->raise();
    dialog->show();
    dialog->setFocus(Qt::OtherFocusReason);
    updateToolbarGeometry();
    update();
}
} // namespace snipaste::ui
