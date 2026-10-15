#pragma once
#include <QColor>
#include <QFrame>
#include <QPointer>
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
    AnnotationOptions(QWidget *parent, std::function<void()> refreshCursor);
    void setCanvas(AnnotationCanvas *canvas);
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
    QList<QToolButton *> m_colorWatchers;
    QList<QPair<QToolButton *, QString>> m_paletteButtons;
    QList<QToolButton *> m_dynamicWidthButtons;
    QPointer<QFrame> m_cornerRadiusPopup;
    QColor m_currentAnnotationColor = QColor(QStringLiteral("#ed1c24"));
    QToolButton *m_lineStyleButton = nullptr;
    QToolButton *m_arrowStyleButton = nullptr;
};
} // namespace snipaste::ui
