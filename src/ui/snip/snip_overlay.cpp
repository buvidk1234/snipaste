#include "ui/snip/snip_overlay.h"
#include "core/command/argument_parser.h"
#include "platform/keyboard.h"
#include "platform/screen_capture.h"
#include "platform/windows/capture_target.h"
#include "ui/annotation/annotation_canvas.h"
#include "ui/annotation/annotation_options.h"
#include "ui/clipboard_output.h"
#include "ui/recognition/ocr_overlay.h"
#include "ui/snip/scroll_capture_panel.h"
#include "ui/snip/scroll_capture_preview.h"

#include <QApplication>
#include <QBackingStore>
#include <QClipboard>
#include <QCoreApplication>
#include <QCursor>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QKeyEvent>
#include <QLayout>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPolygon>
#include <QProcess>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN
#include <windows.h>

#include <mmsystem.h>
#endif

namespace snipaste::ui
{

class SnipBorderOverlay final : public QWidget
{
  public:
    explicit SnipBorderOverlay(SnipOverlay *parent) : QWidget(parent), m_overlay(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_TranslucentBackground);
    }

  protected:
    void paintEvent(QPaintEvent *) override
    {
        if (m_overlay)
        {
            QPainter painter(this);
            m_overlay->paintSelectionFrame(painter);
        }
    }

  private:
    SnipOverlay *m_overlay = nullptr;
};

SnipOverlay::SnipOverlay(QScreen *screen, bool autoCopy, QWidget *parent)
    : SnipOverlay(screen, autoCopy, false, {}, parent)
{
}

SnipOverlay::SnipOverlay(QScreen *screen, bool autoCopy, bool captureCursor, QRect initialSelection,
                         QWidget *parent)
    : SnipOverlay(screen, parent, false)
{
    beginCapture(screen, autoCopy, captureCursor, std::move(initialSelection));
}

SnipOverlay::SnipOverlay(QScreen *screen, QWidget *parent, bool reusable)
    : QWidget(parent), m_reusable(reusable)
{
    Q_ASSERT(screen);
    setAttribute(Qt::WA_DeleteOnClose, !reusable);
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setGeometry(screen->geometry());
    m_selectionBorder = new SnipBorderOverlay(this);
    m_selectionBorder->setGeometry(rect());
    m_selectionBorder->show();
    m_targetDetector = [this](QPoint point, bool elements) {
        if (QGuiApplication::platformName() == QStringLiteral("offscreen"))
            return QRect();
        return platform::captureTargetAt(point, size(), winId(), elements, m_excludedDetectionProcesses,
                                         m_unstableDetectionProcesses);
    };
}

SnipOverlay *SnipOverlay::createPrepared(QScreen *screen)
{
    auto *overlay = new SnipOverlay(screen, nullptr, true);
    overlay->createToolbar();
    overlay->m_preparedCanvas = new AnnotationCanvas({}, overlay);
    overlay->m_preparedCanvas->setObjectName(QStringLiteral("preparedSelectionCanvas"));
    overlay->m_preparedCanvas->setMinimumSize(0, 0);
    overlay->m_preparedCanvas->hide();
    // Prepare styles and the action row now. Keep hidden option-page layouts
    // lazy: activating all of them retains large font/style caches at idle.
    overlay->ensurePolished();
    overlay->m_toolbar->adjustSize();
    overlay->m_sizeToolbar->adjustSize();
    overlay->winId();
    return overlay;
}

void SnipOverlay::beginCapture(QScreen *screen, bool autoCopy, bool captureCursor, QRect initialSelection)
{
    Q_ASSERT(screen && !m_captureActive);
    ++m_sessionGeneration;
    m_captureActive = true;
    m_autoCopy = autoCopy;
    m_captureCursor = captureCursor;
    m_initialSelection = std::move(initialSelection);
    m_outputEmitted = false;
    m_toolbarHidden = false;
    m_dragging = m_dragMoved = m_refreshing = false;
    m_hoverSelection = m_pressedTarget = {};
    m_dragOrigin = m_transformOrigin = {};
    m_transformOriginSelection = {};
    m_targetPoint = QPoint(-1, -1);
    m_targetLevel = 0;
    m_historyIndex = m_regionHistoryIndex = -1;
    m_liveSelection = m_lastSelection = {};
    m_regionHistory.clear();
    m_fixedSelectionSize = {};
    m_aspectRatio = 0;
    m_virtualDesktopMode = false;
    m_elementMode = true;
    m_hexColor = m_showMagnifierGuides = false;
    m_roundedCorners = m_outputShadow = m_outputBorder = false;
    m_captureCornerRadius = 8;
    if (m_roundedButton)
        m_roundedButton->setChecked(false);
    if (m_annotationOptions)
        m_annotationOptions->closePopups();
    if (m_toolbar)
        resetSelection();
    m_screenGeometry = screen->geometry();
    m_singleScreenGeometry = m_screenGeometry;
    setGeometry(m_screenGeometry);
    if (m_toolbar)
    {
        const qreal compact = qMin<qreal>(1, qMax<qreal>(0, (width() - 40) / 576.0));
        for (auto *button : m_toolbar->findChildren<QToolButton *>())
            if (button->property("captureActionButton").toBool())
                button->setFixedSize(qMax(16, int(32 * compact)), 30);
    }
    m_background = platform::captureScreen(screen);
    m_background.setDevicePixelRatio(1);
    m_cleanBackground = m_background;
    m_singleScreenBackground = m_background;
    m_singleScreenBackgroundProvider = [screen = QPointer<QScreen>(screen)] {
        return screen ? platform::captureScreen(screen) : QImage();
    };
    m_backgroundProvider = m_singleScreenBackgroundProvider;
    m_cursorPosition = QCursor::pos() - m_screenGeometry.topLeft();
    if (m_captureCursor)
        drawCapturedCursor(screen);
    if (!m_initialSelection.isEmpty())
        m_selection = m_initialSelection.intersected(rect());
    if (!m_toolbar)
    {
        if (m_initialSelection.isEmpty() && QGuiApplication::platformName() != QStringLiteral("offscreen"))
        {
            const auto generation = m_sessionGeneration;
            QTimer::singleShot(40, this, [this, generation] {
                if (m_captureActive && generation == m_sessionGeneration && !m_toolbar)
                {
                    createToolbar();
                    updateToolbarGeometry();
                    update();
                }
            });
        }
        else
        {
            createToolbar();
        }
    }
    // Prepared application sessions apply preferences before showEvent.
    if (!m_reusable && !m_selection.isEmpty())
        confirmSelection();
}

void SnipOverlay::setQuickResponse(bool enabled)
{
    m_quickResponse = enabled;
    if (!enabled && !m_toolbar)
    {
        createToolbar();
        updateToolbarGeometry();
        update();
    }
}

void SnipOverlay::setRememberToolColors(bool enabled)
{
    m_rememberToolColors = enabled;
    if (m_canvas)
        m_canvas->setRememberToolColors(enabled);
}

void SnipOverlay::setFreeSelect(bool enabled)
{
    m_freeSelect = enabled;
    if (m_canvas)
        m_canvas->setFreeSelect(enabled);
}

void SnipOverlay::setDisableEraserEditing(bool enabled)
{
    m_disableEraserEditing = enabled;
    if (m_canvas)
        m_canvas->setDisableEraserEditing(enabled);
}

void SnipOverlay::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_selectionBorder)
        m_selectionBorder->setGeometry(rect());
    if (m_ocrOverlay)
        m_ocrOverlay->setGeometry(rect());
}

void SnipOverlay::captureSelectionImmediately()
{
    if (m_selection.isValid() && !m_selection.isEmpty())
        copySelection();
}

void SnipOverlay::setFixedSelectionSize(const QSize &size)
{
    m_fixedSelectionSize = size.isValid() ? size.expandedTo(QSize(1, 1)) : QSize();
    m_aspectRatio = m_fixedSelectionSize.isValid() && m_fixedSelectionSize.height() > 0
                        ? qreal(m_fixedSelectionSize.width()) / m_fixedSelectionSize.height()
                        : 0.0;
}

