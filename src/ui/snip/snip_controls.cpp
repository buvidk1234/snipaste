#include "ui/snip/snip_overlay.h"
#include "ui/clipboard_output.h"
#include "platform/screen_capture.h"
#include "ui/snip/capture_icons.h"
#include "ui/snip/capture_support.h"
#include "ui/annotation/annotation_canvas.h"
#include "platform/keyboard.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFormLayout>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QUuid>
#include <QWheelEvent>
#include <QWidgetAction>
#include <memory>

#ifdef Q_OS_WIN
#include <shobjidl_core.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.h>
#include <winrt/base.h>
#endif

namespace snipaste::ui {

#ifdef Q_OS_WIN
bool showNativeShare(QWidget *owner, const QImage &image)
{
    if (image.isNull() || !owner || QGuiApplication::platformName() == QStringLiteral("offscreen"))
        return false;
    const QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
        .filePath(QStringLiteral("snipaste-share-%1.png").arg(QUuid::createUuid().toString(QUuid::Id128)));
    if (!image.save(path, "PNG"))
        return false;

    using winrt::Windows::ApplicationModel::DataTransfer::DataTransferManager;
    using winrt::Windows::Storage::StorageFile;
    static DataTransferManager manager{nullptr};
    static winrt::event_token token{};
    try {
        if (manager) {
            manager.DataRequested(token);
            manager = nullptr;
        }
        auto factory = winrt::get_activation_factory<DataTransferManager, IDataTransferManagerInterop>();
        DataTransferManager next{nullptr};
        const HRESULT hr = factory->GetForWindow(reinterpret_cast<HWND>(owner->winId()),
                                                 winrt::guid_of<DataTransferManager>(),
                                                 winrt::put_abi(next));
        winrt::check_hresult(hr);
        manager = next;
        const auto sharedPath = std::make_shared<std::wstring>(path.toStdWString());
        token = manager.DataRequested([sharedPath](DataTransferManager const &,
                                                     winrt::Windows::ApplicationModel::DataTransfer::DataRequestedEventArgs const &args) {
            const auto file = StorageFile::GetFileFromPathAsync(*sharedPath).get();
            auto data = args.Request().Data();
            data.Properties().Title(L"snipaste 截图");
            auto items = winrt::single_threaded_vector<winrt::Windows::Storage::IStorageItem>();
            items.Append(file);
            data.SetStorageItems(items);
        });
        winrt::check_hresult(factory->ShowShareUIForWindow(reinterpret_cast<HWND>(owner->winId())));
        return true;
    } catch (const winrt::hresult_error &) {
        manager = nullptr;
        QFile::remove(path);
        return false;
    }
}
#endif

namespace {

QIcon makeColorSwatchIcon(const QColor &color)
{
    QPixmap pix(22, 22);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QColor(160, 160, 160));
    p.setBrush(color);
    p.drawRect(QRectF(1, 1, 20, 20));
    // Rainbow corner
    QPolygonF tri{QPointF(21, 13), QPointF(21, 21), QPointF(13, 21)};
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(237, 28, 36));
    p.drawPolygon(tri);
    QPolygonF tri2{QPointF(21, 16), QPointF(21, 21), QPointF(16, 21)};
    p.setBrush(QColor(255, 242, 0));
    p.drawPolygon(tri2);
    QPolygonF tri3{QPointF(21, 19), QPointF(21, 21), QPointF(19, 21)};
    p.setBrush(QColor(0, 162, 232));
    p.drawPolygon(tri3);
    p.end();
    return QIcon(pix);
}

} // namespace

