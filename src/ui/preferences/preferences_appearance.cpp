#include "ui/preferences/preferences_dialog.h"
#include "ui/preferences/appearance_settings.h"
#include "core/config/config_store.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QWheelEvent>

namespace snipaste::ui {
namespace {

QLabel *infoLabel(QWidget *parent, const QString &tip = {})
{
    auto *label = new QLabel(QStringLiteral("i"), parent);
    label->setAlignment(Qt::AlignCenter);
    label->setFixedSize(16, 16);
    label->setStyleSheet(QStringLiteral(
        "QLabel { color: #555555; border: 1px solid #777777; border-radius: 8px; "
        "font-size: 11px; font-weight: 600; }"));
    if (!tip.isEmpty())
        label->setToolTip(tip);
    return label;
}

void colorPreview(QPushButton *button, QColor color)
{
    button->setProperty("color", color.name(QColor::HexArgb));
    QPixmap swatch(20, 20); swatch.fill(color);
    button->setIcon(QIcon(swatch));
}

QPixmap palettePreviewPixmap(const QColor &color, int extent)
{
    QPixmap pixmap(extent, extent);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(QRectF(0, 0, extent, extent), color);

    // Snipaste marks the editable color swatch with a small rainbow corner.
    // It remains visible for opaque colors and does not turn the preview into
    // a transparent checkerboard.
    const qreal split = extent * 0.66;
    QPainterPath triangle;
    triangle.moveTo(split, extent);
    triangle.lineTo(extent, split);
    triangle.lineTo(extent, extent);
    triangle.closeSubpath();
    QLinearGradient gradient(QPointF(split, extent), QPointF(extent, split));
    gradient.setColorAt(0.00, QColor("#ef2b2d"));
    gradient.setColorAt(0.18, QColor("#ff8a22"));
    gradient.setColorAt(0.36, QColor("#ffe500"));
    gradient.setColorAt(0.54, QColor("#31c75b"));
    gradient.setColorAt(0.72, QColor("#24a9e8"));
    gradient.setColorAt(0.86, QColor("#4f58d8"));
    gradient.setColorAt(1.00, QColor("#a84ca8"));
    painter.save();
    painter.setClipPath(triangle);
    painter.fillRect(QRectF(split, split, extent - split, extent - split), gradient);
    painter.restore();
    painter.setPen(QPen(Qt::white, 1.0));
    painter.drawLine(QPointF(split, extent), QPointF(extent, split));
    return pixmap;
}

// The palette keeps its data on the widget so rebuild callbacks never refer to stack locals.
class PaletteEditor final : public QWidget
{
public:
    PaletteEditor(qreal scale, QWidget *parent) : QWidget(parent), m_scale(scale)
    {
        setObjectName(QStringLiteral("annotationPalette"));
        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(0, 0, 0, 0); row->setSpacing(u(4));
        m_preview = new QPushButton(this);
        m_preview->setObjectName(QStringLiteral("paletteCurrentColor"));
        m_preview->setFixedSize(u(56), u(56));
        m_preview->setIconSize(QSize(u(54), u(54)));
        m_preview->setStyleSheet(QStringLiteral("QPushButton { border: 1px solid #777777; padding: 0; }") );
        m_preview->setToolTip(QStringLiteral("选择颜色；双击小色块可直接编辑该颜色"));
        row->addWidget(m_preview);
        auto *cells = new QWidget(this);
        auto *grid = new QGridLayout(cells);
        grid->setContentsMargins(0, 0, 0, 0); grid->setSpacing(u(5));
        const int cellsWidth = u(10 * 26 + 9 * 5);
        const int cellsHeight = u(2 * 26 + 5);
        // Reserve the complete two-row palette width.  Without a fixed
        // cell container, the nested preference layouts can compress the
        // last columns until only part of the palette is visible.
        cells->setFixedSize(cellsWidth, cellsHeight);
        cells->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        for (int i = 0; i < 20; ++i) {
            auto *button = new QToolButton(cells);
            button->setFixedSize(u(26), u(26));
            button->setObjectName(QStringLiteral("paletteColor%1").arg(i));
            button->setContextMenuPolicy(Qt::CustomContextMenu);
            grid->addWidget(button, i / 10, i % 10);
            m_buttons.append(button);
            connect(button, &QToolButton::clicked, this, [this, i] { m_selected = i; refresh(); });
            connect(button, &QWidget::customContextMenuRequested, this, [this, i] { edit(i); });
            button->installEventFilter(this);
        }
        row->addWidget(cells); row->addStretch();
        setFixedSize(sizeHint());
        connect(m_preview, &QPushButton::clicked, this, [this] { edit(m_selected); });
    }