void SnipOverlay::setTargetDetector(TargetDetector detector)
{
    m_targetDetector = std::move(detector);
    m_nativeTargetDetector = false;
    m_targetPoint = QPoint(-1, -1);
    updateHoverTarget();
}

void SnipOverlay::setMouseActions(const QString &doubleClickAction, bool doubleClickExit,
                                  const QString &middleClickAction, bool middleClickExit,
                                  const QString &enterAction, bool enterExit)
{
    m_doubleClickAction = doubleClickAction.trimmed();
    m_middleClickAction = middleClickAction.trimmed();
    m_enterAction = enterAction.trimmed();
    m_doubleClickExit = doubleClickExit;
    m_middleClickExit = middleClickExit;
    m_enterExit = enterExit;
}

void SnipOverlay::setDetectionOptions(bool detectWindows, bool detectElements, bool showHints)
{
    m_detectWindows = detectWindows;
    m_detectElements = detectElements;
    m_elementMode = detectElements;
    m_showShortcutHints = showHints;
    m_targetPoint = QPoint(-1, -1);
    if (isVisible())
        updateHoverTarget();
}

void SnipOverlay::setCaptureDisplayOptions(bool showAnchor, const QColor &maskColor,
                                           const QColor &anchorColor, int borderWidth, bool magnifierMask,
                                           bool magnifierBorder, bool magnifierAnchor, bool crosshair,
                                           bool disableGuides, bool hideOneByOne)
{
    m_showAnchor = showAnchor;
    if (maskColor.isValid())
        m_maskColor = maskColor;
    if (anchorColor.isValid())
        m_anchorColor = anchorColor;
    m_captureBorderWidth = qBound(1, borderWidth, 20);
    m_magnifierMask = magnifierMask;
    m_magnifierBorder = magnifierBorder;
    m_magnifierAnchor = magnifierAnchor;
    m_showCrosshair = crosshair;
    m_disableGuides = disableGuides;
    m_hideOneByOne = hideOneByOne;
    if (m_disableGuides)
        m_showMagnifierGuides = false;
    m_targetPoint = QPoint(-1, -1);
    m_targetHierarchy.clear();
    updateHoverTarget();
    update();
}

void SnipOverlay::playCaptureSound() const
{
    if (!m_captureSoundEnabled || m_captureSoundFile.isEmpty())
        return;

    QFileInfo configured(m_captureSoundFile);
    QString path = configured.isAbsolute()
                       ? configured.absoluteFilePath()
                       : QDir(QCoreApplication::applicationDirPath()).filePath(m_captureSoundFile);
    if (!QFileInfo::exists(path) && configured.isRelative())
        path = QDir::current().filePath(m_captureSoundFile);
    if (!QFileInfo::exists(path))
        return;

#ifdef Q_OS_WIN
    // PlaySound is asynchronous and replaces a previous capture sound, so a
    // rapid sequence of copies does not block the capture UI or accumulate
    // child processes.
    PlaySoundW(reinterpret_cast<LPCWSTR>(path.utf16()), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
#elif defined(Q_OS_MACOS)
    QProcess::startDetached(QStringLiteral("afplay"), {path});
#else
    if (!QProcess::startDetached(QStringLiteral("paplay"), {path}))
        QProcess::startDetached(QStringLiteral("aplay"), {path});
#endif
}

void SnipOverlay::setMagnifierOptions(bool automatic, bool superCapture, const QStringList &colorFormats,
                                      bool hideHexPrefix, bool uppercaseHex, bool grid, bool coordinates,
                                      bool hints, int zoom, bool superMode)
{
    m_showMagnifier = superMode ? superCapture : automatic;
    m_magnifierGrid = grid;
    m_magnifierCoordinates = coordinates;
    m_magnifierHints = hints;
    m_magnifierHideHexPrefix = hideHexPrefix;
    m_magnifierUppercaseHex = uppercaseHex;
    m_magnifierZoom = qBound(3, zoom, 30);
    m_magnifierColorFormats.clear();
    for (const QString &format : colorFormats)
    {
        if ((format == QStringLiteral("RGB") || format == QStringLiteral("HEX")) &&
            !m_magnifierColorFormats.contains(format))
            m_magnifierColorFormats.append(format);
    }
    if (m_magnifierColorFormats.isEmpty())
        m_magnifierColorFormats.append(QStringLiteral("RGB"));
    m_hexColor = m_magnifierColorFormats.first() == QStringLiteral("HEX");
    update();
}

void SnipOverlay::setLastSelection(QRect selection)
{
    m_lastSelection = selection.intersected(rect());
    setSelectionHistory({m_lastSelection});
}

void SnipOverlay::setSelectionHistory(const QList<QRect> &regions)
{
    m_regionHistory.clear();
    m_regionHistoryIndex = -1;
    for (const QRect &region : regions)
    {
        const QRect local = region.intersected(rect());
        if (!local.isEmpty())
            m_regionHistory.append(local);
    }
}

void SnipOverlay::updateHoverTarget(bool allowElements)
{
    if (m_selectionConfirmed || m_dragging)
        return;
    if (!m_detectWindows || m_fixedSelectionSize.isValid())
    {
        m_hoverSelection = {};
        m_targetHierarchy.clear();
        update();
        return;
    }
    if (m_targetPoint != m_cursorPosition || m_targetHierarchy.isEmpty())
    {
        m_targetPoint = m_cursorPosition;
        m_targetLevel = 0;
        m_targetHierarchy.clear();
        const bool detectElements = allowElements && m_elementMode && m_detectElements;
        if (m_nativeTargetDetector && QGuiApplication::platformName() != QStringLiteral("offscreen"))
        {
            m_targetHierarchy = platform::captureTargetHierarchyAt(
                m_cursorPosition, size(), winId(), detectElements, m_excludedDetectionProcesses,
                m_unstableDetectionProcesses);
        }
        else if (!m_nativeTargetDetector && m_targetDetector)
        {
            const QRect window = m_targetDetector(m_cursorPosition, false).intersected(rect());
            const QRect element =
                detectElements ? m_targetDetector(m_cursorPosition, true).intersected(rect()) : window;
            for (const QRect candidate : {element, window})
                if ((!m_hideOneByOne || candidate.width() > 1 || candidate.height() > 1) &&
                    candidate.contains(m_cursorPosition) && !m_targetHierarchy.contains(candidate))
                    m_targetHierarchy.append(candidate);
            if (!m_targetHierarchy.isEmpty() && !m_targetHierarchy.contains(rect()))
                m_targetHierarchy.append(rect());
            std::sort(m_targetHierarchy.begin(), m_targetHierarchy.end(), [](QRect a, QRect b) {
                return qint64(a.width()) * a.height() < qint64(b.width()) * b.height();
            });
        }
        if (m_hideOneByOne)
        {
            m_targetHierarchy.erase(std::remove_if(m_targetHierarchy.begin(), m_targetHierarchy.end(),
                                                   [](const QRect &candidate) {
                                                       return candidate.width() <= 1 &&
                                                              candidate.height() <= 1;
                                                   }),
                                    m_targetHierarchy.end());
        }
    }
    m_hoverSelection = m_targetHierarchy.isEmpty() ? QRect() : m_targetHierarchy.at(m_targetLevel);
    update();
}

void SnipOverlay::cycleTarget(int direction)
{
    updateHoverTarget();
    if (m_targetHierarchy.isEmpty())
        return;
    m_targetLevel = qBound(0, m_targetLevel + direction, int(m_targetHierarchy.size()) - 1);
    m_hoverSelection = m_targetHierarchy.at(m_targetLevel);
    update();
}

void SnipOverlay::forwardCanvasMouseEvent(QMouseEvent *event)
{
    if (!m_canvas || !event)
        return;
    const QPointF local = event->position() - QPointF(m_canvas->geometry().topLeft());
    QMouseEvent forwarded(event->type(), local, event->globalPosition(), event->button(), event->buttons(),
                          event->modifiers());
    QCoreApplication::sendEvent(m_canvas, &forwarded);
    event->accept();
}

void SnipOverlay::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    if (m_scrollCaptureActive && !m_selection.isEmpty())
    {
        painter.drawImage(rect(), m_background);
        painter.fillRect(rect(), m_maskColor);

        painter.save();
        painter.setClipRect(m_selection);
        painter.drawImage(rect(), m_background);
        painter.restore();

        painter.setPen(QPen(QColor(255, 255, 255, 160), 1));
        painter.drawRect(m_selection.adjusted(0, 0, -1, -1));

        const QString prompt = QStringLiteral("滚动页面截取更多内容");
        QFont font = painter.font();
        font.setPointSize(14);
        font.setBold(true);
        painter.setFont(font);
        const QFontMetrics fm(font);
        const int textH = fm.height();
        int textX = m_selection.left();
        int textY = m_selection.top() - 10;
        if (textY - textH < 10)
            textY = m_selection.top() + textH + 8;

        painter.setPen(QColor(0, 0, 0, 180));
        painter.drawText(textX + 1, textY + 1, prompt);
        painter.setPen(QColor(255, 255, 255, 245));
        painter.drawText(textX, textY, prompt);
        return;
    }
    painter.setRenderHint(QPainter::Antialiasing, false);
    QImage display = m_background;
    if (m_canvas)
    {
        // The canvas paints the transient text editor/brush preview itself.
        // The overlay only needs the committed layer here, which is cached by
        // AnnotationCanvas between pointer moves.
        const QImage annotated = m_canvas->displayImage();
        if (annotated.size() == m_background.size())
            display = annotated;
    }
    painter.drawImage(rect(), display);
    painter.fillRect(rect(), m_maskColor);

    const QRect selection = m_selection.isEmpty() ? m_hoverSelection : m_selection;
    if (selection.isValid() && !selection.isEmpty())
    {
        painter.save();
        painter.setClipRect(selection);
        painter.drawImage(rect(), display);
        painter.restore();
        if (!m_selectionBorder)
        {
            paintSelectionFrame(painter);
        }
    }
    if (m_showMagnifier && !m_selectionConfirmed && rect().contains(m_cursorPosition))
        drawMagnifier(painter);
    if ((m_showCrosshair || (m_showMagnifierGuides && !m_disableGuides)) && !m_selectionConfirmed &&
        rect().contains(m_cursorPosition))
        drawMagnifierGuides(painter);
    if (m_showShortcutHints && !m_dragging && !m_transformingSelection)
        drawShortcutHints(painter);
    if (m_selectionBorder)
        m_selectionBorder->update();
}