void SnipOverlay::createSizeToolbar()
{
    m_sizeToolbar = new QFrame(this);
    m_sizeToolbar->setObjectName(QStringLiteral("captureSizeToolbar"));
    m_sizeToolbar->setCursor(Qt::ArrowCursor);
    m_sizeToolbar->setStyleSheet(QStringLiteral(
        "QFrame#captureSizeToolbar { background: rgba(0,0,0,195); border: 1px solid #66727f; }"
        "QToolButton { color: white; background: transparent; border: none; border-right: 1px solid #66727f; }"
        "QToolButton:hover { background: #365b87; } QToolButton:checked { background: #2889ed; }"));
    auto *row = new QHBoxLayout(m_sizeToolbar);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    m_sizeButton = new QToolButton(m_sizeToolbar);
    m_sizeButton->setObjectName(QStringLiteral("captureSize"));
    m_sizeButton->setFixedSize(130, 28);
    m_sizeButton->setCursor(Qt::PointingHandCursor);
    m_sizeButton->setAccessibleName(QStringLiteral("截图尺寸"));
    m_sizeButton->setFocusPolicy(Qt::NoFocus);
    QFont font = m_sizeButton->font(); font.setPixelSize(13); font.setBold(true); m_sizeButton->setFont(font);
    m_sizeButton->setToolTip(QStringLiteral("点击调整截图尺寸（px / dip）"));
    row->addWidget(m_sizeButton);
    connect(m_sizeButton, &QToolButton::clicked, this, &SnipOverlay::showSizeEditor);

    m_roundedButton = new QToolButton(m_sizeToolbar);
    m_roundedButton->setObjectName(QStringLiteral("圆角截图"));
    m_roundedButton->setToolTip(QStringLiteral("圆角截图（点击开启/关闭，滚轮调节半径）"));
    m_roundedButton->setIcon(captureIcon(CaptureIcon::StraightCorners, true));
    m_roundedButton->setIconSize(QSize(20, 20));
    m_roundedButton->setFixedSize(35, 28);
    m_roundedButton->setCheckable(true);
    m_roundedButton->setFocusPolicy(Qt::NoFocus);
    row->addWidget(m_roundedButton);
    connect(m_roundedButton, &QToolButton::clicked, this, [this] {
        m_roundedCorners = !m_roundedCorners;
        if (m_roundedCorners && m_captureCornerRadius <= 0)
            m_captureCornerRadius = 8;
        m_roundedButton->setChecked(m_roundedCorners);
        updateSizeToolbar();
        update();
    });

    m_radiusStepperWidget = new QWidget(m_sizeToolbar);
    m_radiusStepperWidget->setFixedSize(16, 28);
    m_radiusStepperWidget->setStyleSheet(QStringLiteral(
        "QWidget { border-right: 1px solid #66727f; }"
        "QToolButton { border: none; border-right: none; color: white; font-size: 8px; }"
        "QToolButton:hover { background: #365b87; }"));
    auto *stepperLayout = new QVBoxLayout(m_radiusStepperWidget);
    stepperLayout->setContentsMargins(0, 0, 0, 0);
    stepperLayout->setSpacing(0);
    auto *upBtn = new QToolButton(m_radiusStepperWidget);
    upBtn->setText(QStringLiteral("▲"));
    upBtn->setFixedSize(15, 14);
    upBtn->setFocusPolicy(Qt::NoFocus);
    connect(upBtn, &QToolButton::clicked, this, [this] {
        m_captureCornerRadius = qMin(99, m_captureCornerRadius + 1);
        m_roundedCorners = true;
        updateSizeToolbar();
        update();
    });
    auto *downBtn = new QToolButton(m_radiusStepperWidget);
    downBtn->setText(QStringLiteral("▼"));
    downBtn->setFixedSize(15, 14);
    downBtn->setFocusPolicy(Qt::NoFocus);
    connect(downBtn, &QToolButton::clicked, this, [this] {
        m_captureCornerRadius = qMax(0, m_captureCornerRadius - 1);
        if (m_captureCornerRadius == 0)
            m_roundedCorners = false;
        updateSizeToolbar();
        update();
    });
    stepperLayout->addWidget(upBtn);
    stepperLayout->addWidget(downBtn);
    row->addWidget(m_radiusStepperWidget);
    m_radiusStepperWidget->hide();

    const auto add = [this, row](CaptureIcon icon, const QString &name, const QString &tip) {
        auto *button = new QToolButton(m_sizeToolbar);
        button->setObjectName(name); button->setToolTip(tip);
        button->setIcon(captureIcon(icon, true)); button->setIconSize(QSize(20, 20));
        button->setFixedSize(35, 28); button->setFocusPolicy(Qt::NoFocus);
        row->addWidget(button); return button;
    };
    connect(add(CaptureIcon::SelectScreen, QStringLiteral("选择屏幕"),
               QStringLiteral("选择当前屏幕 (%1)").arg(snipaste::platform::primaryShortcutText(QStringLiteral("A")))),
            &QToolButton::clicked, this, &SnipOverlay::selectCurrentScreen);
    connect(add(CaptureIcon::Refresh, QStringLiteral("刷新截图"), QStringLiteral("刷新截图 (F5)")),
            &QToolButton::clicked, this, &SnipOverlay::refreshBackground);
    m_sizeToolbar->hide();
}

