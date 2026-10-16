#include "ui/annotation/annotation_options.h"
#include "ui/annotation/annotation_canvas.h"
#include "ui/annotation/annotation_toolbar_icons.h"
#include "ui/snip/capture_icons.h"
#include <QComboBox>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedLayout>
#include <QToolButton>

namespace snipaste::ui
{
void AnnotationOptions::syncStyleControls()
{
    if (!m_canvas)
        return;
    using Tool = AnnotationCanvas::Tool;
    const auto style = m_canvas->annotationStyle();
    int page = 0;
    switch (style.tool)
    {
    case Tool::Rectangle: case Tool::RoundedRectangle: case Tool::Ellipse: page = 0; break;
    case Tool::Line: case Tool::Arrow: page = 1; break;
    case Tool::Pen: page = 2; break;
    case Tool::Marker: page = 3; break;
    case Tool::Mosaic: case Tool::Blur: page = 4; break;
    case Tool::Text: page = 5; break;
    case Tool::Number: page = 6; break;
    case Tool::Magnifier: page = 7; break;
    case Tool::Eraser: page = 8; break;
    }
    showPage(page);
    updateDynamicWidthButtons(style.width);
    updateAllColorWatchers(style.color);
    const auto check = [this](const QString &name, bool checked) {
        if (auto *button = findChild<QToolButton *>(name))
        {
            const QSignalBlocker blocker(button);
            button->setChecked(checked);
        }
    };
    check(QStringLiteral("annotationShapeFilled"), style.filled);
    check(QStringLiteral("annotationRectangle"), style.tool != Tool::Ellipse);
    check(QStringLiteral("annotationEllipse"), style.tool == Tool::Ellipse);
    for (auto *button : findChildren<QToolButton *>(QStringLiteral("annotationLineStyle")))
    {
        button->setIcon(annotation::makeLineStyleDropdownIcon(style.penStyle, 68, 20));
        for (auto *action : button->menu()->actions())
        {
            const QSignalBlocker blocker(action);
            action->setChecked(action->data().toInt() == int(style.penStyle));
        }
    }
    const auto syncArrow = [](QToolButton *button, core::ArrowHead head, const QIcon &icon) {
        if (!button)
            return;
        button->setIcon(icon);
        if (auto *m = button->menu())
        {
            if (auto *listWidget = m->findChild<QListWidget *>())
            {
                const QSignalBlocker blocker(listWidget);
                for (int i = 0; i < listWidget->count(); ++i)
                {
                    auto *it = listWidget->item(i);
                    it->setSelected(it->data(Qt::UserRole).toInt() == int(head));
                }
            }
            for (auto *action : m->actions())
            {
                const QSignalBlocker blocker(action);
                action->setChecked(action->data().toInt() == int(head));
            }
        }
    };
    const auto arrow = style.tool == Tool::Line ? core::ArrowStyle::None : style.arrowStyle;
    const auto startHead = style.tool == Tool::Line ? core::ArrowHead::None : style.startArrowHead;
    const auto endHead = style.tool == Tool::Line ? core::ArrowHead::None : style.endArrowHead;
    syncArrow(m_startArrowButton, startHead,
              annotation::makeStartArrowDropdownIcon(startHead, 50, 22));
    syncArrow(m_endArrowButton, endHead,
              annotation::makeEndArrowDropdownIcon(endHead, 50, 22));
    check(QStringLiteral("annotationToggleArrow"), arrow != core::ArrowStyle::None);
    if (m_toggleArrowButton)
        m_toggleArrowButton->setIcon(annotation::makeToggleArrowIcon(arrow != core::ArrowStyle::None));
    if (arrow != core::ArrowStyle::None)
        m_lastArrowStyle = arrow;
    for (auto *button : findChildren<QToolButton *>())
    {
        const QSignalBlocker blocker(button);
        if (button->property("effectShapeOption").toBool())
            button->setChecked(button->property("effectShape").toInt() == int(style.effectShape));
        if (button->property("effectToggle").toBool())
        {
            button->setChecked(style.tool == Tool::Blur);
            button->setIcon(captureIcon(style.tool == Tool::Blur ? CaptureIcon::Blur : CaptureIcon::Mosaic));
        }
        if (button->property("numberStyle").isValid())
            button->setChecked(button->property("numberStyle").toInt() == style.numberStyle);
    }
    check(QStringLiteral("annotationMagnifierRectangle"), style.effectShape != core::EffectShape::Ellipse);
    check(QStringLiteral("annotationMagnifierEllipse"), style.effectShape == core::EffectShape::Ellipse);
    check(QStringLiteral("annotationMagnifierCutout"), style.magnifierCutout);
    if (auto *connector = findChild<QToolButton *>(QStringLiteral("annotationMagConn")))
    {
        connector->setIcon(annotation::makeMagnifierConnectorDropdownIcon(style.magnifierConnectorStyle));
        const auto actions = connector->menu()->actions();
        for (int index = 0; index < actions.size(); ++index)
        {
            const QSignalBlocker blocker(actions[index]);
            actions[index]->setChecked(index == style.magnifierConnectorStyle);
        }
    }
    const auto slider = [this](const QString &name, int value) {
        if (auto *control = findChild<QSlider *>(name))
        {
            const QSignalBlocker blocker(control);
            control->setValue(value);
        }
    };
    slider(QStringLiteral("annotationEffectSize"), style.effectSize);
    if (auto *label = findChild<QLabel *>(QStringLiteral("mosaicEffectValue")))
        label->setText(QString::number(style.effectSize));
    slider(QStringLiteral("annotationMagnification"), qRound(style.magnification * 100));
    if (auto *label = findChild<QLabel *>(QStringLiteral("annotationMagnificationValue")))
        label->setText(QString::number(style.magnification, 'g', 3));
    if (auto *size = findChild<QComboBox *>(QStringLiteral("annotationNumberSize")))
    {
        const QSignalBlocker blocker(size);
        const QString value = QString::number((style.numberSize - 4) / 4);
        if (size->findText(value) < 0)
            size->addItem(value);
        size->setCurrentText(value);
    }
    syncTextControls();
}
} // namespace snipaste::ui