    QSize sizeHint() const override
    {
        // Keep the complete 2x10 palette visible when the parent layout is
        // constrained.  Relying only on the child layout's implicit hint lets
        // a narrow preferences page collapse the color cells to zero width.
        const int cellsWidth = u(10 * 26 + 9 * 5);
        return QSize(u(56 + 4) + cellsWidth, u(56));
    }

    QSize minimumSizeHint() const override { return sizeHint(); }

    QStringList colors() const { return m_colors; }
    void setColors(QStringList colors)
    {
        m_colors = std::move(colors);
        if (m_colors.isEmpty()) m_colors = defaultAnnotationPalette();
        while (m_colors.size() > 20) m_colors.removeLast();
        m_selected = qBound(0, m_selected, int(m_colors.size()) - 1);
        refresh();
    }
    void setCount(int count)
    {
        const auto defaults = defaultAnnotationPalette();
        while (m_colors.size() < count) m_colors.append(defaults.at(m_colors.size()));
        while (m_colors.size() > count) m_colors.removeLast();
        m_selected = qBound(0, m_selected, int(m_colors.size()) - 1);
        refresh();
    }

protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        const int index = m_buttons.indexOf(qobject_cast<QToolButton *>(object));
        if (index >= 0 && index < m_colors.size()) {
            if (event->type() == QEvent::MouseButtonPress) {
                const auto *mouse = static_cast<QMouseEvent *>(event);
                if (mouse->button() == Qt::LeftButton) {
                    m_dragIndex = index;
                    m_dragging = false;
                    m_pressGlobal = mouse->globalPosition().toPoint();
                }
            } else if (event->type() == QEvent::MouseMove && m_dragIndex >= 0) {
                const auto *mouse = static_cast<QMouseEvent *>(event);
                if (!m_dragging && (mouse->globalPosition().toPoint() - m_pressGlobal).manhattanLength()
                    >= QApplication::startDragDistance()) {
                    m_dragging = true;
                }
                if (m_dragging) {
                    const int target = indexAtGlobal(mouse->globalPosition().toPoint());
                    if (target >= 0 && target < m_colors.size() && target != m_dragIndex) {
                        m_colors.move(m_dragIndex, target);
                        m_selected = target;
                        m_dragIndex = target;
                        refresh();
                    }
                    return true;
                }
            } else if (event->type() == QEvent::MouseButtonRelease && m_dragIndex >= 0) {
                const auto *mouse = static_cast<QMouseEvent *>(event);
                const bool consume = m_dragging && mouse->button() == Qt::LeftButton;
                m_dragIndex = -1;
                m_dragging = false;
                if (consume)
                    return true;
            } else if (event->type() == QEvent::Wheel) {
                const auto *wheel = static_cast<QWheelEvent *>(event);
                int delta = wheel->angleDelta().y();
                if (delta == 0)
                    delta = wheel->pixelDelta().y();
                if (delta != 0) {
                    QColor color(m_colors.at(index));
                    color.setAlpha(qBound(0, color.alpha() + (delta > 0 ? 16 : -16), 255));
                    m_colors[index] = color.name(QColor::HexArgb);
                    m_selected = index;
                    refresh();
                    return true;
                }
            }
        }
        if (event->type() == QEvent::MouseButtonDblClick) {
            if (index >= 0) { edit(index); return true; }
        }
        return QWidget::eventFilter(object, event);
    }

private:
    int u(int value) const { return qMax(1, qRound(value * m_scale)); }
    int indexAtGlobal(const QPoint &global) const
    {
        for (int index = 0; index < m_buttons.size(); ++index) {
            auto *button = m_buttons.at(index);
            if (button->isVisible() && button->rect().contains(button->mapFromGlobal(global)))
                return index;
        }
        return -1;
    }
    void edit(int index)
    {
        if (index >= m_colors.size()) return;
        const QColor color = QColorDialog::getColor(QColor(m_colors.at(index)), this,
            QStringLiteral("调色板颜色"), QColorDialog::ShowAlphaChannel);
        if (color.isValid()) { m_colors[index] = color.name(QColor::HexArgb); m_selected = index; refresh(); }
    }
    void refresh()
    {
        for (int i = 0; i < m_buttons.size(); ++i) {
            auto *button = m_buttons.at(i);
            button->setVisible(i < m_colors.size());
            if (i < m_colors.size()) {
                button->setStyleSheet(QStringLiteral("background: %1; border: 1px solid #808080;").arg(m_colors.at(i)));
                button->setToolTip(m_colors.at(i));
            }
        }
        m_preview->setIcon(QIcon(palettePreviewPixmap(QColor(m_colors.at(m_selected)), u(44))));
        m_preview->setProperty("color", m_colors.at(m_selected));
        setProperty("colors", m_colors);
    }
    qreal m_scale;
    int m_selected = 3;
    QStringList m_colors;
    QVector<QToolButton *> m_buttons;
    int m_dragIndex = -1;
    bool m_dragging = false;
    QPoint m_pressGlobal;
    QPushButton *m_preview = nullptr;
};

} // namespace