void SnipOverlay::updateSizeToolbar()
{
    if (!m_sizeToolbar)
        return;
    if (!m_selectionConfirmed || m_dragging || m_transformingSelection || m_selection.isEmpty()) {
        m_sizeToolbar->hide();
        return;
    }
    const QSize pixels = imageSelection().size();
    m_sizeButton->setText(QStringLiteral("%1 × %2 | px").arg(pixels.width()).arg(pixels.height()));
    if (m_roundedCorners) {
        m_roundedButton->setText(QStringLiteral("╭  |  %1").arg(m_captureCornerRadius));
        m_roundedButton->setIcon(QIcon());
        m_roundedButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
        m_roundedButton->setFixedWidth(56);
        m_roundedButton->setChecked(true);
        m_radiusStepperWidget->show();
    } else {
        m_roundedButton->setIcon(captureIcon(CaptureIcon::StraightCorners, true));
        m_roundedButton->setText(QString());
        m_roundedButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
        m_roundedButton->setFixedWidth(35);
        m_roundedButton->setChecked(false);
        m_radiusStepperWidget->hide();
    }
    m_sizeToolbar->adjustSize();
    const int x = qBound(0, m_selection.left(), qMax(0, width() - m_sizeToolbar->width()));
    const int above = m_selection.top() - m_sizeToolbar->height() - 5;
    const int y = above >= 0 ? above : qMin(height() - m_sizeToolbar->height(), m_selection.bottom() + 5);
    m_sizeToolbar->move(x, qMax(0, y));
    m_sizeToolbar->show(); m_sizeToolbar->raise();
}