void SnipOverlay::paintSelectionFrame(QPainter &painter)
{
    const QRect selection = m_selection.isEmpty() ? m_hoverSelection : m_selection;
    if (!selection.isValid() || selection.isEmpty())
        return;

    if (m_transformingSelection && !m_transformOriginSelection.isEmpty() &&
        m_transformOriginSelection != selection)
    {
        // Keep the old frame visible while an edge is being dragged. It
        // makes the newly covered desktop area obvious and keeps the
        // resize gesture from looking like the image itself is scaling.
        QPen oldFrame(QColor(255, 255, 255, 230), 1, Qt::DashLine);
        painter.setPen(oldFrame);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(m_transformOriginSelection.adjusted(0, 0, -1, -1));
    }

    QPen pen(QColor(QStringLiteral("#2889ed")));
    pen.setWidth(m_captureBorderWidth);
    painter.setPen(pen);
    if (m_roundedCorners)
        painter.drawRoundedRect(selection.adjusted(0, 0, -1, -1), m_captureCornerRadius,
                                m_captureCornerRadius);
    else
        painter.drawRect(selection.adjusted(0, 0, -1, -1));

    const bool showAnchors = m_anchorThreshold <= 0 || (selection.width() >= m_anchorThreshold &&
                                                        selection.height() >= m_anchorThreshold);
    if (m_showAnchor && showAnchors)
    {
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(m_anchorColor, 1.5));
        painter.setBrush(QColor(QStringLiteral("#2889ed")));
        const QRect r = selection.adjusted(0, 0, -1, -1);
        for (const QPoint &point :
             {r.topLeft(), QPoint(r.center().x(), r.top()), r.topRight(), QPoint(r.right(), r.center().y()),
              r.bottomRight(), QPoint(r.center().x(), r.bottom()), r.bottomLeft(),
              QPoint(r.left(), r.center().y())})
            painter.drawEllipse(point, 4, 4);
        painter.setBrush(Qt::NoBrush);
    }
    if (!m_selectionConfirmed || m_transformingSelection)
    {
        const QString sizeText =
            QStringLiteral("%1 × %2 px")
                .arg(qRound(selection.width() * qreal(m_background.width()) / width()))
                .arg(qRound(selection.height() * qreal(m_background.height()) / height()));
        QFontMetrics metrics(font());
        const int labelWidth = qMax(92, metrics.horizontalAdvance(sizeText) + 18);
        const int labelHeight = 22;
        const int labelX =
            qBound(4, selection.center().x() - labelWidth / 2, qMax(4, width() - labelWidth - 4));
        const int labelY = selection.top() >= labelHeight + 4
                               ? selection.top() - labelHeight - 4
                               : qMin(height() - labelHeight - 4, selection.bottom() + 4);
        const QRect labelRect(labelX, qMax(4, labelY), labelWidth, labelHeight);
        painter.fillRect(labelRect, QColor(0, 0, 0, 180));
        painter.setPen(Qt::white);
        painter.drawText(labelRect, Qt::AlignCenter, sizeText);
    }
}

void SnipOverlay::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton)
    {
        m_transformingSelection = false;
        m_movingSelection = false;
        if (m_selectionConfirmed || m_dragging)
        {
            m_dragging = false;
            resetSelection();
            updateHoverTarget();
        }
        else
        {
            close();
        }
        return;
    }

    if (event->button() == Qt::MiddleButton)
    {
        executeConfiguredAction(m_middleClickAction, m_middleClickExit);
        return;
    }

    if (event->button() != Qt::LeftButton)
        return;

    const bool brushTool = m_canvas && (m_canvas->tool() == AnnotationCanvas::Tool::Pen ||
                                        m_canvas->tool() == AnnotationCanvas::Tool::Marker ||
                                        m_canvas->tool() == AnnotationCanvas::Tool::Mosaic ||
                                        m_canvas->tool() == AnnotationCanvas::Tool::Blur ||
                                        m_canvas->tool() == AnnotationCanvas::Tool::Eraser);
    const bool edgeResize = m_selectionConfirmed && resizeEdgesAt(event->position().toPoint()) != Qt::Edges();
    if (m_selectionConfirmed && m_canvas && !m_canvas->testAttribute(Qt::WA_TransparentForMouseEvents) &&
        !m_selection.contains(event->position().toPoint()) && (brushTool || !edgeResize))
    {
        m_externalDrawing = true;
        forwardCanvasMouseEvent(event);
        return;
    }

    if (m_selectionConfirmed)
    {
        beginSelectionTransform(event->position().toPoint());
        return;
    }

    m_cursorPosition = event->position().toPoint();
    updateHoverTarget();
    m_pressedTarget = m_hoverSelection;
    resetSelection();
    m_hoverSelection = {};
    m_dragging = true;
    m_dragMoved = false;
    m_dragOrigin = event->position().toPoint();
    m_selection = m_pressedTarget;
    if (m_toolbar)
        m_toolbar->hide();
    update();
}

void SnipOverlay::mouseMoveEvent(QMouseEvent *event)
{
    m_cursorPosition = event->position().toPoint();
    if (m_externalDrawing && m_canvas)
    {
        forwardCanvasMouseEvent(event);
        update();
        return;
    }
    if (m_transformingSelection)
    {
        updateSelectionTransform(m_cursorPosition);
        return;
    }
    updateSelectionCursor(m_cursorPosition);
    if (!m_dragging)
    {
        updateHoverTarget();
        update();
        return;
    }

    updateDragSelection(m_cursorPosition, event->modifiers());
    updateToolbarGeometry();
    update();
}

