#pragma once
#include "core/annotation/annotation_types.h"
#include <QColor>
#include <QFrame>
#include <QPointer>
#include <array>
#include <functional>
#include <initializer_list>
class QStackedLayout;
class QHBoxLayout;
class QToolButton;
namespace snipaste::ui
{
class AnnotationCanvas;
class AnnotationOptions final : public QFrame
{
  public:
    AnnotationOptions(QWidget *parent, std::function<void()> refreshCursor,
                      QObject *controlEventFilter = nullptr);
    void setCanvas(AnnotationCanvas *canvas);
    void showPage(int index);
    QStackedLayout *stack() const { return m_toolOptionsStack; }
    void closePopups();

  private:
    static QFrame *makeSeparator(QWidget *parent);
    void addWidthPresets(QHBoxLayout *layout, std::initializer_list<int> widths);
    QWidget *createShapesPage();
    QWidget *createLinesPage();
    QWidget *createPenPage();
    QWidget *createMarkerPage();
    QWidget *createMosaicPage();
    QWidget *createTextPage();
    void syncTextControls();
    void syncStyleControls();
    QWidget *createNumberPage();
    QWidget *createMagnifierPage();
    QWidget *createEraserPage();
    QToolButton *makeWidthPreset(QWidget *parent, int initialWidth, const QString &name,
                                 bool checked = false);
    QWidget *makeColorModule(QWidget *parent);
    QToolButton *makeLineStyleDropdown(QWidget *parent);
    void updateDynamicWidthButtons(qreal width);
    void updateAllColorWatchers(const QColor &color);
    void showCornerRadiusPopup(QWidget *source);
    void refreshAnnotationCursor()
    {
        if (m_refreshCursor)
            m_refreshCursor();
    }
    std::function<void()> m_refreshCursor;
    QPointer<AnnotationCanvas> m_canvas;
    QStackedLayout *m_toolOptionsStack = nullptr;
    std::array<bool, 9> m_createdPages{};
    QPointer<QObject> m_controlEventFilter;
    QList<QToolButton *> m_colorWatchers;
    QList<QPair<QToolButton *, QString>> m_paletteButtons;
    QList<QToolButton *> m_dynamicWidthButtons;
    QPointer<QFrame> m_cornerRadiusPopup;
    QColor m_currentAnnotationColor = QColor(QStringLiteral("#ed1c24"));
    QToolButton *m_lineStyleButton = nullptr;
    QToolButton *m_arrowStyleButton = nullptr;
    QToolButton *m_startArrowButton = nullptr;
    QToolButton *m_endArrowButton = nullptr;
    QToolButton *m_toggleArrowButton = nullptr;
    core::ArrowStyle m_lastArrowStyle = core::ArrowStyle::End;
};
} // namespace snipaste::ui