int PreferencesDialog::px(int physicalPixels) const
{
    // These values describe the reference layout in Qt logical pixels.  Qt
    // already applies the Windows scale factor when painting the window.  A
    // second division here made controls such as QSpinBox and QComboBox lose
    // their editor area at 125%/150% display scaling.
    return qMax(1, physicalPixels);
}

QWidget *PreferencesDialog::createGeneralPage(QWidget *parent)
{
    auto *page = new QWidget(parent);
    page->setObjectName(QStringLiteral("generalPage"));
    page->setProperty("referenceLayout", true);
    page->setProperty("referenceHeight", px(425));
    page->setMinimumHeight(px(425));
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(px(12), px(12), px(12), px(12)); layout->setSpacing(px(7));
    const auto row = [this, page, layout] {
        auto *widget = new QWidget(page); widget->setFixedHeight(px(24));
        auto *line = new QHBoxLayout(widget); line->setContentsMargins(0, 0, 0, 0); line->setSpacing(px(7));
        layout->addWidget(widget); return line;
    };
    auto *languageRow = row();
    languageRow->addWidget(new QLabel(QStringLiteral("显示语言:"), page));
    auto *language = new QComboBox(page); language->setObjectName(QStringLiteral("displayLanguage"));
    language->addItem(QStringLiteral("简体中文")); language->setFixedWidth(px(168));
    languageRow->addWidget(language); languageRow->addStretch();
    auto *startupRow = row();
    m_startupCheck = new QCheckBox(QStringLiteral("开机启动"), page);
    m_startupCheck->setObjectName(QStringLiteral("startAtLogin"));
    m_startupCheck->setChecked(m_config.value(QStringLiteral("general/startup"), false).toBool());
    auto *admin = new QCheckBox(QStringLiteral("以管理员身份"), page);
    admin->setObjectName(QStringLiteral("startupAsAdmin"));
    admin->setChecked(m_config.value(QStringLiteral("general/startupAsAdmin"), false).toBool());
    admin->setEnabled(m_startupCheck->isChecked());
    connect(m_startupCheck, &QCheckBox::toggled, admin, &QWidget::setEnabled);
#if defined(Q_OS_MACOS)
    admin->hide();
#endif
    startupRow->addWidget(m_startupCheck); startupRow->addWidget(admin);
    auto *adminInfo = infoLabel(page, QStringLiteral("管理员启动需要 Windows 授权。"));
#if defined(Q_OS_MACOS)
    adminInfo->hide();
#endif
    startupRow->addWidget(adminInfo); startupRow->addStretch();
    const auto check = [this, page, row](const QString &text, const QString &key, bool fallback) {
        auto *b = new QCheckBox(text, page); b->setObjectName(key);
        b->setChecked(m_config.value(key, fallback).toBool());
        auto *line = row(); line->addWidget(b); line->addStretch(); return b;
    };
    m_backupCheck = check(QStringLiteral("自动备份"), QStringLiteral("general/backup"), true);
    m_quickResponseCheck = check(QStringLiteral("保持快速响应"), QStringLiteral("general/quickResponse"), true);
    m_extendedTrayCheck = check(QStringLiteral("增强版托盘菜单"), QStringLiteral("general/extendedTrayMenu"), true);
    auto *noDpiScale = check(QStringLiteral("高 DPI 设置时禁用贴图窗口缩放"), QStringLiteral("general/disablePasteDpiScaling"), false);
    auto *logRow = row();
    logRow->addWidget(new QLabel(QStringLiteral("日志级别:"), page));
    auto *logLevel = new QComboBox(page); logLevel->setObjectName(QStringLiteral("logLevel"));
    logLevel->addItems({QStringLiteral("静默"), QStringLiteral("普通"), QStringLiteral("详细")});
    logLevel->setCurrentIndex(qBound(0, m_config.value(QStringLiteral("general/logLevel"), 1).toInt(), 2));
    logLevel->setFixedWidth(px(86)); logRow->addWidget(logLevel);
    auto *openLog = new QPushButton(QStringLiteral("打开日志文件"), page); logRow->addWidget(openLog); logRow->addStretch();
    connect(openLog, &QPushButton::clicked, this, [] {
        const QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("snipaste.log"));
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    auto *group = new QGroupBox(QStringLiteral("配置文件存储位置"), page);
    auto *configLayout = new QVBoxLayout(group);
    configLayout->setContentsMargins(px(12), px(20), px(12), px(10)); configLayout->setSpacing(px(7));
    auto *pathRow = new QHBoxLayout; pathRow->setSpacing(px(7));
    pathRow->addWidget(new QLabel(QStringLiteral("路径:"), group));
    auto *path = new QLineEdit(QFileInfo(m_config.filePath()).fileName(), group);
    path->setObjectName(QStringLiteral("configPath")); path->setReadOnly(true); path->setToolTip(m_config.filePath());
    pathRow->addWidget(path); configLayout->addLayout(pathRow);
    auto *buttons = new QHBoxLayout; buttons->setSpacing(px(10));
    auto *folder = new QPushButton(QStringLiteral("打开所在文件夹"), group);
    auto *open = new QPushButton(QStringLiteral("打开"), group);
    auto *change = new QPushButton(QStringLiteral("更改"), group);
    open->setFixedWidth(px(90)); change->setFixedWidth(px(90));
    buttons->addWidget(folder); buttons->addStretch(); buttons->addWidget(open); buttons->addWidget(change);
    configLayout->addLayout(buttons); layout->addWidget(group);
    connect(folder, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_config.filePath()).absolutePath()));
    });
    connect(open, &QPushButton::clicked, this, [this] {
        m_config.save(); QDesktopServices::openUrl(QUrl::fromLocalFile(m_config.filePath()));
    });
    connect(change, &QPushButton::clicked, this, [this, path] {
        const QString destination = QFileDialog::getSaveFileName(this, QStringLiteral("配置文件存储位置"), m_config.filePath(), QStringLiteral("配置文件 (*.ini)"));
        if (destination.isEmpty()) return;
        for (const auto &save : m_pageSavers) save();
        if (!m_config.relocate(destination)) {
            QMessageBox::warning(this, QStringLiteral("配置文件"), QStringLiteral("无法保存到此位置，请选择可写的文件夹。")); return;
        }
        path->setText(QFileInfo(m_config.filePath()).fileName()); path->setToolTip(m_config.filePath());
    });
    layout->addStretch();
    auto *footer = new QHBoxLayout;
    auto *help = new QToolButton(page); help->setText(QStringLiteral("⊙")); help->setToolTip(QStringLiteral("设置在关闭窗口时保存"));
    help->setFixedSize(px(24), px(24)); help->setAutoRaise(true);
    auto *restore = new QPushButton(QStringLiteral("恢复默认"), page); restore->setObjectName(QStringLiteral("restoreGeneral"));
    restore->setFixedWidth(px(90)); footer->addWidget(help); footer->addStretch(); footer->addWidget(restore); layout->addLayout(footer);
    connect(help, &QToolButton::clicked, this, [] { QDesktopServices::openUrl(QUrl(QStringLiteral("https://docs.snipaste.com/zh-cn/"))); });
    connect(restore, &QPushButton::clicked, this, [this, admin, noDpiScale, logLevel] {
        m_startupCheck->setChecked(false); admin->setChecked(false);
        m_backupCheck->setChecked(true); m_quickResponseCheck->setChecked(true); m_extendedTrayCheck->setChecked(true);
        noDpiScale->setChecked(false); logLevel->setCurrentIndex(1);
    });
    m_pageSavers.append([this, admin, noDpiScale, logLevel] {
        m_config.setValue(QStringLiteral("general/startupAsAdmin"), admin->isChecked());
        m_config.setValue(QStringLiteral("general/disablePasteDpiScaling"), noDpiScale->isChecked());
        m_config.setValue(QStringLiteral("general/logLevel"), logLevel->currentIndex());
    });
    return page;
}