void SnipOverlay::updateDragSelection(QPoint position, Qt::KeyboardModifiers modifiers)
{
    if ((position - m_dragOrigin).manhattanLength() >= 3)
        m_dragMoved = true;
    if (!m_dragMoved && !m_fixedSelectionSize.isValid())
        return;

    if (m_fixedSelectionSize.isValid())
    {
        const QSize size = m_fixedSelectionSize.boundedTo(rect().size());
        QPoint topLeft = position;
        topLeft.setX(qBound(0, topLeft.x(), width() - size.width()));
        topLeft.setY(qBound(0, topLeft.y(), height() - size.height()));
        m_selection = QRect(topLeft, size);
    }
    else
    {
        m_selection = QRect(m_dragOrigin, position).normalized();
        if (m_keepAspectRatio && m_aspectRatio > 0.0 && !modifiers.testFlag(Qt::ShiftModifier))
        {
            const int width = qMax(1, m_selection.width());
            const int height = qMax(1, qRound(width / m_aspectRatio));
            const int signedHeight = position.y() >= m_dragOrigin.y() ? height : -height;
            m_selection =
                QRect(m_dragOrigin, QPoint(position.x(), m_dragOrigin.y() + signedHeight)).normalized();
        }
        else if (modifiers.testFlag(Qt::ShiftModifier))
        {
            const QPoint current = position;
            const int side = qMin(qAbs(current.x() - m_dragOrigin.x()), qAbs(current.y() - m_dragOrigin.y()));
            const QPoint corner(m_dragOrigin.x() + (current.x() >= m_dragOrigin.x() ? side : -side),
                                m_dragOrigin.y() + (current.y() >= m_dragOrigin.y() ? side : -side));
            m_selection = QRect(m_dragOrigin, corner).normalized();
        }
    }
    m_selection = m_selection.intersected(rect());
}

void SnipOverlay::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_externalDrawing)
    {
        forwardCanvasMouseEvent(event);
        m_externalDrawing = false;
        updateSelectionCursor(event->position().toPoint());
        update();
        return;
    }
    if (event->button() == Qt::LeftButton && m_transformingSelection)
    {
        updateSelectionTransform(event->position().toPoint());
        m_transformingSelection = false;
        m_movingSelection = false;
        m_resizeEdges = {};
        updateToolbarGeometry();
        updateSelectionCursor(event->position().toPoint());
        return;
    }
    if (event->button() == Qt::LeftButton && m_dragging)
    {
        m_cursorPosition = event->position().toPoint();
        updateDragSelection(m_cursorPosition, event->modifiers());
        m_dragging = false;
        m_selection = m_selection.intersected(rect());
        if (m_selection.width() < 2 || m_selection.height() < 2)
        {
            resetSelection();
            updateHoverTarget();
            return;
        }
        if (m_autoCopy && m_selection.isValid() && !m_selection.isEmpty())
            copySelection();
        else
            confirmSelection();
    }
}

void SnipOverlay::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        executeConfiguredAction(m_doubleClickAction, m_doubleClickExit);
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void SnipOverlay::keyPressEvent(QKeyEvent *event)
{
    if (m_ocrOverlay) {
        QApplication::sendEvent(m_ocrOverlay, event);
        return;
    }
    if (m_scrollCaptureActive)
    {
        if (event->key() == Qt::Key_Escape)
        {
            cancelScrollCapture();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        {
            finishScrollCapture();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_S && event->modifiers().testFlag(Qt::ControlModifier))
        {
            saveScrollCapture();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_E)
        {
            editScrollCapture();
            event->accept();
            return;
        }
    }
    if (event->key() == Qt::Key_Space && m_transformingSelection)
    {
        m_movingSelection = true;
        updateSelectionCursor(m_cursorPosition);
        event->accept();
        return;
    }
    if (handleProShortcut(event))
        return;
    if (event->key() == Qt::Key_Escape)
    {
        if (m_confirmEscape)
        {
            const auto answer = QMessageBox::question(this, QStringLiteral("取消截屏"),
                                                      QStringLiteral("确定要取消当前截屏吗？"),
                                                      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer != QMessageBox::Yes)
                return;
        }
        close();
        return;
    }

    if (event->matches(QKeySequence::Undo) || event->matches(QKeySequence::Redo))
    {
        if (m_canvas)
        {
            if (event->matches(QKeySequence::Undo))
                m_canvas->undo();
            else
                m_canvas->redo();
        }
        return;
    }

    if (m_canvas && event->key() == Qt::Key_Z &&
        event->modifiers() == (snipaste::platform::primaryModifier() | Qt::ShiftModifier))
    {
        m_canvas->clearAnnotations();
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Tab && !m_selectionConfirmed && !m_dragging)
    {
        m_elementMode = !m_elementMode;
        m_targetPoint = QPoint(-1, -1);
        updateHoverTarget();
        return;
    }

    if (event->key() == Qt::Key_Alt && !event->isAutoRepeat() && !m_selectionConfirmed && !m_dragging)
    {
        // Snipaste summons the magnifier with Alt and toggles the two guide
        // lines with the same key.  The magnifier itself remains available
        // after the shortcut so a short Alt press never makes the capture
        // preview disappear unexpectedly.
        m_showMagnifier = true;
        if (!m_disableGuides)
            m_showMagnifierGuides = !m_showMagnifierGuides;
        update();
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Shift && !event->isAutoRepeat() && m_showMagnifier && !m_selectionConfirmed &&
        !m_dragging)
    {
        m_hexColor = !m_hexColor;
        update();
        return;
    }

    if (event->key() == Qt::Key_C && event->modifiers() == Qt::NoModifier && m_showMagnifier &&
        !m_selectionConfirmed)
    {
        QApplication::clipboard()->setText(sampledColorText());
        return;
    }

    if (!m_dragging && event->modifiers() == Qt::NoModifier)
    {
        QPoint delta;
        switch (event->key())
        {
        case Qt::Key_W:
            delta = QPoint(0, -1);
            break;
        case Qt::Key_S:
            delta = QPoint(0, 1);
            break;
        case Qt::Key_A:
            delta = QPoint(-1, 0);
            break;
        case Qt::Key_D:
            delta = QPoint(1, 0);
            break;
        default:
            break;
        }
        if (!m_exchangeNavigationKeys && !delta.isNull())
        {
            m_cursorPosition += delta;
            m_cursorPosition.setX(qBound(0, m_cursorPosition.x(), width() - 1));
            m_cursorPosition.setY(qBound(0, m_cursorPosition.y(), height() - 1));
            if (QGuiApplication::platformName() != QStringLiteral("offscreen"))
                QCursor::setPos(mapToGlobal(m_cursorPosition));
            updateHoverTarget();
            return;
        }
        if (m_exchangeNavigationKeys)
        {
            if (event->key() == Qt::Key_Left)
                delta = QPoint(-1, 0);
            if (event->key() == Qt::Key_Right)
                delta = QPoint(1, 0);
            if (event->key() == Qt::Key_Up)
                delta = QPoint(0, -1);
            if (event->key() == Qt::Key_Down)
                delta = QPoint(0, 1);
            if (!delta.isNull())
            {
                m_cursorPosition += delta;
                m_cursorPosition.setX(qBound(0, m_cursorPosition.x(), width() - 1));
                m_cursorPosition.setY(qBound(0, m_cursorPosition.y(), height() - 1));
                if (QGuiApplication::platformName() != QStringLiteral("offscreen"))
                    QCursor::setPos(mapToGlobal(m_cursorPosition));
                updateHoverTarget();
                return;
            }
        }
    }

    if ((event->key() == Qt::Key_A && event->modifiers() == snipaste::platform::primaryModifier()) ||
        (event->key() == Qt::Key_R && event->modifiers() == Qt::NoModifier))
    {
        if (event->key() == Qt::Key_A && event->modifiers() == snipaste::platform::primaryModifier())
        {
            // Snipaste cycles the full-screen target on repeated primary+A:
            // current screen -> virtual desktop -> current screen.  The
            // first press must always establish the current-screen frame,
            // even when the overlay started with no confirmed selection.
            if (QGuiApplication::screens().size() <= 1 || !m_selectionConfirmed)
                selectCurrentScreen();
            else if (m_virtualDesktopMode)
                selectCurrentScreen();
            else
                selectVirtualDesktop();
            return;
        }
        const QRect target = m_lastSelection;
        if (!target.isEmpty())
        {
            m_dragging = false;
            resetSelection();
            m_selection = target;
            confirmSelection();
        }
        return;
    }

    if (event->key() == Qt::Key_S && event->modifiers().testFlag(snipaste::platform::primaryModifier()) &&
        event->modifiers().testFlag(Qt::AltModifier) &&
        m_copyAsFileMode != static_cast<int>(CopyAsFileMode::Disabled))
    {
        copySelection(true, true);
        return;
    }

    if (event->key() == Qt::Key_S && event->modifiers().testFlag(snipaste::platform::primaryModifier()))
    {
        if (event->modifiers().testFlag(Qt::ShiftModifier))
            quickSaveSelection();
        else
            saveSelection();
        return;
    }

    if (event->key() == Qt::Key_P && event->modifiers() == snipaste::platform::primaryModifier())
    {
        printSelection();
        return;
    }

    if (event->key() == Qt::Key_T && event->modifiers().testFlag(snipaste::platform::primaryModifier()))
    {
        pasteSelection();
        return;
    }

    if ((event->key() == Qt::Key_C && event->modifiers().testFlag(snipaste::platform::primaryModifier())) ||
        (event->key() == Qt::Key_Insert &&
         event->modifiers().testFlag(snipaste::platform::primaryModifier())))
    {
        copySelection();
        return;
    }

    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
    {
        executeConfiguredAction(m_enterAction, m_enterExit);
        return;
    }

    QWidget::keyPressEvent(event);
}

void SnipOverlay::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && m_transformingSelection)
    {
        m_movingSelection = false;
        updateSelectionCursor(m_cursorPosition);
        event->accept();
        return;
    }
    QWidget::keyReleaseEvent(event);
}