void SnipOverlay::showSizeEditor()
{
    if (!m_selectionConfirmed) return;
    auto *popup = new QFrame(this, Qt::Popup | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    popup->setAttribute(Qt::WA_DeleteOnClose);
    popup->setObjectName(QStringLiteral("captureSizeEditor"));
    auto *form = new QFormLayout(popup);
    auto *widthEdit = new QSpinBox(popup);
    auto *heightEdit = new QSpinBox(popup);
    widthEdit->setObjectName(QStringLiteral("captureWidth"));
    heightEdit->setObjectName(QStringLiteral("captureHeight"));
    auto *unit = new QComboBox(popup);
    unit->setObjectName(QStringLiteral("captureSizeUnit"));
    unit->addItems({QStringLiteral("px"), QStringLiteral("dip")});
    const auto populate = [this, widthEdit, heightEdit, unit] {
        const QSize maximum = unit->currentIndex() == 0 ? m_background.size() : size();
        const QSize current = unit->currentIndex() == 0 ? imageSelection().size() : m_selection.size();
        widthEdit->setRange(2, maximum.width()); heightEdit->setRange(2, maximum.height());
        widthEdit->setValue(current.width()); heightEdit->setValue(current.height());
    };
    populate();
    connect(unit, &QComboBox::currentIndexChanged, popup, populate);
    form->addRow(QStringLiteral("宽度"), widthEdit); form->addRow(QStringLiteral("高度"), heightEdit);
    form->addRow(QStringLiteral("单位"), unit);
    auto *apply = new QPushButton(QStringLiteral("应用"), popup);
    apply->setObjectName(QStringLiteral("applyCaptureSize"));
    form->addRow(apply);
    connect(apply, &QPushButton::clicked, this, [this, popup, widthEdit, heightEdit, unit] {
        const qreal sx = unit->currentIndex() == 0 ? qreal(m_background.width()) / width() : 1;
        const qreal sy = unit->currentIndex() == 0 ? qreal(m_background.height()) / height() : 1;
        const QSize requested(qMax(2, qRound(widthEdit->value() / sx)), qMax(2, qRound(heightEdit->value() / sy)));
        QRect next(m_selection.topLeft(), requested.boundedTo(size()));
        next.moveLeft(qMin(next.left(), width() - next.width()));
        next.moveTop(qMin(next.top(), height() - next.height()));
        changeSelection(next, false); updateToolbarGeometry();
        popup->close(); setFocus();
    });
    popup->adjustSize();
    QPoint position = m_sizeToolbar->mapToGlobal(QPoint(0, m_sizeToolbar->height()));
    position.setX(qBound(m_screenGeometry.left(), position.x(), m_screenGeometry.right() - popup->width() + 1));
    position.setY(qBound(m_screenGeometry.top(), position.y(), m_screenGeometry.bottom() - popup->height() + 1));
    popup->move(position); popup->show(); widthEdit->setFocus(); widthEdit->selectAll();
}

void SnipOverlay::selectCurrentScreen()
{
    if (m_virtualDesktopMode) {
        QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        if (!screen)
            return;

        // Hide the overlay before grabbing the screen so the refreshed frame
        // never contains its own dimming layer or selection handles.
        hide();
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        QImage image = platform::captureScreen(screen);
        image.setDevicePixelRatio(1);
        show();

        resetSelection();
        m_virtualDesktopMode = false;
        m_screenGeometry = screen->geometry();
        m_singleScreenGeometry = m_screenGeometry;
        m_singleScreenBackground = image;
        m_singleScreenBackgroundProvider = [screen = QPointer<QScreen>(screen)] {
            return screen ? platform::captureScreen(screen) : QImage();
        };
        m_backgroundProvider = m_singleScreenBackgroundProvider;
        m_background = image;
        m_cleanBackground = image;
        setGeometry(m_screenGeometry);
        m_cursorPosition = QCursor::pos() - m_screenGeometry.topLeft();
        m_selection = rect();
        confirmSelection();
    } else if (m_canvas) {
        changeSelection(rect(), false);
    } else {
        m_selection = rect();
        confirmSelection();
    }
    updateToolbarGeometry(); update();
}

QImage SnipOverlay::captureVirtualDesktop() const
{
    const QList<QScreen *> screens = QGuiApplication::screens();
    if (screens.isEmpty())
        return {};
    const QRect virtualGeometry = screens.first()->virtualGeometry();
    if (virtualGeometry.isEmpty())
        return {};

    QImage result(virtualGeometry.size(), QImage::Format_RGB32);
    result.fill(Qt::black);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    for (QScreen *screen : screens) {
        if (!screen)
            continue;
        QImage image = platform::captureScreen(screen);
        image.setDevicePixelRatio(1);
        const QRect target = screen->geometry().translated(-virtualGeometry.topLeft());
        if (!image.isNull() && !target.isEmpty())
            painter.drawImage(target, image);
    }
    return result;
}

void SnipOverlay::selectVirtualDesktop()
{
    if (m_virtualDesktopMode || QGuiApplication::screens().size() < 2)
        return;

    // Preserve the current screen frame/provider so the next Ctrl+A can
    // switch back without losing the original capture source.
    m_singleScreenGeometry = m_screenGeometry;
    m_singleScreenBackground = m_cleanBackground;
    m_singleScreenBackgroundProvider = m_backgroundProvider;

    hide();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    const QImage image = captureVirtualDesktop();
    show();
    if (image.isNull())
        return;

    resetSelection();
    m_virtualDesktopMode = true;
    m_screenGeometry = QGuiApplication::screens().first()->virtualGeometry();
    m_background = image;
    m_cleanBackground = image;
    m_backgroundProvider = [this] { return captureVirtualDesktop(); };
    setGeometry(m_screenGeometry);
    m_cursorPosition = QCursor::pos() - m_screenGeometry.topLeft();
    m_selection = rect();
    confirmSelection();
    updateToolbarGeometry();
    update();
}

void SnipOverlay::resizeFromCenter(int delta)
{
    if (!m_selectionConfirmed) return;
    QRect next = m_selection.adjusted(-delta, -delta, delta, delta).intersected(rect());
    if (next.width() >= 2 && next.height() >= 2) changeSelection(next, false);
    updateToolbarGeometry();
}

void SnipOverlay::setBackgroundProvider(std::function<QImage()> provider)
{
    m_backgroundProvider = std::move(provider);
    if (!m_virtualDesktopMode)
        m_singleScreenBackgroundProvider = m_backgroundProvider;
}

void SnipOverlay::refreshBackground()
{
    if (!m_backgroundProvider || m_refreshing) return;
    m_refreshing = true;
    if (m_canvas) m_canvas->finishTextInput();
    hide(); // Let the compositor remove the overlay before taking a new frame.
    const auto generation = m_sessionGeneration;
    QTimer::singleShot(60, this, [this, generation] {
        if (generation != m_sessionGeneration || !m_captureActive || !m_backgroundProvider)
            return;
        const QImage image = m_backgroundProvider();
        if (!image.isNull()) {
            m_cleanBackground = image; m_cleanBackground.setDevicePixelRatio(1);
            m_background = m_cleanBackground;
            m_historyIndex = -1;
            if (m_captureCursor) drawCapturedCursor(QGuiApplication::screenAt(m_screenGeometry.center()));
            if (m_canvas) {
                m_canvas->setCaptureImage(m_background, {}, QSizeF(size()));
                m_canvas->setCanvasFrame(m_selection.topLeft(), QSizeF(size()), m_selection);
            }
        }
        m_refreshing = false; show(); updateToolbarGeometry(); update();
    });
}

void SnipOverlay::toggleCapturedCursor()
{
    m_captureCursor = !m_captureCursor;
    m_background = m_cleanBackground;
    if (m_captureCursor) drawCapturedCursor(QGuiApplication::screenAt(m_screenGeometry.center()));
    if (m_canvas) {
        m_canvas->setCaptureImage(m_background, {}, QSizeF(size()));
        m_canvas->setCanvasFrame(m_selection.topLeft(), QSizeF(size()), m_selection);
    }
    update();
}

void SnipOverlay::setCaptureHistory(const QList<QImage> &images,
                                    const QList<core::CaptureGeometry> &geometries,
                                    const QList<QImage> &backgrounds,
                                    std::function<QImage(int)> backgroundLoader,
                                    std::function<QImage(int)> imageLoader)
{
    m_captureHistory = images;
    m_captureHistoryGeometries = geometries;
    m_captureHistoryBackgrounds = backgrounds;
    m_captureHistoryBackgroundLoader = std::move(backgroundLoader);
    m_captureHistoryImageLoader = std::move(imageLoader);
}

void SnipOverlay::navigateHistory(int direction)
{
    if (m_captureHistory.isEmpty())
        return;
    int next = m_historyIndex + direction;
    if (m_historyCycle) {
        if (next < -1)
            next = int(m_captureHistory.size()) - 1;
        else if (next >= m_captureHistory.size())
            next = -1;
    } else {
        next = qBound(-1, next, int(m_captureHistory.size()) - 1);
    }
    if (next == m_historyIndex) return;
    if (m_historyIndex == -1) { m_liveBackground = m_cleanBackground; m_liveSelection = m_selection; }
    m_historyIndex = next;
    resetSelection();
    m_background = m_liveBackground;
    if (next == -1) {
        m_selection = m_liveSelection;
        m_cleanBackground = m_background;
    } else {
        QImage image = m_captureHistory.at(next);
        if (image.isNull() && m_captureHistoryImageLoader)
            image = m_captureHistoryImageLoader(next);
        if (image.isNull()) {
            m_historyIndex = -1;
            m_selectionImageOverride = {};
            m_background = m_liveBackground;
            m_cleanBackground = m_background;
            m_selection = m_liveSelection;
            if (!m_selection.isEmpty())
                confirmSelection();
            else
                updateHoverTarget();
            updateToolbarGeometry();
            update();
            return;
        }
        image.setDevicePixelRatio(1);
        m_selectionImageOverride = image;
        const core::CaptureGeometry geometry = next < m_captureHistoryGeometries.size()
            ? m_captureHistoryGeometries.at(next) : core::CaptureGeometry{};
        QImage historicalBackground;
        // Old records have no selection coordinates. Do not guess using the
        // live selection or recent regions, or decode a desktop we cannot align.
        if (geometry.isValid()) {
            if (next < m_captureHistoryBackgrounds.size())
                historicalBackground = m_captureHistoryBackgrounds.at(next);
            if (historicalBackground.isNull() && m_captureHistoryBackgroundLoader)
                historicalBackground = m_captureHistoryBackgroundLoader(next);
        }
        const bool hasHistoricalBackground = !historicalBackground.isNull();
        if (hasHistoricalBackground) {
            historicalBackground.setDevicePixelRatio(1);
            // Display the original frame even if resolution/DPI has changed.
            // Both the frame and its selection map to the current viewport.
            m_background = std::move(historicalBackground);
        } else {
            m_background = QImage(m_liveBackground.size(), QImage::Format_RGB32);
            m_background.fill(QColor(32, 32, 32));
        }
        m_cleanBackground = m_background;
        QRect restored;
        if (geometry.isValid()) {
            const qreal sx = qreal(width()) / geometry.desktopSize.width();
            const qreal sy = qreal(height()) / geometry.desktopSize.height();
            const int left = qRound(geometry.region.x() * sx);
            const int top = qRound(geometry.region.y() * sy);
            const int right = qRound((geometry.region.x() + geometry.region.width()) * sx);
            const int bottom = qRound((geometry.region.y() + geometry.region.height()) * sy);
            restored = QRect(left, top, qMax(1, right - left), qMax(1, bottom - top));
        }
        if (restored.isEmpty()) {
            const qreal sx = qreal(m_background.width()) / qMax(1, width());
            const qreal sy = qreal(m_background.height()) / qMax(1, height());
            QSize extent(qMax(2, qRound(image.width() / qMax<qreal>(0.01, sx))),
                         qMax(2, qRound(image.height() / qMax<qreal>(0.01, sy))));
            if (extent.width() > width() || extent.height() > height())
                extent.scale(size(), Qt::KeepAspectRatio);
            restored = QRect((width() - extent.width()) / 2,
                             (height() - extent.height()) / 2,
                             extent.width(), extent.height());
        }
        m_selection = restored;
        QPainter painter(&m_background);
        // Composite in the original desktop's pixel coordinates. Mapping the
        // rounded display selection back to pixels can rescale the saved crop.
        const QRect target = hasHistoricalBackground
            ? platform::screenCapturePixelRect(QRect(QPoint(), geometry.desktopSize),
                                               m_background.size(), geometry.region)
            : imageSelection();
        painter.drawImage(target, image);
        // The canvas and the surrounding desktop must share one transform,
        // including when this historical selection is moved or resized.
        if (hasHistoricalBackground)
            m_selectionImageOverride = {};
    }
    if (!m_selection.isEmpty()) confirmSelection();
    else updateHoverTarget();
    updateToolbarGeometry(); update();
}

bool SnipOverlay::handleProShortcut(QKeyEvent *event)
{
    const int key = event->key();
    const auto modifiers = event->modifiers();
    if (m_selectionConfirmed && m_toolbar &&
        ((modifiers == snipaste::platform::primaryModifier() && key >= Qt::Key_1 && key <= Qt::Key_9) ||
         (modifiers == Qt::NoModifier && (key == Qt::Key_B || key == Qt::Key_T || key == Qt::Key_N || key == Qt::Key_E))))
    {
        for (auto *button : m_toolbar->findChildren<QToolButton *>())
        {
            if (!button->property("annotationTool").isValid())
                continue;
            if ((modifiers == snipaste::platform::primaryModifier() &&
                 button->property("pageIndex").toInt() + 1 == key - Qt::Key_0) ||
                (modifiers == Qt::NoModifier && !button->property("toolKey").toString().isEmpty() &&
                 button->property("toolKey").toString() == QKeySequence(key).toString()))
            {
                m_toolbarHidden = false;
                if (!button->isChecked()) button->click();
                updateToolbarGeometry();
                return true;
            }
        }
    }
    if (key == Qt::Key_F5) { refreshBackground(); return true; }
    if (key == Qt::Key_QuoteLeft || key == Qt::Key_Exclam) { toggleCapturedCursor(); return true; }
    if (key == Qt::Key_Comma || key == Qt::Key_Period) { navigateHistory(key == Qt::Key_Comma ? 1 : -1); return true; }
    if (key == Qt::Key_Space && m_selectionConfirmed) {
        m_toolbarHidden = !m_toolbarHidden; updateToolbarGeometry(); return true;
    }
    if (key == Qt::Key_R && (modifiers == Qt::NoModifier || modifiers == Qt::ShiftModifier)
        && !m_regionHistory.isEmpty()) {
        const int count = int(m_regionHistory.size());
        const int direction = modifiers == Qt::ShiftModifier ? -1 : 1;
        m_regionHistoryIndex = m_regionHistoryIndex < 0 ? (direction > 0 ? 0 : count - 1)
            : (m_regionHistoryIndex + direction + count) % count;
        resetSelection(); m_selection = m_regionHistory.at(m_regionHistoryIndex); confirmSelection(); return true;
    }
    if (key == Qt::Key_A && modifiers == snipaste::platform::primaryModifier()) {
        // Keep the repeated shortcut cycle identical to the normal key path:
        // current screen -> all screens -> current screen.  In particular,
        // the first press from an unconfirmed overlay must not jump directly
        // to the virtual desktop frame.
        if (QGuiApplication::screens().size() <= 1 || !m_selectionConfirmed)
            selectCurrentScreen();
        else if (m_virtualDesktopMode)
            selectCurrentScreen();
        else
            selectVirtualDesktop();
        return true;
    }
    if (key == Qt::Key_Tab && m_canvas
        && (m_canvas->tool() == AnnotationCanvas::Tool::Line || m_canvas->tool() == AnnotationCanvas::Tool::Arrow)
        && !m_canvas->testAttribute(Qt::WA_TransparentForMouseEvents)) {
        if (auto *toggle = m_toolOptions->findChild<QToolButton *>(QStringLiteral("annotationToggleArrow")))
            toggle->click();
        return true;
    }
    if ((key == Qt::Key_1 || key == Qt::Key_2
         || key == Qt::Key_BracketLeft || key == Qt::Key_BracketRight)
        && modifiers == Qt::NoModifier) {
        const int direction = (key == Qt::Key_1 || key == Qt::Key_BracketRight) ? 1 : -1;
        if (!m_selectionConfirmed) cycleTarget(direction);
        else if (m_canvas && !m_canvas->testAttribute(Qt::WA_TransparentForMouseEvents))
            m_canvas->adjustToolSize(direction);
        else resizeFromCenter(direction);
        return true;
    }
    // Arrow keys move the confirmed selection by default.  The optional
    // exchange setting swaps that role with WASD; the cursor movement side
    // of the swap is handled by SnipOverlay::keyPressEvent while selecting.
    QPoint direction;
    const bool useWasdForSelection = m_exchangeNavigationKeys;
    if (key == Qt::Key_Left || (useWasdForSelection && key == Qt::Key_A)) direction.setX(-1);
    if (key == Qt::Key_Right || (useWasdForSelection && key == Qt::Key_D)) direction.setX(1);
    if (key == Qt::Key_Up || (useWasdForSelection && key == Qt::Key_W)) direction.setY(-1);
    if (key == Qt::Key_Down || (useWasdForSelection && key == Qt::Key_S)) direction.setY(1);
    if (m_exchangeNavigationKeys && !m_selectionConfirmed
        && (key == Qt::Key_Left || key == Qt::Key_Right
            || key == Qt::Key_Up || key == Qt::Key_Down)) {
        // Let keyPressEvent move the cursor when the selection is not fixed.
        return false;
    }
    if (!direction.isNull() && m_selectionConfirmed) {
        QRect next = m_selection;
        if (modifiers == snipaste::platform::primaryModifier() || modifiers == Qt::ShiftModifier) {
            const int step = modifiers == snipaste::platform::primaryModifier() ? 1 : -1;
            if (direction.x() < 0) next.adjust(-step, 0, 0, 0);
            if (direction.x() > 0) next.adjust(0, 0, step, 0);
            if (direction.y() < 0) next.adjust(0, -step, 0, 0);
            if (direction.y() > 0) next.adjust(0, 0, 0, step);
            changeSelection(next, false);
        } else {
            next.translate(direction);
            next.moveLeft(qBound(0, next.left(), width() - next.width()));
            next.moveTop(qBound(0, next.top(), height() - next.height()));
            changeSelection(next, true);
        }
        updateToolbarGeometry(); return true;
    }
    return false;
}

QImage SnipOverlay::pasteSourceImage() const
{
    return selectionImage(false);
}

core::AnnotationDocument SnipOverlay::pasteAnnotations() const
{
    if (!m_canvas)
        return {};
    m_canvas->finishTextInput();
    auto document = m_canvas->selectionDocument();
    const int padding = (m_outputBorder ? m_outputBorderWidth : 0) +
                        (m_outputShadow ? m_outputShadowBlur / 2 : 0);
    document.translate(QPointF(padding, padding));
    return document;
}

void SnipOverlay::wheelEvent(QWheelEvent *event)
{
    if (!m_wheelEnabled) {
        event->ignore();
        return;
    }
    const int direction = event->angleDelta().y() > 0 ? 1 : -1;
    if (!m_selectionConfirmed) cycleTarget(direction);
    else if (m_canvas && !m_canvas->testAttribute(Qt::WA_TransparentForMouseEvents))
        m_canvas->adjustToolSize(direction);
    else resizeFromCenter(direction);
    event->accept();
}

void SnipOverlay::showOutputMenu(const QPoint &position, bool shareOnly)
{
    auto *menu = new QMenu(this);
    menu->setObjectName(shareOnly ? QStringLiteral("captureShareMenu") : QStringLiteral("captureOutputMenu"));
    menu->setAttribute(Qt::WA_DeleteOnClose);

    if (shareOnly) {
        menu->addAction(QStringLiteral("复制图像"), this, [this] { copySelection(); })->setShortcut(QKeySequence::Copy);
        auto *copyFile = menu->addAction(QStringLiteral("复制图像文件"), this, [this] {
            const QImage image = selectionImage();
            if (image.isNull()) return;
            if (!copyImageToClipboard(image, CopyAsFileMode::Always))
                return;
            emit selectionCaptured(globalSelection());
            emit screenshotCaptured(image);
            emit screenshotCapturedWithBackground(image, m_cleanBackground);
            close();
        });
        if (m_copyAsFileMode != static_cast<int>(CopyAsFileMode::Disabled))
            copyFile->setShortcut(QKeySequence(snipaste::platform::primaryModifier() | Qt::AltModifier | Qt::Key_S));
        menu->addAction(QStringLiteral("保存图像…"), this, [this] { saveSelection(); })->setShortcut(QKeySequence::Save);
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() != QStringLiteral("offscreen")) {
            menu->addAction(QStringLiteral("系统分享"), this, [this] {
                const QImage image = selectionImage();
                if (showNativeShare(this, image))
                    return;
                copyImageToClipboard(image, CopyAsFileMode::Always);
            });
        }
#endif
        menu->popup(position);
        return;
    }

    auto *widgetAction = new QWidgetAction(menu);
    auto *panel = new QFrame(menu);
    panel->setObjectName(QStringLiteral("outputOptionsPanel"));
    panel->setStyleSheet(QStringLiteral(
        "QFrame#outputOptionsPanel { background: #ffffff; border-radius: 6px; }"
        "QLabel { color: #111111; font-size: 13px; font-weight: normal; }"
        "QCheckBox { color: #111111; font-size: 13px; spacing: 5px; }"
        "QSpinBox { border: 1px solid #c4c9cf; border-radius: 2px; padding: 2px 4px; background: white; font-size: 12px; }"));
    auto *vbox = new QVBoxLayout(panel);
    vbox->setContentsMargins(12, 10, 12, 10);
    vbox->setSpacing(8);

    auto *title = new QLabel(QStringLiteral("输出选项:"), panel);
    vbox->addWidget(title);

    // Row 1: 阴影
    auto *rowShadow = new QHBoxLayout();
    rowShadow->setSpacing(8);
    auto *chkShadow = new QCheckBox(QStringLiteral("阴影:"), panel);
    chkShadow->setChecked(m_outputShadow);
    auto *spinShadow = new QSpinBox(panel);
    spinShadow->setRange(0, 100);
    spinShadow->setValue(m_outputShadowBlur);
    spinShadow->setFixedWidth(52);
    auto *btnShadowColor = new QToolButton(panel);
    btnShadowColor->setFixedSize(22, 22);
    btnShadowColor->setIcon(makeColorSwatchIcon(m_outputShadowColor));
    rowShadow->addWidget(chkShadow);
    rowShadow->addWidget(spinShadow);
    rowShadow->addWidget(btnShadowColor);
    vbox->addLayout(rowShadow);

    connect(chkShadow, &QCheckBox::toggled, this, [this](bool c) { m_outputShadow = c; });
    connect(spinShadow, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) { m_outputShadowBlur = v; });
    connect(btnShadowColor, &QToolButton::clicked, this, [this, btnShadowColor] {
        const QColor c = QColorDialog::getColor(m_outputShadowColor, this, QStringLiteral("阴影颜色"), QColorDialog::ShowAlphaChannel);
        if (c.isValid()) {
            m_outputShadowColor = c;
            btnShadowColor->setIcon(makeColorSwatchIcon(c));
        }
    });

    // Row 2: 边框
    auto *rowBorder = new QHBoxLayout();
    rowBorder->setSpacing(8);
    auto *chkBorder = new QCheckBox(QStringLiteral("边框:"), panel);
    chkBorder->setChecked(m_outputBorder);
    auto *spinBorder = new QSpinBox(panel);
    spinBorder->setRange(0, 50);
    spinBorder->setValue(m_outputBorderWidth);
    spinBorder->setFixedWidth(52);
    auto *btnBorderColor = new QToolButton(panel);
    btnBorderColor->setFixedSize(22, 22);
    btnBorderColor->setIcon(makeColorSwatchIcon(m_outputBorderColor));
    rowBorder->addWidget(chkBorder);
    rowBorder->addWidget(spinBorder);
    rowBorder->addWidget(btnBorderColor);
    vbox->addLayout(rowBorder);

    connect(chkBorder, &QCheckBox::toggled, this, [this](bool c) { m_outputBorder = c; });
    connect(spinBorder, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) { m_outputBorderWidth = v; });
    connect(btnBorderColor, &QToolButton::clicked, this, [this, btnBorderColor] {
        const QColor c = QColorDialog::getColor(m_outputBorderColor, this, QStringLiteral("边框颜色"), QColorDialog::ShowAlphaChannel);
        if (c.isValid()) {
            m_outputBorderColor = c;
            btnBorderColor->setIcon(makeColorSwatchIcon(c));
        }
    });

    widgetAction->setDefaultWidget(panel);
    menu->addAction(widgetAction);

    // Standard actions preserved for regression test compatibility
    auto *a1 = menu->addAction(QStringLiteral("复制图像"), this, [this] { copySelection(); });
    auto *a2 = menu->addAction(QStringLiteral("复制图像文件"), this, [this] {
        const QImage image = selectionImage();
        if (image.isNull()) return;
        if (!copyImageToClipboard(image, CopyAsFileMode::Always))
            return;
        emit selectionCaptured(globalSelection());
        emit screenshotCaptured(image);
        emit screenshotCapturedWithBackground(image, m_cleanBackground);
        close();
    });
    auto *a3 = menu->addAction(QStringLiteral("保存图像…"), this, [this] { saveSelection(); });
    auto *a4 = menu->addAction(QStringLiteral("快捷保存"), this, [this] { quickSaveSelection(); });
    auto *a5 = menu->addAction(QStringLiteral("打印"), this, [this] { printSelection(); });
    auto *a6 = menu->addAction(QStringLiteral("贴到屏幕"), this, [this] { pasteSelection(); });
    auto *a7 = menu->addAction(QStringLiteral("输入截屏尺寸…"), this, &SnipOverlay::showSizeEditor);
    auto *a8 = menu->addAction(QStringLiteral("刷新截图"), this, &SnipOverlay::refreshBackground);
    auto *a9 = menu->addAction(QStringLiteral("二维码 / 条形码"), this, &SnipOverlay::barcodeSelection);
    auto *hints = menu->addAction(QStringLiteral("快捷键提示"));
    a1->setShortcut(QKeySequence::Copy);
    if (m_copyAsFileMode != static_cast<int>(CopyAsFileMode::Disabled))
        a2->setShortcut(QKeySequence(snipaste::platform::primaryModifier() | Qt::AltModifier | Qt::Key_S));
    a3->setShortcut(QKeySequence::Save);
    a4->setShortcut(QKeySequence(snipaste::platform::primaryModifier() | Qt::ShiftModifier | Qt::Key_S));
    a5->setShortcut(QKeySequence::Print);
    a6->setShortcut(QKeySequence(snipaste::platform::primaryModifier() | Qt::Key_T));
    a8->setShortcut(QKeySequence(Qt::Key_F5));
    hints->setCheckable(true); hints->setChecked(m_showShortcutHints);
    connect(hints, &QAction::toggled, this, [this](bool enabled) { m_showShortcutHints = enabled; update(); });

    for (auto *a : {a1, a2, a3, a4, a5, a6, a7, a8, a9, hints})
        a->setVisible(false);

    menu->popup(position);
}

} // namespace snipaste::ui