QWidget *PreferencesDialog::createInterfacePage(QWidget *parent)
{
    auto *page = new QWidget(parent); page->setObjectName(QStringLiteral("interfacePage"));
    page->setProperty("referenceLayout", true);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(px(12), px(12), px(12), px(12));
    auto *tabs = new QTabWidget(page); tabs->setObjectName(QStringLiteral("interfaceTabs")); layout->addWidget(tabs);
    const auto newPage = [this, tabs](const QString &label, const QString &name, int height) {
        auto *widget = new QWidget(tabs); widget->setObjectName(name); widget->setProperty("referenceHeight", px(height));
        auto *column = new QVBoxLayout(widget); column->setContentsMargins(px(12), px(13), px(12), px(13)); column->setSpacing(px(7));
        tabs->addTab(widget, label); return column;
    };
    const auto row = [this](QVBoxLayout *column) {
        const bool toolbarRow = column->parentWidget()->objectName() == QStringLiteral("interfaceToolbar");
        auto *widget = new QWidget(column->parentWidget()); widget->setFixedHeight(px(toolbarRow ? 40 : 29));
        auto *line = new QHBoxLayout(widget); line->setContentsMargins(0, 0, 0, 0); line->setSpacing(px(7));
        column->addWidget(widget); return line;
    };
    const auto restore = [this](QVBoxLayout *column, const QString &name) {
        column->addStretch(); auto *line = new QHBoxLayout; line->addStretch();
        auto *button = new QPushButton(QStringLiteral("恢复默认"), column->parentWidget());
        button->setFixedWidth(px(90)); button->setObjectName(name); line->addWidget(button); column->addLayout(line); return button;
    };
    auto *general = newPage(QStringLiteral("常规"), QStringLiteral("interfaceGeneral"), 165);
    auto *fontRow = row(general); fontRow->addWidget(new QLabel(QStringLiteral("界面字体:")));
    auto *fontButton = new QPushButton(page); fontButton->setObjectName(QStringLiteral("interfaceFont"));
    const auto setFontValue = [fontButton](const QFont &font) {
        fontButton->setProperty("fontValue", font.toString());
        fontButton->setText(QStringLiteral("%1, %2").arg(font.family()).arg(font.pointSize()));
    };
    QFont uiFont(QStringLiteral("Microsoft YaHei UI"), 9);
    const auto savedFont = m_config.value(QStringLiteral("interface/font")).toString();
    if (!savedFont.isEmpty()) uiFont.fromString(savedFont);
    setFontValue(uiFont); fontRow->addWidget(fontButton); fontRow->addStretch();
    connect(fontButton, &QPushButton::clicked, this, [this, fontButton, setFontValue] {
        QFont initial; initial.fromString(fontButton->property("fontValue").toString());
        bool ok = false; const QFont font = QFontDialog::getFont(&ok, initial, this, QStringLiteral("界面字体"));
        if (ok) setFontValue(font);
    });
    auto *themeRow = row(general); themeRow->addWidget(new QLabel(QStringLiteral("主题颜色:")));
    auto *follow = new QRadioButton(QStringLiteral("跟随系统主题"), themeRow->parentWidget()); follow->setObjectName(QStringLiteral("followSystemColor"));
    auto *custom = new QRadioButton(themeRow->parentWidget()); custom->setObjectName(QStringLiteral("customThemeColor"));
    follow->setChecked(m_config.value(QStringLiteral("interface/followSystemColor"), false).toBool()); custom->setChecked(!follow->isChecked());
    auto *accent = new QPushButton(QStringLiteral("自定义")); accent->setObjectName(QStringLiteral("accentColor")); accent->setFixedWidth(px(90));
    colorPreview(accent, QColor(m_config.value(QStringLiteral("interface/accentColor"), QStringLiteral("#2889ed")).toString()));
    themeRow->addWidget(follow); themeRow->addWidget(custom); themeRow->addWidget(accent); themeRow->addStretch();
    connect(accent, &QPushButton::clicked, this, [this, accent, custom] {
        const QColor color = QColorDialog::getColor(QColor(accent->property("color").toString()), this, QStringLiteral("主题颜色"));
        if (color.isValid()) { colorPreview(accent, color); custom->setChecked(true); }
    });
    auto *trayRow = row(general); trayRow->addWidget(new QLabel(QStringLiteral("托盘图标:")));
    auto *trayAuto = new QRadioButton(QStringLiteral("自动"), trayRow->parentWidget()); trayAuto->setObjectName(QStringLiteral("trayAuto"));
    auto *trayColor = new QRadioButton(trayRow->parentWidget()); trayColor->setObjectName(QStringLiteral("trayColor"));
    trayColor->setIcon(applicationIcon()); trayColor->setIconSize(QSize(px(20), px(20)));
    auto *trayCustom = new QRadioButton(trayRow->parentWidget()); trayCustom->setObjectName(QStringLiteral("trayCustom"));
    auto *chooseIcon = new QPushButton(QStringLiteral("自定义")); chooseIcon->setFixedWidth(px(90)); chooseIcon->setObjectName(QStringLiteral("trayIconFile"));
    chooseIcon->setProperty("path", m_config.value(QStringLiteral("interface/trayIconPath")).toString());
    const int mode = m_config.value(QStringLiteral("interface/trayIconMode"), 0).toInt();
    (mode == 2 ? trayCustom : mode == 1 ? trayColor : trayAuto)->setChecked(true);
    chooseIcon->setEnabled(trayCustom->isChecked());
    connect(trayCustom, &QRadioButton::toggled, chooseIcon, &QWidget::setEnabled);
    connect(chooseIcon, &QPushButton::clicked, this, [this, chooseIcon] {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("托盘图标"), {}, QStringLiteral("图标 (*.ico *.png *.svg)"));
        if (!path.isEmpty() && !QIcon(path).isNull()) { chooseIcon->setProperty("path", path); chooseIcon->setIcon(QIcon(path)); }
    });
    trayRow->addWidget(trayAuto); trayRow->addWidget(trayColor); trayRow->addWidget(trayCustom); trayRow->addWidget(chooseIcon); trayRow->addStretch();
    connect(restore(general, QStringLiteral("restoreInterfaceGeneral")), &QPushButton::clicked, this, [=] {
        setFontValue(QFont(QStringLiteral("Microsoft YaHei UI"), 9)); custom->setChecked(true); colorPreview(accent, QColor("#2889ed"));
        trayAuto->setChecked(true); chooseIcon->setProperty("path", QString()); chooseIcon->setIcon(QIcon());
    });
    m_pageSavers.append([=, this] {
        m_config.setValue(QStringLiteral("interface/font"), fontButton->property("fontValue"));
        m_config.setValue(QStringLiteral("interface/followSystemColor"), follow->isChecked());
        m_config.setValue(QStringLiteral("interface/accentColor"), accent->property("color"));
        m_config.setValue(QStringLiteral("interface/trayIconMode"), trayCustom->isChecked() ? 2 : trayColor->isChecked() ? 1 : 0);
        m_config.setValue(QStringLiteral("interface/trayIconPath"), chooseIcon->property("path"));
    });

    const CaptureAppearance settings = CaptureAppearance::fromConfig(m_config);
    auto *magnifier = newPage(QStringLiteral("放大镜"), QStringLiteral("interfaceMagnifier"), 217);
    const auto check = [this](QHBoxLayout *line, const QString &label, const QString &name, bool value) {
        auto *b = new QCheckBox(label, line->parentWidget()); b->setObjectName(name); b->setChecked(value); line->addWidget(b); return b;
    };
    auto *autoRow = row(magnifier); autoRow->addWidget(new QLabel(QStringLiteral("自动显示当:")));
    auto *normal = check(autoRow, QStringLiteral("普通截屏"), QStringLiteral("magnifierAuto"), settings.automaticMagnifier);
    auto *super = check(autoRow, QStringLiteral("超级截屏"), QStringLiteral("magnifierSuper"), m_config.value(QStringLiteral("interface/magnifier/super"), false).toBool()); autoRow->addStretch();
    auto *formatRow = row(magnifier); formatRow->addWidget(new QLabel(QStringLiteral("颜色值:")));
    auto *formats = new QComboBox; formats->setObjectName(QStringLiteral("magnifierFormats")); formats->setMinimumWidth(px(76)); formats->setFixedWidth(px(76)); formats->setSizeAdjustPolicy(QComboBox::AdjustToContents); formats->addItems(settings.colorFormats);
    auto *minus = new QPushButton(QStringLiteral("−")); minus->setObjectName(QStringLiteral("removeColorFormat")); minus->setFixedWidth(px(27));
    auto *plus = new QPushButton(QStringLiteral("+")); plus->setObjectName(QStringLiteral("addColorFormat")); plus->setFixedWidth(px(27));
    formatRow->addWidget(formats); formatRow->addWidget(minus); formatRow->addWidget(plus); formatRow->addStretch();
    connect(plus, &QPushButton::clicked, this, [formats] { for (const auto &format : {QStringLiteral("RGB"), QStringLiteral("HEX")}) if (formats->findText(format) < 0) { formats->addItem(format); formats->setCurrentText(format); return; } });
    connect(minus, &QPushButton::clicked, this, [formats] { if (formats->count() > 1) formats->removeItem(formats->currentIndex()); });
    auto *hexRow = row(magnifier); hexRow->addSpacing(px(55));
    auto *hideHash = check(hexRow, QStringLiteral("隐藏 '#'"), QStringLiteral("magnifierHideHash"), settings.hideHexPrefix);
    auto *uppercase = check(hexRow, QStringLiteral("HEX 颜色值大写"), QStringLiteral("magnifierUppercase"), settings.uppercaseHex); hexRow->addStretch();
    auto *showRow = row(magnifier); showRow->addWidget(new QLabel(QStringLiteral("显示:")));
    auto *grid = check(showRow, QStringLiteral("网格线"), QStringLiteral("magnifierGrid"), settings.magnifierGrid);
    auto *coordinates = check(showRow, QStringLiteral("屏幕坐标"), QStringLiteral("magnifierCoordinates"), settings.magnifierCoordinates);
    auto *hints = check(showRow, QStringLiteral("操作提示"), QStringLiteral("magnifierHints"), settings.magnifierHints); showRow->addStretch();
    auto *zoomRow = row(magnifier); zoomRow->addWidget(new QLabel(QStringLiteral("放大倍数:")));
    auto *zoom = new QSpinBox; zoom->setObjectName(QStringLiteral("magnifierZoom")); zoom->setRange(3, 30); zoom->setValue(settings.magnifierZoom); zoom->setMinimumWidth(px(64)); zoom->setFixedWidth(px(64)); zoomRow->addWidget(zoom); zoomRow->addStretch();
    connect(restore(magnifier, QStringLiteral("restoreMagnifier")), &QPushButton::clicked, this, [=] {
        normal->setChecked(true); super->setChecked(false); formats->clear(); formats->addItem(QStringLiteral("RGB"));
        hideHash->setChecked(false); uppercase->setChecked(false); grid->setChecked(true); coordinates->setChecked(true); hints->setChecked(true); zoom->setValue(9);
    });
    m_pageSavers.append([=, this] {
        m_config.setValue(QStringLiteral("interface/magnifier/automatic"), normal->isChecked());
        m_config.setValue(QStringLiteral("interface/magnifier/super"), super->isChecked());
        QStringList names; for (int i = 0; i < formats->count(); ++i) names.append(formats->itemText(i));
        m_config.setValue(QStringLiteral("interface/magnifier/formats"), names);
        m_config.setValue(QStringLiteral("interface/magnifier/hideHash"), hideHash->isChecked());
        m_config.setValue(QStringLiteral("interface/magnifier/uppercase"), uppercase->isChecked());
        m_config.setValue(QStringLiteral("interface/magnifier/grid"), grid->isChecked());
        m_config.setValue(QStringLiteral("interface/magnifier/coordinates"), coordinates->isChecked());
        m_config.setValue(QStringLiteral("interface/magnifier/hints"), hints->isChecked());
        m_config.setValue(QStringLiteral("interface/magnifier/zoom"), zoom->value());
    });

    // Leave room for both palette rows and the action row below them.  The
    // previous reference height compressed the fixed palette and let the
    // import/export buttons paint over its second row.
    auto *toolbar = newPage(QStringLiteral("工具条"), QStringLiteral("interfaceToolbar"), 520);
    auto *remember = check(row(toolbar), QStringLiteral("为不同标注形状分别记忆颜色"), QStringLiteral("rememberToolColors"), settings.rememberToolColors);
    auto *freeSelect = check(row(toolbar), QStringLiteral("自由选中"), QStringLiteral("freeSelectAnnotations"), m_config.value(QStringLiteral("interface/toolbar/freeSelect"), true).toBool());
    auto *eraser = check(row(toolbar), QStringLiteral("对橡皮擦工具禁用二次编辑"), QStringLiteral("disableEraserEditing"), m_config.value(QStringLiteral("interface/toolbar/disableEraserEditing"), true).toBool());
    auto *shareRow = row(toolbar); auto *share = check(shareRow, QStringLiteral("显示“分享”按钮"), QStringLiteral("showShareButton"), settings.showShare);
    auto *shareInfo = infoLabel(nullptr, QStringLiteral("显示截图工具条的分享菜单。")); shareRow->addWidget(shareInfo); shareRow->addStretch();
    auto *penRow = row(toolbar); penRow->addWidget(new QLabel(QStringLiteral("笔尖数量:")));
    auto *pens = new QSpinBox; pens->setObjectName(QStringLiteral("penTipCount")); pens->setRange(1, 5); pens->setValue(settings.penCount); pens->setMinimumWidth(px(80)); pens->setFixedWidth(px(80)); penRow->addWidget(pens); penRow->addWidget(infoLabel(nullptr, QStringLiteral("笔尖数量会显示在截图工具条中。"))); penRow->addStretch();
    auto *paletteGroup = new QGroupBox(QStringLiteral("调色板")); auto *paletteLayout = new QVBoxLayout(paletteGroup);
    paletteLayout->setContentsMargins(px(12), px(20), px(12), px(10)); paletteLayout->setSpacing(px(8));
    auto *countRow = new QHBoxLayout; countRow->setSpacing(px(7)); countRow->addWidget(new QLabel(QStringLiteral("颜色数量:")));
    auto *count = new QComboBox; count->setObjectName(QStringLiteral("paletteCount")); count->setMinimumWidth(px(80)); count->setFixedWidth(px(80)); count->setSizeAdjustPolicy(QComboBox::AdjustToContents); for (int i = 1; i <= 20; ++i) count->addItem(QString::number(i));
    count->setCurrentText(QString::number(settings.palette.size())); countRow->addWidget(count); countRow->addStretch();
    auto *paletteInfo = infoLabel(nullptr, QStringLiteral("单击选择颜色，双击或右键编辑；支持 RGB/ARGB 十六进制颜色文本导入。")); countRow->addWidget(paletteInfo); paletteLayout->addLayout(countRow);
    auto *palette = new PaletteEditor(1.0, paletteGroup); palette->setColors(settings.palette);
    auto *paletteRow = new QHBoxLayout;
    paletteRow->setContentsMargins(0, 0, 0, 0);
    paletteRow->addWidget(palette, 0, Qt::AlignLeft | Qt::AlignVCenter);
    paletteRow->addStretch();
    paletteLayout->addLayout(paletteRow);
    paletteLayout->addSpacing(px(4));
    connect(count, &QComboBox::currentTextChanged, palette, [palette](const QString &text) { palette->setCount(text.toInt()); });
    auto *paletteButtons = new QHBoxLayout; paletteButtons->setSpacing(px(8));
    auto *apply = new QToolButton; apply->setText(QStringLiteral("应用到")); apply->setObjectName(QStringLiteral("applyPalette")); apply->setPopupMode(QToolButton::InstantPopup);
    auto *applyMenu = new QMenu(apply); apply->setMenu(applyMenu);
    applyMenu->addAction(QStringLiteral("所有标注工具"), this, [this, palette] { m_config.setValue(QStringLiteral("interface/toolbar/palette"), palette->colors()); });
    auto *importButton = new QPushButton(QStringLiteral("从剪贴板导入")); importButton->setObjectName(QStringLiteral("importPalette")); importButton->setFixedWidth(px(120));
    auto *exportButton = new QPushButton(QStringLiteral("导出到剪贴板")); exportButton->setObjectName(QStringLiteral("exportPalette")); exportButton->setFixedWidth(px(120));
    apply->setFixedWidth(px(90));
    paletteButtons->addWidget(apply); paletteButtons->addStretch(); paletteButtons->addWidget(importButton); paletteButtons->addWidget(exportButton); paletteLayout->addLayout(paletteButtons); toolbar->addWidget(paletteGroup);
    connect(importButton, &QPushButton::clicked, this, [this, palette, count] {
        const QString text = QApplication::clipboard()->text().trimmed();
        QStringList colors; const QJsonDocument json = QJsonDocument::fromJson(text.toUtf8());
        if (json.isArray()) { for (const auto &value : json.array()) colors.append(value.toString()); }
        else colors = text.split(QRegularExpression(QStringLiteral("[\\s,;]+")), Qt::SkipEmptyParts);
        if (colors.isEmpty() || colors.size() > 20 || std::any_of(colors.begin(), colors.end(), [](const QString &color) { return !QColor(color).isValid(); })) {
            QMessageBox::warning(this, QStringLiteral("导入调色板"), QStringLiteral("请复制 1–20 个有效颜色，例如 #FF4141，每行一个。")); return;
        }
        palette->setColors(colors); count->setCurrentText(QString::number(colors.size()));
    });
    connect(exportButton, &QPushButton::clicked, this, [palette] { QApplication::clipboard()->setText(palette->colors().join(QChar::LineFeed)); });
    connect(restore(toolbar, QStringLiteral("restoreToolbar")), &QPushButton::clicked, this, [=] {
        remember->setChecked(true); freeSelect->setChecked(true); eraser->setChecked(true); share->setChecked(true); pens->setValue(3);
        palette->setColors(defaultAnnotationPalette()); count->setCurrentText(QStringLiteral("20"));
    });
    m_pageSavers.append([=, this] {
        m_config.setValue(QStringLiteral("interface/toolbar/rememberColors"), remember->isChecked());
        m_config.setValue(QStringLiteral("interface/toolbar/freeSelect"), freeSelect->isChecked());
        m_config.setValue(QStringLiteral("interface/toolbar/disableEraserEditing"), eraser->isChecked());
        m_config.setValue(QStringLiteral("interface/toolbar/showShare"), share->isChecked());
        m_config.setValue(QStringLiteral("interface/toolbar/penCount"), pens->value());
        m_config.setValue(QStringLiteral("interface/toolbar/palette"), palette->colors());
    });
    return page;
}

} // namespace snipaste::ui