void SnipOverlay::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() != QEvent::WindowDeactivate || !m_exitOnFocusLoss)
        return;
    const auto generation = m_sessionGeneration;
    QTimer::singleShot(0, this, [this, generation] {
        if (generation != m_sessionGeneration || !m_captureActive || !isVisible() || isActiveWindow() ||
            QApplication::activePopupWidget() || QApplication::activeModalWidget())
            return;
        close();
    });
}

void SnipOverlay::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    activateWindow();
    raise();
    setFocus();
    // The first frame only needs the underlying window rectangle.  Element
    // detection uses IAccessible/UIAutomation and can block the GUI thread;
    // defer it until the frame is already visible and the cursor has stayed
    // still.  A cursor move or a click will perform the detailed hit test on
    // demand through the normal event path.
    if (!m_selection.isEmpty() && !m_selectionConfirmed)
        confirmSelection();
    updateHoverTarget(false);
    updateToolbarGeometry();
    update();
    if (m_detectElements && m_elementMode)
    {
        const QPoint initialCursor = m_cursorPosition;
        const auto generation = m_sessionGeneration;
        QTimer::singleShot(120, this, [this, initialCursor, generation] {
            if (generation != m_sessionGeneration || !m_captureActive || !isVisible() ||
                m_selectionConfirmed || m_dragging || m_cursorPosition != initialCursor)
                return;
            m_targetPoint = QPoint(-1, -1);
            m_targetHierarchy.clear();
            updateHoverTarget(true);
            update();
        });
    }
}

QPoint SnipOverlay::sampledPixel() const
{
    const qreal scaleX = static_cast<qreal>(m_background.width()) / qMax(1, width());
    const qreal scaleY = static_cast<qreal>(m_background.height()) / qMax(1, height());
    return QPoint(qBound(0, qRound(m_cursorPosition.x() * scaleX), qMax(0, m_background.width() - 1)),
                  qBound(0, qRound(m_cursorPosition.y() * scaleY), qMax(0, m_background.height() - 1)));
}

QString SnipOverlay::sampledColorText() const
{
    if (m_background.isNull())
        return {};
    const QColor color = m_background.pixelColor(sampledPixel());
    const bool hasRgb = m_magnifierColorFormats.contains(QStringLiteral("RGB"));
    const bool hasHex = m_magnifierColorFormats.contains(QStringLiteral("HEX"));
    const bool useHex = hasHex && (!hasRgb || m_hexColor);
    if (!useHex)
        return QStringLiteral("%1, %2, %3").arg(color.red()).arg(color.green()).arg(color.blue());
    QString hex = color.name(QColor::HexRgb);
    if (m_magnifierHideHexPrefix)
        hex.remove(QLatin1Char('#'));
    return m_magnifierUppercaseHex ? hex.toUpper() : hex;
}

void SnipOverlay::drawMagnifier(QPainter &painter) const
{
    if (m_background.isNull() || !rect().contains(m_cursorPosition))
        return;

    const qreal zoomScale = qreal(m_magnifierZoom) / 9.0;
    const int panelWidth = qMax(1, qRound(168 * zoomScale));
    const int previewHeight = qMax(1, qRound(132 * zoomScale));
    constexpr int infoHeight = 92;
    QPoint topLeft = m_cursorPosition + QPoint(10, 20);
    if (topLeft.x() + panelWidth > width())
        topLeft.setX(m_cursorPosition.x() - panelWidth - 10);
    if (topLeft.y() + previewHeight + infoHeight > height())
        topLeft.setY(m_cursorPosition.y() - previewHeight - infoHeight - 20);
    topLeft.setX(qBound(0, topLeft.x(), qMax(0, width() - panelWidth)));
    topLeft.setY(qBound(0, topLeft.y(), qMax(0, height() - previewHeight - infoHeight)));
    const QRect target(topLeft, QSize(panelWidth, previewHeight));
    const QPoint pixel = sampledPixel();
    // Keep the sampled pixel at the centre even at the edge of a monitor.
    QImage crop(19, 15, QImage::Format_RGB32);
    crop.fill(Qt::black);
    QImage background = m_background;
    background.setDevicePixelRatio(1);
    {
        QPainter samplePainter(&crop);
        samplePainter.drawImage(QPoint(9, 7) - pixel, background);
    }
    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(target, crop);
    const qreal cellWidth = qreal(target.width()) / 19;
    const qreal cellHeight = qreal(target.height()) / 15;
    if (m_magnifierGrid)
    {
        QPen grid(QColor(190, 215, 245, 75));
        grid.setCosmetic(true);
        painter.setPen(grid);
        for (int x = 1; x < 19; ++x)
            painter.drawLine(QPointF(target.left() + x * cellWidth, target.top()),
                             QPointF(target.left() + x * cellWidth, target.bottom()));
        for (int y = 1; y < 15; ++y)
            painter.drawLine(QPointF(target.left(), target.top() + y * cellHeight),
                             QPointF(target.right(), target.top() + y * cellHeight));
    }
    painter.setBrush(Qt::NoBrush);
    if (m_magnifierBorder)
    {
        painter.setPen(QPen(Qt::white, 1));
        painter.drawRect(target.adjusted(0, 0, -1, -1));
    }
    // Always identify the sampled pixel, including with the grid or anchors disabled.
    // Leave its interior untouched so the preview still shows the actual colour.
    const QRectF sampledCell(target.left() + 9 * cellWidth, target.top() + 7 * cellHeight,
                             cellWidth, cellHeight);
    const QPointF centre = sampledCell.center();
    const qreal guideLength = m_magnifierAnchor ? qMax(panelWidth, previewHeight) : 7;
    const QLineF guides[] = {
        {{qMax(qreal(target.left() + 2), sampledCell.left() - 3 - guideLength), centre.y()},
         {sampledCell.left() - 3, centre.y()}},
        {{sampledCell.right() + 3, centre.y()},
         {qMin(qreal(target.right() - 2), sampledCell.right() + 3 + guideLength), centre.y()}},
        {{centre.x(), qMax(qreal(target.top() + 2), sampledCell.top() - 3 - guideLength)},
         {centre.x(), sampledCell.top() - 3}},
        {{centre.x(), sampledCell.bottom() + 3},
         {centre.x(), qMin(qreal(target.bottom() - 2), sampledCell.bottom() + 3 + guideLength)}}};
    for (const auto &pen : {QPen(Qt::black, 3), QPen(Qt::white, 1)})
    {
        painter.setPen(pen);
        painter.drawLines(guides, 4);
        painter.drawRect(sampledCell.adjusted(-1, -1, 1, 1));
    }

    const QRect info(target.left(), target.bottom() + 1, panelWidth, infoHeight);
    if (m_magnifierMask)
        painter.fillRect(info, QColor(0, 0, 0, 210));
    QFont font = painter.font();
    font.setPixelSize(13);
    painter.setFont(font);
    painter.setPen(Qt::white);
    const QPoint coordinates = m_screenGeometry.topLeft() + pixel;
    if (m_magnifierCoordinates)
        painter.drawText(info.adjusted(0, 3, 0, -infoHeight + 24), Qt::AlignCenter,
                         QStringLiteral("(%1, %2)").arg(coordinates.x()).arg(coordinates.y()));
    const QRect swatch(info.left() + 28, info.top() + 30, 13, 13);
    painter.fillRect(swatch, m_background.pixelColor(pixel));
    painter.drawRect(swatch);
    painter.drawText(QRect(info.left() + 49, info.top() + 24, 130, 26), Qt::AlignVCenter | Qt::AlignLeft,
                     sampledColorText());
    if (m_magnifierHints)
    {
        painter.drawText(QRect(info.left(), info.top() + 52, panelWidth, 20), Qt::AlignCenter,
                         QStringLiteral("按 C 复制颜色值"));
        painter.drawText(QRect(info.left(), info.top() + 71, panelWidth, 20), Qt::AlignCenter,
                         QStringLiteral("按 Shift 切换 RGB/HEX"));
    }
    painter.restore();
}

void SnipOverlay::drawMagnifierGuides(QPainter &painter) const
{
    if (!rect().contains(m_cursorPosition))
        return;
    painter.save();
    QPen guide(QColor(255, 255, 255, 175), 1, Qt::DashLine);
    guide.setCosmetic(true);
    painter.setPen(guide);
    painter.drawLine(QPoint(0, m_cursorPosition.y()), QPoint(width(), m_cursorPosition.y()));
    painter.drawLine(QPoint(m_cursorPosition.x(), 0), QPoint(m_cursorPosition.x(), height()));
    painter.restore();
}

void SnipOverlay::drawShortcutHints(QPainter &painter) const
{
    const int rows = m_selectionConfirmed ? 7 : 8;
    const int panelHeight = 12 + rows * 32;
    const QRect panel(12, qMax(0, height() - panelHeight - 12), 326, panelHeight);
    painter.save();
    painter.fillRect(panel, QColor(0, 0, 0, 175));
    painter.setPen(QColor(135, 145, 155));
    painter.drawRect(panel.adjusted(0, 0, -1, -1));
    QFont font = painter.font();
    font.setPixelSize(13);
    painter.setFont(font);
    const auto row = [&](int index, const QStringList &keys, const QString &description) {
        const int y = panel.top() + 10 + 32 * index;
        const QFontMetrics metrics(font);
        int totalWidth = 0;
        const auto keyExtent = [&metrics](const QString &key) {
            return key == QStringLiteral("mouse") ? 18
                   : key == QStringLiteral("/")   ? 8
                                                  : qMax(18, metrics.horizontalAdvance(key) + 6);
        };
        for (const QString &key : keys)
            totalWidth += keyExtent(key) + 3;
        int x = panel.left() + 105 - totalWidth;
        for (const QString &key : keys)
        {
            const int keyWidth = keyExtent(key);
            const QRect keyRect(x, y, keyWidth, 19);
            painter.setPen(QColor(185, 190, 195));
            if (key == QStringLiteral("mouse"))
            {
                const QRect mouse = keyRect.adjusted(3, 0, -3, 0);
                painter.drawRoundedRect(mouse, 5, 5);
                painter.drawLine(mouse.left(), mouse.top() + 8, mouse.right(), mouse.top() + 8);
                painter.drawLine(mouse.center().x(), mouse.top() + 2, mouse.center().x(), mouse.top() + 7);
            }
            else
            {
                if (key != QStringLiteral("/"))
                    painter.drawRect(keyRect);
                painter.setPen(Qt::white);
                painter.drawText(keyRect, Qt::AlignCenter, key);
            }
            x += keyWidth + 3;
        }
        painter.setPen(Qt::white);
        painter.drawText(QRect(panel.left() + 115, y - 1, 205, 22), Qt::AlignVCenter, description);
    };
    row(0, {QStringLiteral("W"), QStringLiteral("A"), QStringLiteral("S"), QStringLiteral("D")},
        QStringLiteral("将鼠标指针移动 1 像素"));
    if (m_selectionConfirmed)
        row(1, {QStringLiteral("↑"), QStringLiteral("↓"), QStringLiteral("←"), QStringLiteral("→")},
            QStringLiteral("将截屏区域移动 1 像素"));
    else
        row(1, {QStringLiteral("Tab")}, QStringLiteral("切换检测窗口/检测界面元素"));
    row(2, {QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("/"), QStringLiteral("mouse")},
        m_selectionConfirmed ? QStringLiteral("从中心调整区域大小")
                             : QStringLiteral("选择上一层/下一层界面元素"));
    row(3, {QStringLiteral("Ctrl"), QStringLiteral("A")}, QStringLiteral("设置截屏区域为当前屏幕/全屏"));
    if (!m_selectionConfirmed)
        row(4, {QStringLiteral("R"), QStringLiteral("Shift"), QStringLiteral("R")},
            QStringLiteral("使用上一次截屏的区域"));
    row(rows - 3, {QStringLiteral(","), QStringLiteral(".")}, QStringLiteral("回溯截屏历史"));
    row(rows - 2, {QStringLiteral("`"), QStringLiteral("/"), QStringLiteral("!")},
        QStringLiteral("显示/隐藏捕获的鼠标指针"));
    row(rows - 1, {QStringLiteral("F5")}, QStringLiteral("刷新截图"));
    painter.restore();
}

void SnipOverlay::closeEvent(QCloseEvent *event)
{
    if (m_scrollCaptureActive)
    {
        m_scrollCaptureActive = false;
        m_scrollAutoRunning = false;
        m_scrollCaptureBusy = false;
        // The low-level hook is process-wide; always remove it before the
        // overlay is destroyed, including Esc/window-manager close paths.
        stopScrollInputMonitoring();
        if (m_scrollPanel)
        {
            m_scrollPanel->hide();
            m_scrollPanel->deleteLater();
            m_scrollPanel = nullptr;
        }
        restoreScrollCaptureCursor();
        m_scrollWindow = 0;
    }
    if (m_includeUnsavedHistory && !m_outputEmitted && m_selectionConfirmed)
    {
        const QImage image = selectionImage();
        if (!image.isNull())
            emit unsavedScreenshotCaptured(image);
        if (!image.isNull())
            emit unsavedScreenshotCapturedWithBackground(image, m_cleanBackground);
    }
    if (m_toolbar)
        m_toolbar->hide();
    if (m_sizeToolbar)
        m_sizeToolbar->hide();
    if (m_toolOptions)
        m_toolOptions->hide();
    if (m_annotationOptions)
        m_annotationOptions->closePopups();
    // The capture result has already been delivered synchronously above.
    // Drop the desktop-sized images before QWidget's deferred destruction so
    // a finished screenshot does not keep the entire monitor resident.
    releaseCaptureResources();
    QWidget::closeEvent(event);
    // Keep destruction deterministic for callers that close the overlay from
    // a toolbar action while an editor or popup still owns focus.  Qt's
    // WA_DeleteOnClose path can be deferred behind that nested interaction;
    // deleteLater() is idempotent and guarantees the overlay is released on
    // the next event turn after the close event has been accepted.
    if (event->isAccepted())
    {
        const bool wasActive = m_captureActive;
        m_captureActive = false;
        ++m_sessionGeneration;
        if (m_reusable)
        {
            hide();
            resize(1, 1);
            // A hidden QWidget retains its last full-screen backing buffer.
            // Shrink it explicitly; the next capture restores the geometry.
            if (auto *store = backingStore())
                store->resize(QSize(1, 1));
        }
        if (wasActive)
            emit sessionClosed();
        if (!m_reusable)
            deleteLater();
    }
}

void SnipOverlay::releaseCaptureResources()
{
    clearOcrOverlay();
    // Release the canvas pixels first, including the converted render cache
    // and any shared references to the original full-screen image.
    if (m_preparedCanvas)
    {
        m_preparedCanvas->hide();
        m_preparedCanvas->resetCapture();
        m_preparedCanvas->setObjectName(QStringLiteral("preparedSelectionCanvas"));
    }
    else
    {
        delete m_canvas;
    }
    if (m_annotationOptions)
        m_annotationOptions->setCanvas(nullptr);
    m_canvas = nullptr;

    m_background = {};
    m_cleanBackground = {};
    m_selectionImageOverride = {};
    m_liveBackground = {};
    m_singleScreenBackground = {};
    m_captureHistory.clear();
    m_captureHistoryBackgrounds.clear();
    m_captureHistoryGeometries.clear();
    m_captureHistoryBackgroundLoader = {};
    m_captureHistoryImageLoader = {};
    m_backgroundProvider = {};
    m_singleScreenBackgroundProvider = {};
    m_targetHierarchy.clear();
    m_scrollStitcher.clear();
    m_scrollScreen = nullptr;
    m_scrollGlobalSelection = {};
    m_scrollCaptureCount = 0;
    if (m_scrollPreview)
    {
        m_scrollPreview->hide();
        m_scrollPreview->deleteLater();
        m_scrollPreview = nullptr;
    }
}

void SnipOverlay::updateToolbarGeometry()
{
    if (!m_toolbar)
        return;
    if (m_ocrOverlay) {
        m_toolbar->hide();
        m_sizeToolbar->hide();
        m_ocrOverlay->raise();
        return;
    }
    updateSizeToolbar();
    if (m_toolbarHidden || m_dragging || m_transformingSelection || !m_selectionConfirmed ||
        !m_selection.isValid() || m_selection.isEmpty())
    {
        m_toolbar->hide();
        return;
    }

    m_toolbar->adjustSize();
    constexpr int margin = 6;
    const int tbWidth = m_toolbar->width();
    const int tbHeight = m_toolbar->height();

    int y = m_selection.bottom() + margin;
    bool insideSelection = false;
    if (y + tbHeight > height() - 8)
    {
        if (m_selection.height() >= tbHeight + margin * 2)
        {
            y = m_selection.bottom() - tbHeight - margin;
            insideSelection = true;
        }
        else if (m_selection.top() - tbHeight - margin >= 8)
        {
            y = m_selection.top() - tbHeight - margin;
        }
        else
        {
            y = m_selection.top() + margin;
            insideSelection = true;
        }
    }

    int x = m_selection.right() + 1 - tbWidth;
    if (insideSelection)
    {
        x = m_selection.right() - tbWidth - margin;
    }
    x = qBound(8, x, qMax(8, width() - tbWidth - 8));
    y = qBound(8, y, qMax(8, height() - tbHeight - 8));
    m_toolbar->move(x, y);
    m_toolbar->show();
    m_toolbar->raise();
}

QRect SnipOverlay::imageSelection() const
{
    if (m_selection.isEmpty() || width() <= 0 || height() <= 0)
        return {};

    const qreal scaleX = static_cast<qreal>(m_background.width()) / width();
    const qreal scaleY = static_cast<qreal>(m_background.height()) / height();
    // Map both edges as half-open coordinates.  Rounding each edge keeps the
    // requested logical size stable at fractional device-pixel ratios while
    // still following the exact position when a handle is dragged.  Using
    // ceil for the far edge made a 300x200 logical selection become 301x201
    // at 1.25x DPI.
    const int left = qRound(m_selection.left() * scaleX);
    const int top = qRound(m_selection.top() * scaleY);
    const int right = qRound((m_selection.left() + m_selection.width()) * scaleX);
    const int bottom = qRound((m_selection.top() + m_selection.height()) * scaleY);
    const QRect result(left, top, qMax(1, right - left), qMax(1, bottom - top));
    return result.intersected(m_background.rect());
}

QImage SnipOverlay::selectionImage() const
{
    if (m_selection.isEmpty())
        return {};
    if (m_canvas)
        m_canvas->finishTextInput();
    QImage result = m_canvas ? m_canvas->renderedImage() : m_background.copy(imageSelection());
    if (result.isNull())
        return result;

    if (m_roundedCorners)
    {
        QImage rounded(result.size(), QImage::Format_ARGB32_Premultiplied);
        rounded.fill(Qt::transparent);
        QPainter painter(&rounded);
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(QRectF(0, 0, rounded.width(), rounded.height()), m_captureCornerRadius,
                            m_captureCornerRadius);
        painter.setClipPath(path);
        painter.drawImage(0, 0, result);
        result = rounded;
    }

    if (m_outputBorder && m_outputBorderWidth > 0)
    {
        const int bw = m_outputBorderWidth;
        QImage bordered(result.width() + bw * 2, result.height() + bw * 2,
                        QImage::Format_ARGB32_Premultiplied);
        bordered.fill(Qt::transparent);
        QPainter p(&bordered);
        p.setRenderHint(QPainter::Antialiasing);
        p.drawImage(bw, bw, result);
        p.setPen(QPen(m_outputBorderColor, bw));
        p.setBrush(Qt::NoBrush);
        if (m_roundedCorners)
            p.drawRoundedRect(QRectF(bw / 2.0, bw / 2.0, bordered.width() - bw, bordered.height() - bw),
                              m_captureCornerRadius, m_captureCornerRadius);
        else
            p.drawRect(QRectF(bw / 2.0, bw / 2.0, bordered.width() - bw, bordered.height() - bw));
        result = bordered;
    }

    if (m_outputShadow && m_outputShadowBlur > 0)
    {
        const int pad = m_outputShadowBlur / 2;
        QImage shadowed(result.width() + pad * 2, result.height() + pad * 2,
                        QImage::Format_ARGB32_Premultiplied);
        shadowed.fill(Qt::transparent);
        QPainter p(&shadowed);
        p.setRenderHint(QPainter::Antialiasing);
        for (int i = pad; i >= 1; --i)
        {
            int alpha = (m_outputShadowColor.alpha() * (pad - i + 1)) / (pad * 3);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(m_outputShadowColor.red(), m_outputShadowColor.green(),
                              m_outputShadowColor.blue(), alpha));
            p.drawRoundedRect(QRectF(pad - i, pad - i + 2, result.width() + i * 2, result.height() + i * 2),
                              m_roundedCorners ? m_captureCornerRadius + i : i,
                              m_roundedCorners ? m_captureCornerRadius + i : i);
        }
        p.drawImage(pad, pad, result);
        result = shadowed;
    }

    return result;
}

QRect SnipOverlay::globalSelection() const
{
    return m_selection.translated(m_screenGeometry.topLeft());
}

void SnipOverlay::drawCapturedCursor(const QScreen *screen)
{
    if (!screen || m_background.isNull())
        return;

    QPixmap cursorPixmap = QCursor().pixmap();
    QPoint hotSpot = QCursor().hotSpot();
    if (cursorPixmap.isNull())
    {
        cursorPixmap = QPixmap(24, 30);
        cursorPixmap.fill(Qt::transparent);
        QPainter cursorPainter(&cursorPixmap);
        QPolygon arrow({QPoint(1, 1), QPoint(1, 25), QPoint(7, 19), QPoint(12, 29), QPoint(17, 26),
                        QPoint(11, 17), QPoint(21, 17)});
        cursorPainter.setPen(QPen(Qt::black, 2));
        cursorPainter.setBrush(Qt::white);
        cursorPainter.drawPolygon(arrow);
        hotSpot = QPoint(1, 1);
    }

    const QRect geometry = screen->geometry();
    const qreal scaleX = static_cast<qreal>(m_background.width()) / geometry.width();
    const qreal scaleY = static_cast<qreal>(m_background.height()) / geometry.height();
    const QPoint cursorPosition = QCursor::pos() - geometry.topLeft();
    const QPoint target(qRound(cursorPosition.x() * scaleX), qRound(cursorPosition.y() * scaleY));
    const QSize targetSize(qMax(1, qRound(cursorPixmap.width() * scaleX)),
                           qMax(1, qRound(cursorPixmap.height() * scaleY)));
    const QPixmap scaledCursor =
        cursorPixmap.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPainter painter(&m_background);
    painter.drawPixmap(target - QPoint(qRound(hotSpot.x() * scaleX), qRound(hotSpot.y() * scaleY)),
                       scaledCursor);
}

void SnipOverlay::copySelection(bool closeAfter, bool copyAsFileShortcut)
{
    if (!m_selectionConfirmed && !m_dragging && m_selection.isEmpty())
        m_selection = m_hoverSelection;
    const QImage image = selectionImage();
    if (image.isNull())
        return;

    emit selectionCaptured(globalSelection());
    emit screenshotCaptured(image);
    emit screenshotCapturedWithBackground(image, m_cleanBackground);
    m_outputEmitted = true;
    copyImageToClipboard(image, m_copyAsFileMode, copyAsFileShortcut);
    playCaptureSound();
    if (closeAfter)
    {
        close();
    }
}

void SnipOverlay::saveSelection(bool closeAfter)
{
    const QImage image = selectionImage();
    if (image.isNull())
        return;

    emit selectionCaptured(globalSelection());
    emit screenshotCaptured(image);
    emit screenshotCapturedWithBackground(image, m_cleanBackground);
    m_outputEmitted = true;
    QString defaultName = snipaste::core::expandOutputFilePattern(
        m_outputFileNamePattern.trimmed().isEmpty() ? QStringLiteral("Snipaste_$yyyy-MM-dd_HH-mm-ss$.png")
                                                    : m_outputFileNamePattern,
        QDateTime::currentDateTime());
    if (defaultName.trimmed().isEmpty())
        defaultName = QStringLiteral("snipaste.png");
    if (m_rememberExtension && !m_lastSavedExtension.isEmpty())
    {
        QFileInfo rememberedInfo(defaultName);
        const QString baseName = rememberedInfo.completeBaseName();
        if (!baseName.isEmpty())
            defaultName =
                rememberedInfo.path() == QStringLiteral(".")
                    ? baseName + QChar('.') + m_lastSavedExtension
                    : QDir(rememberedInfo.path()).filePath(baseName + QChar('.') + m_lastSavedExtension);
    }
    const bool hiddenForDialog = m_hideOnFileDialog && isVisible();
    if (hiddenForDialog)
        hide();
    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("保存截图"), defaultName,
        QStringLiteral("PNG 图片 (*.png);;JPEG 图片 (*.jpg *.jpeg);;所有文件 (*.*)"));
    if (path.isEmpty())
    {
        if (hiddenForDialog)
            show();
        return;
    }

    // QFileDialog does not append a suffix when the user types a bare name
    // and chooses the catch-all filter.  Keep the output deterministic and
    // remember the extension only when that preference is enabled.
    QFileInfo selectedInfo(path);
    QString extension = selectedInfo.suffix().trimmed().toLower();
    if (extension.isEmpty())
    {
        extension = m_rememberExtension && !m_lastSavedExtension.isEmpty() ? m_lastSavedExtension
                                                                           : QStringLiteral("png");
        path += QChar('.') + extension;
    }
    if (!image.save(path, nullptr, m_outputImageQuality))
    {
        if (hiddenForDialog)
            show();
        return;
    }
    if (m_rememberExtension)
    {
        m_lastSavedExtension = extension;
        emit fileSaved(path);
    }
    if (closeAfter)
        close();
    else if (hiddenForDialog)
        show();
}

void SnipOverlay::quickSaveSelection(bool closeAfter)
{
    const QImage image = selectionImage();
    if (image.isNull())
        return;
    const QString directory = m_quickSaveDirectory.trimmed().isEmpty()
                                  ? QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
                                  : m_quickSaveDirectory;
    if (!QDir().mkpath(directory))
        return;
    QString fileName = snipaste::core::expandOutputFilePattern(
        m_outputFileNamePattern.trimmed().isEmpty() ? QStringLiteral("Snipaste_$yyyy-MM-dd_HH-mm-ss$.png")
                                                    : m_outputFileNamePattern,
        QDateTime::currentDateTime());
    if (fileName.trimmed().isEmpty())
        fileName = QStringLiteral("snipaste.png");
    if (QFileInfo(fileName).suffix().trimmed().isEmpty())
        fileName += QStringLiteral(".png");
    const QString path =
        QFileInfo(fileName).isAbsolute() ? QDir::cleanPath(fileName) : QDir(directory).filePath(fileName);
    if (!image.save(path, nullptr, m_outputImageQuality))
        return;
    emit selectionCaptured(globalSelection());
    emit screenshotCaptured(image);
    emit screenshotCapturedWithBackground(image, m_cleanBackground);
    m_outputEmitted = true;
    if (m_captureSoundQuickSave)
        playCaptureSound();
    emit quickSaveCompleted(path);
    if (m_quickSaveNotification)
        QToolTip::showText(QCursor::pos(), QStringLiteral("已保存截图：%1").arg(path), nullptr, QRect(),
                           3000);
    if (closeAfter)
        close();
}

void SnipOverlay::printSelection(bool closeAfter)
{
    const QImage image = selectionImage();
    if (image.isNull())
        return;
    emit printRequested(image);
    emit selectionCaptured(globalSelection());
    emit screenshotCaptured(image);
    emit screenshotCapturedWithBackground(image, m_cleanBackground);
    m_outputEmitted = true;
    if (closeAfter)
        close();
}

bool SnipOverlay::pasteSelection(bool closeAfter)
{
    const QImage image = selectionImage();
    if (image.isNull())
        return false;

    emit selectionCaptured(globalSelection());
    emit screenshotCaptured(image);
    emit screenshotCapturedWithBackground(image, m_cleanBackground);
    m_outputEmitted = true;
    emit pasteRequested(image);
    if (closeAfter)
        close();
    return true;
}

void SnipOverlay::executeConfiguredAction(const QString &action, bool closeAfter)
{
    const QString normalized = action.trimmed();
    if (normalized.isEmpty() || normalized == QStringLiteral("无"))
        return;
    if (normalized == QStringLiteral("复制到剪贴板"))
        copySelection(closeAfter);
    else if (normalized == QStringLiteral("贴到屏幕"))
        pasteSelection(closeAfter);
    else if (normalized == QStringLiteral("保存文件"))
        saveSelection(closeAfter);
}

void SnipOverlay::annotateSelection()
{
    const QImage image = selectionImage();
    if (image.isNull())
        return;

    emit selectionCaptured(globalSelection());
    emit annotateRequested(image);
    close();
}

void SnipOverlay::barcodeSelection()
{
    const QImage image = selectionImage();
    if (image.isNull())
        return;

    emit selectionCaptured(globalSelection());
    emit screenshotCaptured(image);
    emit screenshotCapturedWithBackground(image, m_cleanBackground);
    m_outputEmitted = true;
    emit barcodeRequested(image);
    close();
}

} // namespace snipaste::ui
