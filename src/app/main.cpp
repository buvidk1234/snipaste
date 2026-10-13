#include "app/logging.h"
#include "core/command/command_registry.h"
#include "core/command/argument_parser.h"
#include "core/config/config_store.h"
#include "core/control/hot_corner_state.h"
#include "platform/keyboard.h"
#include "core/history/screenshot_history.h"
#include "core/recognition/ocr_service.h"
#include "core/recognition/barcode_service.h"
#include "platform/windows/global_hotkey.h"
#include "platform/windows/super_capture.h"
#include "platform/windows/single_instance.h"
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
#include "platform/macos/system.h"
#endif
#include "ui/paste/paste_window.h"
#include "ui/paste/paste_manager.h"
#include "ui/annotation/annotation_dialog.h"
#include "ui/history/screenshot_history_dialog.h"
#include "ui/preferences/preferences_dialog.h"
#include "ui/preferences/appearance_settings.h"
#include "ui/snip/snip_overlay.h"
#include "ui/snip/window_capture_overlay.h"
#include "ui/tray/tray_controller.h"

#include <QApplication>
#include <QClipboard>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QMessageBox>
#include <QMutex>
#include <QMutexLocker>
#include <QMenu>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPointer>
#include <QCursor>
#include <QDateTime>
#include <QElapsedTimer>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QMimeData>
#include <QInputDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QKeyEvent>
#include <QPaintEvent>
#include <QKeySequence>
#include <QListWidget>
#include <QListView>
#include <QLabel>
#include <QIcon>
#include <QPixmap>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QPainter>
#include <QScreen>
#include <QStandardPaths>
#include <QStyle>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVector>
#include <QUuid>
#include <QRegularExpression>
#include <QTextDocumentFragment>
#include <QTextDocument>
#include <QAbstractTextDocumentLayout>
#include <QPrinter>
#include <QPrintDialog>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)
#include <Carbon/Carbon.h>
#endif

namespace {

using snipaste::core::expandOutputFilePattern;
using snipaste::core::normalizeMultiValueOptions;
using snipaste::core::parsePointSpec;
using snipaste::core::parseRectSpec;
using snipaste::core::parseSizeSpec;
using snipaste::core::splitOutputSpec;

struct HotkeySpec {
    unsigned int modifiers = 0;
    unsigned int key = 0;
    bool valid = false;
    bool simple = false;
};

HotkeySpec parseHotkey(const QString &text)
{
    HotkeySpec result;
    const QKeySequence sequence(text, QKeySequence::PortableText);
    if (sequence.isEmpty())
        return result;
    const QKeyCombination combination = sequence[0];
    const Qt::KeyboardModifiers qtModifiers = combination.keyboardModifiers();
    const int base = combination.key();
    const bool functionKey = base >= Qt::Key_F1 && base <= Qt::Key_F24;
    result.simple = !functionKey
        && (qtModifiers == Qt::NoModifier || qtModifiers == Qt::ShiftModifier);
#ifdef Q_OS_WIN
    if (qtModifiers.testFlag(Qt::ControlModifier))
        result.modifiers |= MOD_CONTROL;
    if (qtModifiers.testFlag(Qt::AltModifier))
        result.modifiers |= MOD_ALT;
    if (qtModifiers.testFlag(Qt::ShiftModifier))
        result.modifiers |= MOD_SHIFT;
    if (qtModifiers.testFlag(Qt::MetaModifier))
        result.modifiers |= MOD_WIN;
    if (base >= Qt::Key_A && base <= Qt::Key_Z)
        result.key = static_cast<unsigned int>('A' + base - Qt::Key_A);
    else if (base >= Qt::Key_0 && base <= Qt::Key_9)
        result.key = static_cast<unsigned int>('0' + base - Qt::Key_0);
    else if (base >= Qt::Key_F1 && base <= Qt::Key_F24)
        result.key = static_cast<unsigned int>(VK_F1 + base - Qt::Key_F1);
    else {
        switch (base) {
        case Qt::Key_Space: result.key = VK_SPACE; break;
        case Qt::Key_Tab: result.key = VK_TAB; break;
        case Qt::Key_Return:
        case Qt::Key_Enter: result.key = VK_RETURN; break;
        case Qt::Key_Escape: result.key = VK_ESCAPE; break;
        case Qt::Key_Insert: result.key = VK_INSERT; break;
        case Qt::Key_Delete: result.key = VK_DELETE; break;
        case Qt::Key_Home: result.key = VK_HOME; break;
        case Qt::Key_End: result.key = VK_END; break;
        case Qt::Key_PageUp: result.key = VK_PRIOR; break;
        case Qt::Key_PageDown: result.key = VK_NEXT; break;
        case Qt::Key_Left: result.key = VK_LEFT; break;
        case Qt::Key_Right: result.key = VK_RIGHT; break;
        case Qt::Key_Up: result.key = VK_UP; break;
        case Qt::Key_Down: result.key = VK_DOWN; break;
        case Qt::Key_Print: result.key = VK_SNAPSHOT; break;
        default: break;
        }
    }
#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    if (qtModifiers.testFlag(Qt::ControlModifier))
        result.modifiers |= controlKey;
    if (qtModifiers.testFlag(Qt::AltModifier))
        result.modifiers |= optionKey;
    if (qtModifiers.testFlag(Qt::ShiftModifier))
        result.modifiers |= shiftKey;
    if (qtModifiers.testFlag(Qt::MetaModifier))
        result.modifiers |= cmdKey;

    static constexpr unsigned short letters[] = {
        kVK_ANSI_A, kVK_ANSI_B, kVK_ANSI_C, kVK_ANSI_D, kVK_ANSI_E,
        kVK_ANSI_F, kVK_ANSI_G, kVK_ANSI_H, kVK_ANSI_I, kVK_ANSI_J,
        kVK_ANSI_K, kVK_ANSI_L, kVK_ANSI_M, kVK_ANSI_N, kVK_ANSI_O,
        kVK_ANSI_P, kVK_ANSI_Q, kVK_ANSI_R, kVK_ANSI_S, kVK_ANSI_T,
        kVK_ANSI_U, kVK_ANSI_V, kVK_ANSI_W, kVK_ANSI_X, kVK_ANSI_Y,
        kVK_ANSI_Z,
    };
    static constexpr unsigned short digits[] = {
        kVK_ANSI_0, kVK_ANSI_1, kVK_ANSI_2, kVK_ANSI_3, kVK_ANSI_4,
        kVK_ANSI_5, kVK_ANSI_6, kVK_ANSI_7, kVK_ANSI_8, kVK_ANSI_9,
    };
    if (base >= Qt::Key_A && base <= Qt::Key_Z)
        result.key = letters[base - Qt::Key_A];
    else if (base >= Qt::Key_0 && base <= Qt::Key_9)
        result.key = digits[base - Qt::Key_0];
    else if (base >= Qt::Key_F1 && base <= Qt::Key_F20) {
        // Carbon's function-key virtual codes are intentionally sparse (for
        // example F1=122, F2=120, F3=99), so they cannot be derived by adding
        // an offset to kVK_F1.
        static constexpr unsigned short functionKeys[] = {
            kVK_F1, kVK_F2, kVK_F3, kVK_F4, kVK_F5,
            kVK_F6, kVK_F7, kVK_F8, kVK_F9, kVK_F10,
            kVK_F11, kVK_F12, kVK_F13, kVK_F14, kVK_F15,
            kVK_F16, kVK_F17, kVK_F18, kVK_F19, kVK_F20,
        };
        result.key = functionKeys[base - Qt::Key_F1];
    }
    else {
        switch (base) {
        case Qt::Key_Space: result.key = kVK_Space; break;
        case Qt::Key_Tab: result.key = kVK_Tab; break;
        case Qt::Key_Return:
        case Qt::Key_Enter: result.key = kVK_Return; break;
        case Qt::Key_Escape: result.key = kVK_Escape; break;
        case Qt::Key_Delete: result.key = kVK_ForwardDelete; break;
        case Qt::Key_Home: result.key = kVK_Home; break;
        case Qt::Key_End: result.key = kVK_End; break;
        case Qt::Key_PageUp: result.key = kVK_PageUp; break;
        case Qt::Key_PageDown: result.key = kVK_PageDown; break;
        case Qt::Key_Left: result.key = kVK_LeftArrow; break;
        case Qt::Key_Right: result.key = kVK_RightArrow; break;
        case Qt::Key_Up: result.key = kVK_UpArrow; break;
        case Qt::Key_Down: result.key = kVK_DownArrow; break;
        default: break;
        }
    }
#else
    Q_UNUSED(qtModifiers)
    Q_UNUSED(base)
#endif
    result.valid = result.key != 0;
    return result;
}

bool isApplicationIgnored(const snipaste::core::ConfigStore &config, const QString &key)
{
    QStringList ignored = config.value(key).toStringList();
    if (ignored.isEmpty())
        ignored = config.value(key).toString().split(QRegularExpression(QStringLiteral("[,;\\n]+")),
                                                     Qt::SkipEmptyParts);
    if (ignored.isEmpty())
        return false;
#ifdef Q_OS_WIN
    HWND window = GetForegroundWindow();
    if (!window)
        return false;
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process)
        return false;
    wchar_t buffer[MAX_PATH]{};
    DWORD size = MAX_PATH;
    const bool success = QueryFullProcessImageNameW(process, 0, buffer, &size) != FALSE;
    CloseHandle(process);
    if (!success)
        return false;
    const QString executable = QFileInfo(QString::fromWCharArray(buffer, static_cast<int>(size)))
                                   .fileName()
                                   .toLower();
    for (const QString &entry : ignored) {
        const QString normalized = entry.trimmed().toLower();
        if (!normalized.isEmpty() && executable.contains(normalized))
            return true;
    }
#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    return snipaste::platform::macForegroundApplicationMatches(ignored);
#else
    Q_UNUSED(config)
    Q_UNUSED(key)
#endif
    return false;
}

bool isHotkeyIgnored(const snipaste::core::ConfigStore &config)
{
    return isApplicationIgnored(config, QStringLiteral("hotkeys/ignoreApplications"));
}

QStringList configuredProcessList(const snipaste::core::ConfigStore &config,
                                  const QString &key, const QString &legacyKey = {})
{
    QVariant value = config.value(key);
    if (!value.isValid() && !legacyKey.isEmpty())
        value = config.value(legacyKey);
    QStringList entries = value.toStringList();
    if (entries.isEmpty())
        entries = value.toString().split(QRegularExpression(QStringLiteral("[,;\\n]+")),
                                         Qt::SkipEmptyParts);
    for (QString &entry : entries)
        entry = entry.trimmed();
    entries.removeAll(QString());
    return entries;
}

QImage clipboardTextImage(const QString &text)
{
    const QStringList lines = text.split(QChar::LineFeed);
    QFont font;
    font.setPixelSize(18);
    const QFontMetrics metrics(font);
    int width = 320;
    for (const QString &line : lines)
        width = qMax(width, metrics.horizontalAdvance(line) + 32);
    const int height = qMax(64, lines.size() * metrics.lineSpacing() + 32);
    QImage image(QSize(width, height), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setFont(font);
    painter.setPen(Qt::black);
    painter.drawText(QRect(16, 16, width - 32, height - 32),
                     Qt::AlignLeft | Qt::AlignTop,
                     text);
    return image;
}

QColor parseClipboardColor(const QString &text)
{
    const QString value = text.trimmed();
    if (value.startsWith(QChar('#'))) {
        const QColor color(value);
        return color.isValid() ? color : QColor();
    }

    const QStringList components = value.split(QRegularExpression(QStringLiteral("[,;\\s]+")),
                                               Qt::SkipEmptyParts);
    if (components.size() != 3 && components.size() != 4)
        return {};
    QVector<double> numbers;
    numbers.reserve(components.size());
    for (const QString &component : components) {
        bool ok = false;
        const double number = component.toDouble(&ok);
        if (!ok || number < 0.0)
            return {};
        numbers.append(number);
    }
    const bool normalized = std::all_of(numbers.cbegin(), numbers.cend(), [](double value) {
        return value <= 1.0;
    });
    const auto channel = [normalized](double value) {
        return qBound(0, qRound(normalized ? value * 255.0 : value), 255);
    };
    QColor color(channel(numbers.at(0)), channel(numbers.at(1)), channel(numbers.at(2)));
    if (numbers.size() == 4)
        color.setAlpha(channel(numbers.at(3)));
    return color;
}

QImage clipboardColorImage(const QColor &color)
{
    if (!color.isValid())
        return {};
    QImage image(QSize(240, 160), QImage::Format_ARGB32_Premultiplied);
    image.fill(color);
    return image;
}

QImage clipboardHtmlImage(const QString &html, bool forceWrap = true)
{
    QTextDocument document;
    document.setDefaultFont(QFont(QStringLiteral("Microsoft YaHei UI"), 10));
    document.setHtml(html);
    document.setTextWidth(forceWrap ? 720 : -1);
    const QSizeF documentSize = document.documentLayout()->documentSize();
    const QSize size(qMax(64, qCeil(documentSize.width()) + 24),
                     qMax(48, qCeil(documentSize.height()) + 24));
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.translate(12, 12);
    document.drawContents(&painter);
    return image;
}

QImage addDropShadow(const QImage &source)
{
    if (source.isNull())
        return {};
    constexpr int margin = 12;
    QImage result(source.size() + QSize(margin * 2, margin * 2),
                  QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setOpacity(0.18);
    for (int offset = 4; offset >= 1; --offset)
        painter.drawImage(margin + offset, margin + offset, source);
    painter.setOpacity(1.0);
    painter.drawImage(margin, margin, source);
    return result;
}

QImage removeRedundantAlpha(const QImage &source, bool enabled)
{
    if (!enabled || source.isNull() || !source.hasAlphaChannel())
        return source;
    for (int y = 0; y < source.height(); ++y) {
        for (int x = 0; x < source.width(); ++x)
            if (qAlpha(source.pixel(x, y)) != 255)
                return source;
    }
    return source.convertToFormat(QImage::Format_RGB32);
}

QImage addSoftwareMetadata(const QImage &source, bool enabled)
{
    if (!enabled || source.isNull())
        return source;
    QImage result = source;
    result.setText(QStringLiteral("Software"),
                   QStringLiteral("snipaste %1").arg(QCoreApplication::applicationVersion()));
    result.setText(QStringLiteral("Comment"), QStringLiteral("Created by snipaste"));
    return result;
}

bool printImage(const QImage &image)
{
    if (image.isNull())
        return false;
    QPrinter printer(QPrinter::HighResolution);
    printer.setDocName(QStringLiteral("snipaste screenshot"));
    QPrintDialog dialog(&printer);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    QPainter painter(&printer);
    if (!painter.isActive())
        return false;
    const QRect page = printer.pageRect(QPrinter::DevicePixel).toRect();
    const QImage scaled = image.scaled(page.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const QPoint topLeft(page.left() + (page.width() - scaled.width()) / 2,
                         page.top() + (page.height() - scaled.height()) / 2);
    painter.drawImage(topLeft, scaled);
    return true;
}

QStringList resolvePasteFileArguments(const QStringList &arguments)
{
    QStringList resolved;
    QString currentFolder;
    for (const QString &argument : arguments) {
        if (argument.trimmed().isEmpty())
            continue;
        const QString expanded = QDir::fromNativeSeparators(argument);
        const QFileInfo info(expanded);
        const bool explicitFolder = expanded.endsWith(QChar('/'))
                                    || expanded.endsWith(QChar('\\'));
        if (info.isDir() || explicitFolder) {
            currentFolder = info.isAbsolute() ? info.absoluteFilePath()
                                              : QDir::current().absoluteFilePath(expanded);
            continue;
        }
        if (info.isAbsolute() || currentFolder.isEmpty())
            resolved.append(info.isAbsolute() ? info.absoluteFilePath() : expanded);
        else
            resolved.append(QDir(currentFolder).filePath(expanded));
    }
    return resolved;
}

bool ensureMacScreenCaptureAccess(const QString &operation)
{
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    if (snipaste::platform::macScreenCaptureAccessGranted()
        || snipaste::platform::requestMacScreenCaptureAccess())
        return true;
    QMessageBox::warning(nullptr,
                          QStringLiteral("需要屏幕录制权限"),
                          QStringLiteral("请在“系统设置 → 隐私与安全性 → 屏幕录制”中允许 snipaste，\n"
                                         "然后重新开始%1。")
                              .arg(operation));
    return false;
#else
    Q_UNUSED(operation)
    return true;
#endif
}

QImage captureScreens(const QList<QScreen *> &screens)
{
    if (screens.isEmpty())
        return {};
    if (!ensureMacScreenCaptureAccess(QStringLiteral("截图")))
        return {};

    const QRect virtualGeometry = screens.first()->virtualGeometry();
    QImage result(virtualGeometry.size(), QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::black);
    QPainter painter(&result);
    for (QScreen *screen : screens) {
        if (!screen)
            continue;
        QImage image = screen->grabWindow(0).toImage();
        const QRect target = screen->geometry().translated(-virtualGeometry.topLeft());
        if (image.size() != target.size())
            image = image.scaled(target.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        painter.drawImage(target.topLeft(), image);
    }
    return result;
}

QImage captureAllScreens()
{
    return captureScreens(QGuiApplication::screens());
}

void applyConfiguredTheme(const QString &theme)
{
    QPalette palette = QApplication::style()->standardPalette();
    if (theme == QStringLiteral("深色")) {
        palette.setColor(QPalette::Window, QColor(45, 45, 48));
        palette.setColor(QPalette::WindowText, QColor(235, 235, 235));
        palette.setColor(QPalette::Base, QColor(30, 30, 30));
        palette.setColor(QPalette::AlternateBase, QColor(45, 45, 48));
        palette.setColor(QPalette::Text, QColor(235, 235, 235));
        palette.setColor(QPalette::Button, QColor(60, 60, 64));
        palette.setColor(QPalette::ButtonText, QColor(235, 235, 235));
        palette.setColor(QPalette::Highlight, QColor(42, 130, 218));
        palette.setColor(QPalette::HighlightedText, Qt::white);
    }
    QApplication::setPalette(palette);
}

void applyConfiguredProcessPriority(const snipaste::core::ConfigStore &config)
{
#ifdef Q_OS_WIN
    const int configured = qBound(0, config.value(QStringLiteral("general/process_priority"),
                                                  config.value(QStringLiteral("process_priority"), 0)).toInt(), 3);
    DWORD priority = NORMAL_PRIORITY_CLASS;
    if (configured == 1)
        priority = ABOVE_NORMAL_PRIORITY_CLASS;
    else if (configured == 2)
        priority = HIGH_PRIORITY_CLASS;
    else if (configured == 3)
        priority = REALTIME_PRIORITY_CLASS;
    if (!SetPriorityClass(GetCurrentProcess(), priority))
        qWarning() << "failed to set process priority" << configured << GetLastError();
#else
    Q_UNUSED(config)
#endif
}

class WhiteboardWindow final : public QWidget
{
public:
    WhiteboardWindow(QScreen *screen, QColor color, bool transparent, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_color(std::move(color))
        , m_transparent(transparent)
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        setFocusPolicy(Qt::StrongFocus);
        if (m_transparent)
            setAttribute(Qt::WA_TranslucentBackground);
        if (screen)
            setGeometry(screen->geometry());
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        QColor fill = m_color;
        if (m_transparent)
            fill.setAlpha(80);
        painter.fillRect(rect(), fill);
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Escape)
            close();
        else
            QWidget::keyPressEvent(event);
    }

private:
    QColor m_color;
    bool m_transparent = false;
};

void pasteClipboardAsKeyboardInput()
{
#ifdef Q_OS_WIN
    INPUT inputs[4]{};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CONTROL;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 'V';
    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = 'V';
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = VK_CONTROL;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(4, inputs, sizeof(INPUT));
#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    if (!snipaste::platform::postMacPasteShortcut())
        qWarning() << "macOS rejected Cmd+V injection; grant Accessibility permission to snipaste";
#endif
}

QColor parseColorSpec(const QString &value, const QColor &fallback = QColor(Qt::white))
{
    const QString text = value.trimmed();
    if (text.isEmpty())
        return fallback;
    QColor color(text);
    return color.isValid() ? color : fallback;
}

QString expandEnvironmentVariables(const QString &value)
{
    QString expanded = value;
    const QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    static const QRegularExpression percent(QStringLiteral("%([^%]+)%"));
    QRegularExpressionMatch match;
    int offset = 0;
    while ((match = percent.match(expanded, offset)).hasMatch()) {
        const QString replacement = environment.value(match.captured(1), match.captured(0));
        expanded.replace(match.capturedStart(0), match.capturedLength(0), replacement);
        offset = match.capturedStart(0) + replacement.size();
    }
    static const QRegularExpression dollar(QStringLiteral("\\$\\{([^}]+)\\}"));
    offset = 0;
    while ((match = dollar.match(expanded, offset)).hasMatch()) {
        const QString replacement = environment.value(match.captured(1), match.captured(0));
        expanded.replace(match.capturedStart(0), match.capturedLength(0), replacement);
        offset = match.capturedStart(0) + replacement.size();
    }
    return expanded;
}

bool startExternalCommand(const QStringList &command)
{
    if (command.isEmpty())
        return false;
    QStringList expanded;
    expanded.reserve(command.size());
    for (const QString &part : command)
        expanded.append(expandEnvironmentVariables(part));
    const QString program = expanded.takeFirst();
    return !program.isEmpty() && QProcess::startDetached(program, expanded);
}

QColor colorAtCursor()
{
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    if (!ensureMacScreenCaptureAccess(QStringLiteral("取色")))
        return {};
#endif
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
        return {};
    const QPoint local = QCursor::pos() - screen->geometry().topLeft();
    const QImage image = screen->grabWindow(0).toImage();
    if (image.isNull() || screen->geometry().width() <= 0 || screen->geometry().height() <= 0)
        return {};
    const qreal scaleX = qreal(image.width()) / qreal(screen->geometry().width());
    const qreal scaleY = qreal(image.height()) / qreal(screen->geometry().height());
    const QPoint pixel(qBound(0, qRound(local.x() * scaleX), image.width() - 1),
                       qBound(0, qRound(local.y() * scaleY), image.height() - 1));
    return image.pixelColor(pixel);
}

qreal configuredPasteOpacity(const snipaste::core::ConfigStore &config)
{
    // Older development builds wrote the value as a normalized fraction
    // (0.1..1.0), while the preferences page writes a percentage
    // (10..100).  Accept both formats so an old 0.8 value cannot become
    // 0.008 and get clamped to a nearly invisible paste.
    const double raw = config.value(QStringLiteral("paste/opacity"), 100).toDouble();
    const double normalized = raw > 1.0 ? raw / 100.0 : raw;
    return qBound<qreal>(0.1, qreal(normalized), 1.0);
}

void trimIdleProcessMemory();

void saveHistorySnapshotAsync(snipaste::core::ScreenshotHistory &history,
                              const QString &directory)
{
    // ScreenshotHistory is updated only on the GUI thread.  Copy its
    // implicitly-shared image lists before dispatching so the disk writer
    // never reads a live QList while a subsequent capture is being added.
    const snipaste::core::ScreenshotHistory snapshot = history;
    const quint64 snapshotRevision = history.revision();
    static QMutex generationMutex;
    static QMutex saveMutex;
    static quint64 latestGeneration = 0;
    quint64 generation = 0;
    {
        QMutexLocker locker(&generationMutex);
        generation = ++latestGeneration;
    }

    QtConcurrent::run([snapshot, directory, generation, snapshotRevision, historyPtr = &history] {
        QMutexLocker saveLocker(&saveMutex);
        {
            QMutexLocker generationLocker(&generationMutex);
            if (generation != latestGeneration)
                return;
        }
        snapshot.saveToDirectory(directory);
        // Rebase only after the write that owns this generation completed.
        // A newer capture may have prepended an item while the worker was
        // writing; the revision check prevents its paths from being changed
        // to filenames belonging to the older snapshot.
        QMetaObject::invokeMethod(QCoreApplication::instance(),
                                  [historyPtr, directory, generation, snapshotRevision] {
                                      QMutexLocker generationLocker(&generationMutex);
                                      if (generation == latestGeneration
                                          && historyPtr->revision() == snapshotRevision) {
                                          historyPtr->rebaseBackgroundPaths(directory);
                                          historyPtr->releaseDecodedImages();
                                          historyPtr->releaseDecodedBackgrounds();
                                          trimIdleProcessMemory();
                                      }
                                  },
                                  Qt::QueuedConnection);
    });
}

QString outputFilePath(const QString &value, const snipaste::core::ConfigStore &config)
{
    QString path = value.trimmed();
    if (path.isEmpty())
        return {};
    path = expandOutputFilePattern(path, QDateTime::currentDateTime());
    if (QFileInfo(path).isRelative()) {
        const QString directory = config.value(QStringLiteral("output/saveDirectory"),
                                               QStandardPaths::writableLocation(QStandardPaths::PicturesLocation))
                                      .toString();
        path = QDir(directory).filePath(path);
    }
    return QDir::cleanPath(path);
}

void trimIdleProcessMemory()
{
#ifdef Q_OS_WIN
    // Qt has already released the screenshot/overlay objects at this point.
    // Ask Windows to evict those now-unused pages so the idle Working Set
    // reflects the release instead of waiting for system memory pressure.
    SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1),
                             static_cast<SIZE_T>(-1));
#endif
}

} // namespace

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    // Capture pixels at the monitor's native scale.  Without opting into
    // per-monitor DPI awareness before QApplication is created, Windows can
    // DPI-virtualize screen grabs to logical pixels; the overlay then paints
    // those already reduced pixels back into the selection and the exported
    // screenshot looks softer than the desktop.
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        SetProcessDPIAware();
#endif
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("snipaste"));
    QCoreApplication::setApplicationName(QStringLiteral("snipaste"));
    QCoreApplication::setApplicationVersion(QStringLiteral(SNIPASTE_VERSION));
    QApplication::setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("开源截图、贴图和标注工具"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption snipOption(QStringLiteral("snip"),
                                         QStringLiteral("启动区域截图"));
    const QCommandLineOption copyOption(QStringLiteral("copy"),
                                        QStringLiteral("启动截图并自动复制"));
    const QCommandLineOption pasteOption(QStringLiteral("paste"),
                                         QStringLiteral("将剪贴板内容贴到屏幕"));
    const QCommandLineOption clipboardPasteOption(QStringLiteral("clipboard"),
                                                   QStringLiteral("paste 的 --clipboard 别名"));
    const QCommandLineOption historyOption(QStringLiteral("history"),
                                           QStringLiteral("打开截屏历史"));
    const QCommandLineOption fullScreenOption(QStringLiteral("full-screen"),
                                              QStringLiteral("捕获所有屏幕并复制"));
    const QCommandLineOption officialFullOption(QStringLiteral("full"), QStringLiteral("snip 命令的全屏模式"));
    const QCommandLineOption officialLastOption(QStringLiteral("last"), QStringLiteral("重复上次截图区域"));
    const QCommandLineOption activeWindowOption(QStringLiteral("active-window"), QStringLiteral("截取当前活动窗口"));
    const QCommandLineOption activeScreenOption(QStringLiteral("active-screen"), QStringLiteral("截取光标所在屏幕"));
    const QCommandLineOption customOption(QStringLiteral("custom"), QStringLiteral("打开自定义截图"));
    const QCommandLineOption holdOption(QStringLiteral("hold"), QStringLiteral("截图确认后保留标注界面"));
    const QCommandLineOption delayOption(QStringLiteral("delay"), QStringLiteral("截图前延迟秒数"), QStringLiteral("seconds"));
    const QCommandLineOption outputOption({QStringLiteral("o"), QStringLiteral("output")}, QStringLiteral("截图输出方式"), QStringLiteral("output"));
    const QCommandLineOption blockOption(QStringLiteral("block"), QStringLiteral("等待截图流程结束后退出"), QStringLiteral("seconds"));
    const QCommandLineOption areaOption(QStringLiteral("area"), QStringLiteral("选区 x,y,width,height"), QStringLiteral("rect"));
    const QCommandLineOption sizeOption(QStringLiteral("size"), QStringLiteral("固定尺寸 width,height"), QStringLiteral("size"));
    const QCommandLineOption positionOption(QStringLiteral("pos"), QStringLiteral("贴图位置 x,y"), QStringLiteral("position"));
    const QCommandLineOption keepAspectOption(QStringLiteral("keep_aspect_ratio"), QStringLiteral("锁定选区宽高比"));
    const QCommandLineOption shadowOption(QStringLiteral("shadow"), QStringLiteral("截图包含阴影"));
    const QCommandLineOption noShadowOption(QStringLiteral("no-shadow"), QStringLiteral("截图不含阴影"));
    const QCommandLineOption windowOption(QStringLiteral("window"),
                                          QStringLiteral("选择窗口截图"));
    const QCommandLineOption elementOption(QStringLiteral("element"),
                                           QStringLiteral("选择界面元素截图"));
    const QCommandLineOption showImagesOption(QStringLiteral("show-images"), QStringLiteral("显示所有贴图"));
    const QCommandLineOption hideImagesOption(QStringLiteral("hide-images"), QStringLiteral("隐藏所有贴图"));
    const QCommandLineOption toggleImagesOption(QStringLiteral("toggle-images"), QStringLiteral("切换所有贴图可见性"));
    const QCommandLineOption groupManagerOption(QStringLiteral("show-group-manager"), QStringLiteral("打开贴图组管理"));
    const QCommandLineOption emptyGroupOption(QStringLiteral("empty-group"), QStringLiteral("清空当前贴图组"));
    const QCommandLineOption switchGroupPrevOption(QStringLiteral("switch-group-prev"), QStringLiteral("切换到上一个贴图组"));
    const QCommandLineOption soloOption(QStringLiteral("solo"), QStringLiteral("切换 Solo 模式"));
    const QCommandLineOption toggleClickThroughOption(QStringLiteral("toggle-click-through"), QStringLiteral("切换鼠标穿透"));
    const QCommandLineOption noClickThroughOption(QStringLiteral("no-click-through"), QStringLiteral("取消所有贴图鼠标穿透"));
    const QCommandLineOption disableHotkeysOption(QStringLiteral("disable-hotkeys"), QStringLiteral("禁用全局热键"));
    const QCommandLineOption enableHotkeysOption(QStringLiteral("enable-hotkeys"), QStringLiteral("启用全局热键"));
    const QCommandLineOption toggleHotkeysOption(QStringLiteral("toggle-hotkeys"), QStringLiteral("切换全局热键"));
    const QCommandLineOption pickColorOption(QStringLiteral("pick-color"), QStringLiteral("拾取光标所在颜色"));
    const QCommandLineOption barcodeDecodeOption(QStringLiteral("barcode-decode"), QStringLiteral("识别图片中的二维码/条码"), QStringLiteral("file"));
    const QCommandLineOption whiteboardOption(QStringLiteral("whiteboard"), QStringLiteral("打开白板"));
    const QCommandLineOption transparentWhiteboardOption(QStringLiteral("transparent-whiteboard"), QStringLiteral("打开透明白板"));
    const QCommandLineOption whiteboardColorOption(QStringLiteral("color"), QStringLiteral("白板颜色"), QStringLiteral("color"));
    const QCommandLineOption plainOption(QStringLiteral("plain"), QStringLiteral("将剪贴板纯文本贴图"));
    const QCommandLineOption filesOption(QStringLiteral("files"), QStringLiteral("将文件列表贴图"));
    const QCommandLineOption configOption(QStringLiteral("config"), QStringLiteral("指定配置文件路径"), QStringLiteral("path"));
    const QCommandLineOption htmlOption(QStringLiteral("html"), QStringLiteral("将 HTML 贴出"), QStringLiteral("html"));
    QCommandLineOption restartOption(QStringLiteral("snipaste-restart"),
                                     QStringLiteral("内部重启标记"));
    restartOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(snipOption);
    parser.addOption(copyOption);
    parser.addOption(pasteOption);
    parser.addOption(clipboardPasteOption);
    parser.addOption(historyOption);
    parser.addOption(fullScreenOption);
    parser.addOption(officialFullOption);
    parser.addOption(officialLastOption);
    parser.addOption(activeWindowOption);
    parser.addOption(activeScreenOption);
    parser.addOption(customOption);
    parser.addOption(holdOption);
    parser.addOption(delayOption);
    parser.addOption(outputOption);
    parser.addOption(blockOption);
    parser.addOption(areaOption);
    parser.addOption(sizeOption);
    parser.addOption(positionOption);
    parser.addOption(keepAspectOption);
    parser.addOption(shadowOption);
    parser.addOption(noShadowOption);
    parser.addOption(windowOption);
    parser.addOption(elementOption);
    parser.addOption(showImagesOption);
    parser.addOption(hideImagesOption);
    parser.addOption(toggleImagesOption);
    parser.addOption(groupManagerOption);
    parser.addOption(emptyGroupOption);
    parser.addOption(switchGroupPrevOption);
    parser.addOption(soloOption);
    parser.addOption(toggleClickThroughOption);
    parser.addOption(noClickThroughOption);
    parser.addOption(disableHotkeysOption);
    parser.addOption(enableHotkeysOption);
    parser.addOption(toggleHotkeysOption);
    parser.addOption(pickColorOption);
    parser.addOption(barcodeDecodeOption);
    parser.addOption(whiteboardOption);
    parser.addOption(transparentWhiteboardOption);
    parser.addOption(whiteboardColorOption);
    parser.addOption(plainOption);
    parser.addOption(filesOption);
    parser.addOption(htmlOption);
    parser.addOption(configOption);
    parser.addPositionalArgument(QStringLiteral("command"), QStringLiteral("附加命令，如 create-group、switch-group、exit"));
    parser.addOption(restartOption);
    parser.process(normalizeMultiValueOptions(QCoreApplication::arguments()));

    snipaste::app::initializeLogging();

    snipaste::platform::SingleInstance instance;
    bool instanceAcquired = instance.acquire(QStringLiteral("snipaste-single-instance"));
    if (!instanceAcquired && QCoreApplication::arguments().contains(QStringLiteral("--snipaste-restart"))) {
        for (int attempt = 0; attempt < 20 && !instanceAcquired; ++attempt) {
            QThread::msleep(100);
            instanceAcquired = instance.acquire(QStringLiteral("snipaste-single-instance"));
        }
    }
    if (!instanceAcquired) {
        // A second launch is a command invocation, not a reason to display a
        // modal error. Forward its arguments to the resident tray instance.
        const QStringList forwardedArguments = QCoreApplication::arguments().mid(1);
        const int blockIndex = forwardedArguments.indexOf(QStringLiteral("--block"));
        const bool waitForCompletion = blockIndex >= 0
            || std::any_of(forwardedArguments.cbegin(), forwardedArguments.cend(),
                           [](const QString &value) { return value.startsWith(QStringLiteral("--block=")); });
        int waitMs = 120000;
        if (waitForCompletion) {
            QString blockValue;
            if (blockIndex >= 0 && blockIndex + 1 < forwardedArguments.size()
                && !forwardedArguments.at(blockIndex + 1).startsWith(QChar('-')))
                blockValue = forwardedArguments.at(blockIndex + 1);
            else {
                for (const QString &value : forwardedArguments)
                    if (value.startsWith(QStringLiteral("--block=")))
                        blockValue = value.mid(QStringLiteral("--block=").size());
            }
            bool ok = false;
            const double seconds = blockValue.toDouble(&ok);
            if (ok && seconds > 0.0)
                waitMs = qBound(1000, qRound(seconds * 1000.0) + 5000, 24 * 60 * 60 * 1000);
        }
        const bool sent = waitForCompletion
            ? instance.sendMessageAndWait(QStringLiteral("snipaste-single-instance"),
                                          forwardedArguments, waitMs)
            : instance.sendMessage(QStringLiteral("snipaste-single-instance"), forwardedArguments);
        if (sent) {
            qInfo() << "another snipaste instance is already running in tray; forwarded arguments and exiting.";
            return 0;
        }
        qWarning() << "another snipaste instance is already running";
        QMessageBox::information(nullptr,
                                 QStringLiteral("snipaste 已在运行"),
                                 QStringLiteral("snipaste 已经在系统托盘中运行。\n"
                                                "请右键托盘图标打开菜单。"));
        return 0;
    }

    const QString configPath = parser.isSet(configOption)
                                   ? QFileInfo(parser.value(configOption)).absoluteFilePath()
                                   : QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
                                         + QStringLiteral("/config.ini");
    snipaste::core::ConfigStore config(configPath);
    config.load();
    applyConfiguredTheme(
        config.value(QStringLiteral("interface/theme"), QStringLiteral("跟随系统")).toString());
    snipaste::ui::applyApplicationAppearance(config);
    applyConfiguredProcessPriority(config);
    QApplication::setDoubleClickInterval(qBound(100,
                                                 config.value(QStringLiteral("misc/double_tap_interval"),
                                                              config.value(QStringLiteral("double_tap_interval"), 500)).toInt(),
                                                 2000));

    snipaste::core::CommandRegistry commands;
    snipaste::core::ScreenshotHistory history(
        qBound(1, config.value(QStringLiteral("capture/maxHistory"), 20).toInt(), 999));
    QString historyPath = config.value(QStringLiteral("general/history_dir"),
                                       config.value(QStringLiteral("history_dir"))).toString().trimmed();
    if (!historyPath.isEmpty()) {
        historyPath = expandEnvironmentVariables(historyPath);
        if (QFileInfo(historyPath).isRelative())
            historyPath = QDir(QFileInfo(config.filePath()).absolutePath()).filePath(historyPath);
        historyPath = QDir::cleanPath(historyPath);
    } else {
        historyPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                      + QStringLiteral("/history");
    }
    // Loading every historical screenshot at startup keeps up to hundreds of
    // megabytes of decoded pixels resident even though the tray is idle.  The
    // history is populated on first use instead (startSnip/history.open or a
    // direct screenshot command).
    bool historyLoaded = false;
    const auto ensureHistoryLoaded = [&history, &historyPath, &historyLoaded] {
        if (historyLoaded)
            return;
        history.loadFromDirectory(historyPath);
        historyLoaded = true;
    };
    snipaste::ui::PasteManager pasteManager;
    const auto configuredPasteMouseActions = [&config] {
        QStringList actions;
        actions.reserve(18);
        for (int index = 0; index < 18; ++index)
            actions.append(config.value(QStringLiteral("control/mouseAction%1").arg(index)).toString());
        return actions;
    };
    pasteManager.setRestoreCount(config.value(QStringLiteral("paste/restoreCount"), 1).toInt());
    pasteManager.setUseCapturePosition(config.value(QStringLiteral("paste/useCapturePosition"), true).toBool());
    pasteManager.setDefaultOpacity(configuredPasteOpacity(config));
    pasteManager.setShadowEnabled(config.value(QStringLiteral("paste/shadow"), true).toBool());
    pasteManager.setResizeEnabled(!config.value(QStringLiteral("paste/disable_frame_resize"), false).toBool());
    pasteManager.setMagneticAttachThreshold(config.value(QStringLiteral("paste/magnetic_attach_threshold"),
                                                         config.value(QStringLiteral("magnetic_attach_threshold"), 50)).toInt());
    pasteManager.setExchangeNavigationKeys(
        config.value(QStringLiteral("hotkeys/exchange_wasd_and_arrow_keys"), false).toBool());
    const qreal maxDownloadMb = config.value(QStringLiteral("misc/auto_download_max_image_size_mb"),
                                             config.value(QStringLiteral("auto_download_max_image_size_mb"), 20.0)).toDouble();
    pasteManager.setMaxNetworkImageBytes(maxDownloadMb <= 0.0
                                             ? 0
                                             : qRound64(maxDownloadMb * 1024.0 * 1024.0));
    pasteManager.setCopyAsFile(config.value(QStringLiteral("output/copyAsFileMode"), 2).toInt() == 0);
    pasteManager.setBackgroundMode(
        config.value(QStringLiteral("paste/backgroundMode"), QStringLiteral("伪透明")).toString());
    pasteManager.setSmoothScale(config.value(QStringLiteral("paste/smoothScale"), true).toBool());
    pasteManager.setVirtualDesktopPinned(config.value(QStringLiteral("paste/virtualDesktop"), false).toBool());
    pasteManager.setEscapeActions(
        config.value(QStringLiteral("paste/escapeAction"), QStringLiteral("关闭贴图")).toString(),
        config.value(QStringLiteral("paste/shiftEscapeAction"), QStringLiteral("销毁贴图")).toString());
    pasteManager.setMouseActions(configuredPasteMouseActions());
    const QString pasteStatePath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                                   + QStringLiteral("/pastes");
    pasteManager.loadState(pasteStatePath);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&pasteManager, &pasteStatePath] {
        pasteManager.saveState(pasteStatePath);
    });
    QRect lastSelection = config.value(QStringLiteral("capture/lastRegion")).toRect();
    QList<QRect> recentSelections;
    const int maxHistoryRegions = qBound(1,
                                         config.value(QStringLiteral("capture/maxHistoryRegions"), 8).toInt(),
                                         99);
    for (const QVariant &region : config.value(QStringLiteral("capture/recentRegions")).toList())
        if (region.toRect().isValid()) recentSelections.append(region.toRect());
    if (recentSelections.isEmpty() && lastSelection.isValid()) recentSelections.append(lastSelection);
    const auto recordScreenshot = [&history, &historyPath, &ensureHistoryLoaded](
                                      const QImage &image,
                                      const QImage &background = {}) {
        ensureHistoryLoaded();
        history.add(image, background);
        saveHistorySnapshotAsync(history, historyPath);
    };
    const auto autoSaveScreenshot = [&config](const QImage &image) {
        if (!config.value(QStringLiteral("capture/autoSave"), false).toBool()
            || image.isNull())
            return;
        QString directory = config.value(QStringLiteral("output/autoSaveDirectory"),
                                          config.value(QStringLiteral("output/saveDirectory")))
                                 .toString();
        if (directory.isEmpty())
            directory = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
        const QString pattern = config.value(QStringLiteral("output/fileNamePattern"),
                                              QStringLiteral("Snipaste_$yyyy-MM-dd_HH-mm-ss$.png"))
                                    .toString();
        const QString fileName = expandOutputFilePattern(pattern, QDateTime::currentDateTime());
        const bool removeAlpha = config.value(QStringLiteral("output/auto_alpha_channel_removal"),
                                              config.value(QStringLiteral("auto_alpha_channel_removal"), true)).toBool();
        const bool softwareMetadata = config.value(QStringLiteral("misc/software_metadata"),
                                                   config.value(QStringLiteral("software_metadata"), true)).toBool();
        const QImage source = image;
        QtConcurrent::run([source, directory, fileName, removeAlpha, softwareMetadata] {
            if (!QDir().mkpath(directory))
                return;
            QImage output = addSoftwareMetadata(removeRedundantAlpha(source, removeAlpha), softwareMetadata);
            if (!output.save(QDir(directory).filePath(fileName), "PNG"))
                qWarning() << "failed to auto-save screenshot" << directory;
        });
    };
    QPointer<snipaste::ui::SnipOverlay> activeOverlay;
    QPointer<snipaste::ui::WindowCaptureOverlay> activeWindowOverlay;
    const auto showPaste = [&pasteManager](const QImage &image) {
        // Let the capture overlay process its close/repaint events first.  A
        // top-level paste window performs native window setup and image
        // scaling; doing that directly inside the toolbar click makes the
        // click visibly pause and leaves the selection overlay on screen.
        const QImage copy = image;
        QTimer::singleShot(0, &pasteManager, [&pasteManager, copy] {
            if (!copy.isNull())
                pasteManager.createPaste(copy);
        });
    };
    const auto outputCapture = [&config, &pasteManager](const QImage &image, const QString &spec) {
        if (image.isNull())
            return;
        const QStringList outputs = splitOutputSpec(spec);
        const bool shadow = outputs.contains(QStringLiteral("shadow"));
        const bool noShadow = outputs.contains(QStringLiteral("no-shadow"));
        QImage normalizedImage = removeRedundantAlpha(
            image,
            config.value(QStringLiteral("output/auto_alpha_channel_removal"),
                         config.value(QStringLiteral("auto_alpha_channel_removal"), true)).toBool());
        QImage outputImage = shadow && !noShadow ? addDropShadow(normalizedImage) : normalizedImage;
        outputImage = addSoftwareMetadata(outputImage,
                                           config.value(QStringLiteral("misc/software_metadata"),
                                                        config.value(QStringLiteral("software_metadata"), true)).toBool());
        const auto runOutput = [&](const QString &output) {
            if (output == QStringLiteral("clipboard")) {
                QApplication::clipboard()->setImage(outputImage);
            } else if (output == QStringLiteral("pin")) {
                pasteManager.createPaste(outputImage);
            } else if (output == QStringLiteral("ocr_clipboard")) {
                const auto result = snipaste::core::OcrService::recognize(outputImage);
                QApplication::clipboard()->setText(result.success ? result.text : QString());
            } else if (output == QStringLiteral("quick-save")) {
                QString directory = config.value(QStringLiteral("output/saveDirectory"),
                                                  QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).toString();
                if (directory.isEmpty())
                    directory = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
                QDir().mkpath(directory);
                const QString pattern = config.value(QStringLiteral("output/fileNamePattern"),
                                                      QStringLiteral("Snipaste_$yyyy-MM-dd_HH-mm-ss$.png")).toString();
                outputImage.save(QDir(directory).filePath(expandOutputFilePattern(pattern,
                                                                                  QDateTime::currentDateTime())), "PNG");
            } else if (output == QStringLiteral("file-dialog")) {
                const bool hideForDialog = config.value(QStringLiteral("paste/hideOnFileDialog"), false).toBool();
                if (hideForDialog)
                    pasteManager.setHiddenForFileDialog(true);
                const QString path = QFileDialog::getSaveFileName(nullptr,
                                                                    QStringLiteral("保存截图"),
                                                                    QStringLiteral("snipaste.png"),
                                                                    QStringLiteral("PNG 图片 (*.png);;JPEG 图片 (*.jpg *.jpeg);;所有文件 (*.*)"));
                if (!path.isEmpty())
                    outputImage.save(path);
                if (hideForDialog)
                    pasteManager.setHiddenForFileDialog(false);
            } else if (output == QStringLiteral("printer")) {
                if (!printImage(outputImage))
                    qInfo() << "printer output cancelled or unavailable";
            } else if (output.startsWith(QStringLiteral("exec(")) && output.endsWith(QChar(')'))) {
                QString command = output.mid(5, output.size() - 6);
                const QString tempPath = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                             .filePath(QStringLiteral("snipaste-%1.png")
                                                           .arg(QUuid::createUuid().toString(QUuid::Id128)));
                if (outputImage.save(tempPath, "PNG")) {
                    command.replace(QStringLiteral("%file%"), QDir::toNativeSeparators(tempPath));
                    command.replace(QStringLiteral("%FILE%"), QDir::toNativeSeparators(tempPath));
#ifdef Q_OS_WIN
                    QProcess::startDetached(QStringLiteral("cmd.exe"), {QStringLiteral("/c"), command});
#else
                    QProcess::startDetached(QStringLiteral("sh"), {QStringLiteral("-c"), command});
#endif
                }
            } else {
                const QString path = outputFilePath(output, config);
                if (!path.isEmpty()) {
                    QDir().mkpath(QFileInfo(path).absolutePath());
                    outputImage.save(path);
                }
            }
        };
        const auto isExec = [](const QString &value) {
            return value.startsWith(QStringLiteral("exec(")) && value.endsWith(QChar(')'));
        };
        const auto isFile = [&](const QString &value) {
            return !value.isEmpty() && value != QStringLiteral("clipboard")
                   && value != QStringLiteral("pin") && value != QStringLiteral("ocr_clipboard")
                   && value != QStringLiteral("quick-save") && value != QStringLiteral("file-dialog")
                   && value != QStringLiteral("printer") && value != QStringLiteral("success")
                   && value != QStringLiteral("silent") && value != QStringLiteral("no-auto-save")
                   && value != QStringLiteral("shadow") && value != QStringLiteral("no-shadow")
                   && !isExec(value);
        };
        for (const QString &raw : outputs) {
            const QString output = raw.trimmed();
            if (isFile(output))
                runOutput(output);
        }
        const QStringList orderedTokens = {QStringLiteral("file-dialog"), QStringLiteral("printer"),
                                           QStringLiteral("quick-save"), QStringLiteral("clipboard"),
                                           QStringLiteral("pin"), QStringLiteral("ocr_clipboard"),
                                           QStringLiteral("success")};
        for (const QString &token : orderedTokens)
            for (const QString &raw : outputs)
                if (raw.trimmed() == token)
                    runOutput(token);
        for (const QString &raw : outputs)
            if (isExec(raw.trimmed()))
                runOutput(raw.trimmed());
    };
    const auto startSnip = [&app,
                            &activeOverlay,
                            &history,
                            &historyPath,
                            ensureHistoryLoaded,
                            &lastSelection,
                            &recentSelections,
                            &config,
                            &pasteManager,
                            maxHistoryRegions,
                            recordScreenshot,
                            autoSaveScreenshot,
                            showPaste,
                             outputCapture](bool autoCopy, bool reuseLast, QSize fixedSize = {}, QRect requestedSelection = {},
                                            bool pinAfterCapture = false, QString outputSpec = {}, bool hold = false,
                                            bool noAutoSave = false, bool keepAspect = false) {
        if (activeOverlay) {
            activeOverlay->raise();
            activeOverlay->activateWindow();
            return;
        }
        ensureHistoryLoaded();
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
        if (!ensureMacScreenCaptureAccess(QStringLiteral("截图")))
            return;
#endif

        QScreen *screen = requestedSelection.isValid()
                              ? QGuiApplication::screenAt(requestedSelection.center())
                              : reuseLast && lastSelection.isValid()
                              ? QGuiApplication::screenAt(lastSelection.center())
                              : QGuiApplication::screenAt(QCursor::pos());
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        if (!screen)
            return;

        QRect initialSelection;
        if (requestedSelection.isValid() || (reuseLast && lastSelection.isValid())) {
            const QRect source = requestedSelection.isValid() ? requestedSelection : lastSelection;
            const QRect local = source.translated(-screen->geometry().topLeft());
            initialSelection = local.intersected(screen->geometry().translated(-screen->geometry().topLeft()));
        }

        activeOverlay = new snipaste::ui::SnipOverlay(screen,
                                                     autoCopy,
                                                     config.value(QStringLiteral("capture/cursor"),
                                                                  false)
                                                         .toBool(),
                                                     initialSelection);
        QObject::connect(activeOverlay, &QObject::destroyed, &app, [] {
            QTimer::singleShot(250, qApp, [] { trimIdleProcessMemory(); });
        });
        activeOverlay->setFixedSelectionSize(fixedSize);
        activeOverlay->setHoldAfterCapture(hold);
        activeOverlay->setKeepAspectRatio(keepAspect);
        const bool captureDisabled = config.value(QStringLiteral("capture/disable"), false).toBool();
        activeOverlay->setDetectionOptions(
            !captureDisabled && config.value(QStringLiteral("capture/autoDetectWindow"), true).toBool(),
            !captureDisabled && config.value(QStringLiteral("capture/autoDetectElements"), true).toBool(),
            !captureDisabled && config.value(QStringLiteral("capture/shortcutHints"), true).toBool());
        activeOverlay->setDetectionProcessFilters(
            configuredProcessList(config, QStringLiteral("snip/exclude_from_detection"),
                                  QStringLiteral("exclude_from_detection")),
            configuredProcessList(config, QStringLiteral("snip/crashes"),
                                  QStringLiteral("crashes")));
        activeOverlay->setExitOnFocusLoss(
            config.value(QStringLiteral("capture/exitOnFocus"), true).toBool());
        activeOverlay->setWheelEnabled(
            !config.value(QStringLiteral("capture/disableWheel"), false).toBool());
        activeOverlay->setConfirmEscape(
            config.value(QStringLiteral("capture/confirmEsc"), false).toBool());
        activeOverlay->setHistoryCycling(
            config.value(QStringLiteral("capture/historyCycle"), false).toBool());
        activeOverlay->setIncludeUnsavedHistory(
            config.value(QStringLiteral("capture/includeUnsaved"), false).toBool());
        activeOverlay->setAnchorThreshold(
            config.value(QStringLiteral("snip/anchors_threshold"),
                         config.value(QStringLiteral("anchors_threshold"), 80)).toInt());
        activeOverlay->setUppercaseHexColor(
            config.value(QStringLiteral("misc/uppercase_hex_color"),
                         config.value(QStringLiteral("uppercase_hex_color"), true)).toBool());
        activeOverlay->setHideOutputOptions(
            config.value(QStringLiteral("interface/hide_output_options"),
                         config.value(QStringLiteral("hide_output_options"), false)).toBool());
        activeOverlay->setQuickSaveDirectory(
            config.value(QStringLiteral("output/saveDirectory"),
                         QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).toString());
        activeOverlay->setAutoSelectAnnotations(
            config.value(QStringLiteral("annotate/auto_select_on_creation"),
                         config.value(QStringLiteral("auto_select_on_creation"), true)).toBool());
        activeOverlay->setPenTipCount(
            config.value(QStringLiteral("interface/toolbar/penCount"), 3).toInt());
        const QStringList annotationPalette = config.value(
            QStringLiteral("interface/toolbar/palette"), QStringList()).toStringList();
        if (!annotationPalette.isEmpty())
            activeOverlay->setAnnotationPalette(annotationPalette);
        activeOverlay->setMouseActions(
            config.value(QStringLiteral("capture/doubleClickAction"), QStringLiteral("复制到剪贴板")).toString(),
            config.value(QStringLiteral("capture/doubleClickExit"), true).toBool(),
            config.value(QStringLiteral("capture/middleClickAction"), QStringLiteral("贴到屏幕")).toString(),
            config.value(QStringLiteral("capture/middleClickExit"), true).toBool(),
            config.value(QStringLiteral("capture/enterAction"), QStringLiteral("复制到剪贴板")).toString(),
            config.value(QStringLiteral("capture/enterExit"), true).toBool());
        activeOverlay->setExchangeNavigationKeys(
            config.value(QStringLiteral("hotkeys/exchange_wasd_and_arrow_keys"), false).toBool());
        activeOverlay->setLastSelection(lastSelection.translated(-screen->geometry().topLeft()));
        QList<QRect> localRegions;
        for (const QRect &region : recentSelections)
            localRegions.append(region.translated(-screen->geometry().topLeft()));
        activeOverlay->setSelectionHistory(localRegions);
        activeOverlay->setCaptureHistory(
            history.images(), localRegions, history.backgrounds(),
            [&history](int index) { return history.backgroundAt(index); },
            [&history](int index) { return history.imageAt(index); });
        QObject::connect(activeOverlay,
                         &snipaste::ui::SnipOverlay::selectionCaptured,
                         [&lastSelection, &recentSelections, &config, maxHistoryRegions](const QRect &globalRect) {
                             lastSelection = globalRect;
                             recentSelections.removeAll(globalRect);
                             recentSelections.prepend(globalRect);
                             while (recentSelections.size() > maxHistoryRegions) recentSelections.removeLast();
                             QVariantList regions;
                             for (const QRect &region : recentSelections) regions.append(region);
                             config.setValue(QStringLiteral("capture/recentRegions"), regions);
                             config.setValue(QStringLiteral("capture/lastRegion"), globalRect);
                             config.save();
                         });
        QObject::connect(activeOverlay,
                         &snipaste::ui::SnipOverlay::screenshotCapturedWithBackground,
                         [&pasteManager, &lastSelection, recordScreenshot](const QImage &image,
                                                                            const QImage &background) {
                             pasteManager.rememberCapture(image, lastSelection);
                             recordScreenshot(image, background);
                         });
        if (!noAutoSave) {
            QObject::connect(activeOverlay,
                             &snipaste::ui::SnipOverlay::screenshotCaptured,
                             autoSaveScreenshot);
        }
        QObject::connect(activeOverlay,
                         &snipaste::ui::SnipOverlay::unsavedScreenshotCapturedWithBackground,
                         [&config, recordScreenshot](const QImage &image, const QImage &background) {
                             if (config.value(QStringLiteral("capture/includeUnsaved"), false).toBool())
                                 recordScreenshot(image, background);
                         });
        if (!outputSpec.isEmpty()) {
            QObject::connect(activeOverlay,
                             &snipaste::ui::SnipOverlay::screenshotCaptured,
                             [outputCapture, outputSpec](const QImage &image) {
                                 outputCapture(image, outputSpec);
                             });
        }
        if (pinAfterCapture) {
            QObject::connect(activeOverlay,
                             &snipaste::ui::SnipOverlay::screenshotCaptured,
                             showPaste);
        }
        QObject::connect(activeOverlay,
                         &snipaste::ui::SnipOverlay::pasteRequested,
                         showPaste);
        QObject::connect(activeOverlay,
                         &snipaste::ui::SnipOverlay::annotateRequested,
                         [&history, &historyPath, &pasteManager, &lastSelection, autoSaveScreenshot](const QImage &image) {
                             const QRect captureRegion = lastSelection;
                             snipaste::ui::AnnotationDialog dialog(image);
                             if (dialog.exec() != QDialog::Accepted)
                                 return;
                             const QImage annotated = dialog.resultImage();
                             history.add(annotated);
                             saveHistorySnapshotAsync(history, historyPath);
                             autoSaveScreenshot(annotated);
                             pasteManager.rememberCapture(annotated, captureRegion);
                             QApplication::clipboard()->setImage(annotated);
                             pasteManager.createPaste(annotated);
                         });
        QObject::connect(activeOverlay,
                         &snipaste::ui::SnipOverlay::ocrRequested,
                         [](const QImage &image) {
                             auto *watcher = new QFutureWatcher<snipaste::core::OcrResult>(qApp);
                             QObject::connect(watcher,
                                              &QFutureWatcher<snipaste::core::OcrResult>::finished,
                                              watcher,
                                              [watcher] {
                                                  const snipaste::core::OcrResult result = watcher->result();
                                                  watcher->deleteLater();
                                                  if (result.success) {
                                                      QApplication::clipboard()->setText(result.text);
                                                      QMessageBox::information(nullptr,
                                                                               QStringLiteral("OCR \u7ed3\u679c"),
                                                                               result.text);
                                                  } else {
                                                      QMessageBox::warning(nullptr,
                                                                            QStringLiteral("OCR \u5931\u8d25"),
                                                                            result.error);
                                                  }
                                              });
                             watcher->setFuture(QtConcurrent::run([image] {
                                 return snipaste::core::OcrService::recognize(image);
                             }));
                         });
        QObject::connect(activeOverlay,
                         &snipaste::ui::SnipOverlay::barcodeRequested,
                         [](const QImage &image) {
                             const snipaste::core::BarcodeResult result =
                                 snipaste::core::BarcodeService::recognize(image);
                             if (result.success) {
                                 QApplication::clipboard()->setText(result.text);
                                 QMessageBox::information(nullptr,
                                                          QStringLiteral("二维码/条码结果"),
                                                          result.text);
                             } else {
                                 QMessageBox::warning(nullptr,
                                                       QStringLiteral("二维码/条码识别失败"),
                                                       result.error);
                             }
                         });
        QObject::connect(activeOverlay,
                         &snipaste::ui::SnipOverlay::printRequested,
                         [](const QImage &image) {
                             if (!printImage(image))
                                 qInfo() << "printer output cancelled or unavailable";
                         });
        activeOverlay->show();
        if (autoCopy && !initialSelection.isEmpty()) {
            auto *overlay = activeOverlay.data();
            QTimer::singleShot(0, overlay, [overlay] { overlay->captureSelectionImmediately(); });
        }
    };

    snipaste::platform::SuperCapture superCapture(&app);
    const auto configureSuperCapture = [&config, &superCapture] {
        const QString modifier = config.value(QStringLiteral("control/superCaptureModifier"), QStringLiteral("none")).toString();
        Qt::KeyboardModifier selectedModifier = Qt::NoModifier;
        if (modifier.contains(QStringLiteral("Cmd"), Qt::CaseInsensitive)
            || modifier.contains(QStringLiteral("Meta"), Qt::CaseInsensitive)
            || modifier.contains(QStringLiteral("Command"), Qt::CaseInsensitive))
            selectedModifier = Qt::MetaModifier;
        else if (modifier.contains(QStringLiteral("Ctrl"), Qt::CaseInsensitive))
            selectedModifier = Qt::ControlModifier;
        else if (modifier.contains(QStringLiteral("Shift"), Qt::CaseInsensitive))
            selectedModifier = Qt::ShiftModifier;
        else if (modifier.contains(QStringLiteral("Alt"), Qt::CaseInsensitive))
            selectedModifier = Qt::AltModifier;
        superCapture.setModifier(selectedModifier);
        const QString mouse = config.value(QStringLiteral("control/superCaptureMouse"), QStringLiteral("left")).toString();
        superCapture.setMouseButton(mouse.contains(QStringLiteral("right"), Qt::CaseInsensitive)
                                    || mouse == QStringLiteral("2")
                                    ? Qt::RightButton : Qt::LeftButton);
        // The reference UI uses “none” as the disabled state. Keep an
        // explicit boolean for users who edit config.ini directly.
        const bool enabled = config.value(QStringLiteral("control/superCaptureEnabled"), false).toBool()
            || selectedModifier != Qt::NoModifier;
        superCapture.setEnabled(enabled);
    };
    QObject::connect(&superCapture, &snipaste::platform::SuperCapture::rectangleCaptured,
                     &app, [startSnip](const QRect &globalRect) {
                         startSnip(true, false, {}, globalRect, true);
                     });
    configureSuperCapture();

    snipaste::platform::GlobalHotkey screenshotHotkey;
    QObject::connect(&screenshotHotkey,
                     &snipaste::platform::GlobalHotkey::triggered,
                     [&commands, &config](int) {
                         if (!isHotkeyIgnored(config))
                             commands.execute(QStringLiteral("snip.start"));
                     });
    snipaste::platform::GlobalHotkey screenshotCopyHotkey;
    QObject::connect(&screenshotCopyHotkey,
                     &snipaste::platform::GlobalHotkey::triggered,
                     [&commands, &config](int) {
                         if (!isHotkeyIgnored(config))
                             commands.execute(QStringLiteral("snip.copy"));
                     });
    snipaste::platform::GlobalHotkey pasteHotkey;
    QObject::connect(&pasteHotkey,
                     &snipaste::platform::GlobalHotkey::triggered,
                     [&commands, &config](int) {
                         if (!isHotkeyIgnored(config))
                             commands.execute(QStringLiteral("paste.clipboard"));
                     });
    snipaste::platform::GlobalHotkey customSnipHotkey;
    QObject::connect(&customSnipHotkey,
                     &snipaste::platform::GlobalHotkey::triggered,
                     [&commands, &config](int) {
                         if (!isHotkeyIgnored(config)) commands.execute(QStringLiteral("snip.custom"));
                     });
    snipaste::platform::GlobalHotkey toggleImagesHotkey;
    QObject::connect(&toggleImagesHotkey,
                     &snipaste::platform::GlobalHotkey::triggered,
                     [&commands, &config](int) {
                         if (!isHotkeyIgnored(config)) commands.execute(QStringLiteral("paste.toggleAll"));
                     });
    snipaste::platform::GlobalHotkey switchGroupHotkey;
    QObject::connect(&switchGroupHotkey,
                     &snipaste::platform::GlobalHotkey::triggered,
                     [&commands, &config](int) {
                         if (!isHotkeyIgnored(config)) commands.execute(QStringLiteral("paste.switchGroup"));
                     });
    snipaste::platform::GlobalHotkey clickThroughHotkey;
    QObject::connect(&clickThroughHotkey,
                     &snipaste::platform::GlobalHotkey::triggered,
                     [&commands, &config](int) {
                         if (!isHotkeyIgnored(config)) commands.execute(QStringLiteral("paste.toggleClickThrough"));
                     });
    bool hotkeysEnabled = true;
    const auto applyHotkeys = [&config,
                               &screenshotHotkey,
                               &screenshotCopyHotkey,
                               &pasteHotkey,
                               &customSnipHotkey,
                               &toggleImagesHotkey,
                               &switchGroupHotkey,
                               &clickThroughHotkey,
                               &hotkeysEnabled] {
        const auto registerConfigured = [&config](const QString &key,
                                                  const QString &fallback,
                                                  snipaste::platform::GlobalHotkey &hotkey,
                                                  int id) {
            const QString value = config.value(key, fallback).toString();
            const HotkeySpec spec = parseHotkey(value);
            if (!spec.valid) {
                hotkey.clearKey();
                qWarning() << "invalid or empty global hotkey" << key << value;
                return;
            }
            if (!hotkey.registerKey(id, spec.modifiers, spec.key))
                qWarning() << "global hotkey unavailable" << key << value;
        };
        registerConfigured(QStringLiteral("hotkeys/snip"), QStringLiteral("F1"), screenshotHotkey, 1);
        registerConfigured(QStringLiteral("hotkeys/copy"),
                           snipaste::platform::primaryPortableShortcutText(QStringLiteral("F1")),
                           screenshotCopyHotkey, 3);
        registerConfigured(QStringLiteral("hotkeys/paste"), QStringLiteral("F3"), pasteHotkey, 2);
        registerConfigured(QStringLiteral("hotkeys/custom"), QStringLiteral("Shift+F1"), customSnipHotkey, 4);
        registerConfigured(QStringLiteral("hotkeys/toggleImages"), QStringLiteral("Shift+F3"), toggleImagesHotkey, 5);
        registerConfigured(QStringLiteral("hotkeys/switchGroup"),
                           snipaste::platform::primaryPortableShortcutText(QStringLiteral("F3")),
                           switchGroupHotkey, 6);
        registerConfigured(QStringLiteral("hotkeys/clickThrough"), QString(), clickThroughHotkey, 7);
        if (!hotkeysEnabled) {
            screenshotHotkey.setEnabled(false);
            screenshotCopyHotkey.setEnabled(false);
            pasteHotkey.setEnabled(false);
            customSnipHotkey.setEnabled(false);
            toggleImagesHotkey.setEnabled(false);
            switchGroupHotkey.setEnabled(false);
            clickThroughHotkey.setEnabled(false);
        }
    };
    applyHotkeys();

    QString lastPastedFileClipboard;
    commands.registerCommand({
        QStringLiteral("snip.start"),
        QStringLiteral("开始截图"),
        [startSnip] { startSnip(false, false); },
    });
    commands.registerCommand({
        QStringLiteral("snip.copy"),
        QStringLiteral("截屏并自动复制"),
        [startSnip] { startSnip(true, false); },
    });
    commands.registerCommand({
        QStringLiteral("snip.fullScreen"),
        QStringLiteral("所有屏幕全屏并复制"),
        [recordScreenshot, autoSaveScreenshot, &pasteManager] {
            const QImage image = captureAllScreens();
            if (image.isNull())
                return;
            recordScreenshot(image, image);
            autoSaveScreenshot(image);
            QApplication::clipboard()->setImage(image);
            pasteManager.createPaste(image);
        },
    });
    commands.registerCommand({
        QStringLiteral("snip.activeScreen"),
        QStringLiteral("截取当前屏幕"),
        [recordScreenshot, autoSaveScreenshot, &pasteManager] {
            if (!ensureMacScreenCaptureAccess(QStringLiteral("当前屏幕截图")))
                return;
            QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
            if (!screen)
                screen = QGuiApplication::primaryScreen();
            if (!screen)
                return;
            QImage image = screen->grabWindow(0).toImage();
            image.setDevicePixelRatio(1.0);
            if (image.isNull())
                return;
            recordScreenshot(image, image);
            autoSaveScreenshot(image);
            QApplication::clipboard()->setImage(image);
            pasteManager.createPaste(image);
        },
    });
    const auto startWindowCapture = [&app,
                                     &activeWindowOverlay,
                                     &recordScreenshot,
                                     &autoSaveScreenshot,
                                     &outputCapture](bool elementMode, QString outputSpec = {},
                                                    bool noAutoSave = false) {
        if (activeWindowOverlay) {
            activeWindowOverlay->raise();
            activeWindowOverlay->activateWindow();
            return;
        }
        QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        if (!screen)
            return;
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
        if (!ensureMacScreenCaptureAccess(QStringLiteral("窗口截图")))
            return;
#endif
        activeWindowOverlay = new snipaste::ui::WindowCaptureOverlay(screen, elementMode);
        QObject::connect(activeWindowOverlay, &QObject::destroyed, &app, [] {
            QTimer::singleShot(250, qApp, [] { trimIdleProcessMemory(); });
        });
        QObject::connect(activeWindowOverlay,
                         &snipaste::ui::WindowCaptureOverlay::screenshotCaptured,
                         [recordScreenshot, autoSaveScreenshot, outputCapture,
                          outputSpec, noAutoSave](const QImage &image) {
                             recordScreenshot(image);
                             if (!noAutoSave)
                                 autoSaveScreenshot(image);
                             if (outputSpec.isEmpty()) {
                                 QApplication::clipboard()->setImage(image);
                             } else {
                                 outputCapture(image, outputSpec);
                             }
                         });
        activeWindowOverlay->show();
    };
    commands.registerCommand({
        QStringLiteral("snip.window"),
        QStringLiteral("窗口截图"),
        [startWindowCapture] { startWindowCapture(false); },
    });
    commands.registerCommand({
        QStringLiteral("snip.element"),
        QStringLiteral("界面元素截图"),
        [startWindowCapture] { startWindowCapture(true); },
    });
    commands.registerCommand({
        QStringLiteral("snip.last"),
        QStringLiteral("重复上次截屏区域"),
        [startSnip] { startSnip(false, true); },
    });
    commands.registerCommand({
        QStringLiteral("snip.custom"),
        QStringLiteral("自定义截屏"),
        [startSnip,
         &commands,
         &history,
         &historyPath,
         &pasteManager,
         recordScreenshot,
         autoSaveScreenshot] {
            QMenu menu;
            QAction *regionAction = menu.addAction(QStringLiteral("当前屏幕区域"));
            QAction *delayedAction = menu.addAction(QStringLiteral("延时 3 秒后截图"));
            QAction *fixedSizeAction = menu.addAction(QStringLiteral("固定尺寸区域"));
            QAction *fullScreenAction = menu.addAction(QStringLiteral("当前屏幕全屏并复制"));
            QAction *allScreensAction = menu.addAction(QStringLiteral("所有屏幕全屏并复制"));
            menu.addSeparator();
            QAction *lastRegionAction = menu.addAction(QStringLiteral("重复上次截屏区域"));
            QAction *windowAction = menu.addAction(QStringLiteral("窗口截图"));
            QAction *elementAction = menu.addAction(QStringLiteral("界面元素截图"));
            QAction *historyAction = menu.addAction(QStringLiteral("打开截屏历史"));
            QAction *selected = menu.exec(QCursor::pos());
            if (selected == regionAction) {
                startSnip(false, false);
            } else if (selected == delayedAction) {
                QTimer::singleShot(3000, [startSnip] { startSnip(false, false); });
            } else if (selected == fixedSizeAction) {
                bool widthOk = false;
                const int width = QInputDialog::getInt(nullptr,
                                                        QStringLiteral("固定尺寸截图"),
                                                        QStringLiteral("宽度（像素）:"),
                                                        800,
                                                        16,
                                                        10000,
                                                        1,
                                                        &widthOk);
                if (!widthOk)
                    return;
                bool heightOk = false;
                const int height = QInputDialog::getInt(nullptr,
                                                         QStringLiteral("固定尺寸截图"),
                                                         QStringLiteral("高度（像素）:"),
                                                         600,
                                                         16,
                                                         10000,
                                                         1,
                                                         &heightOk);
                if (heightOk)
                    startSnip(false, false, QSize(width, height));
            } else if (selected == fullScreenAction) {
                if (!ensureMacScreenCaptureAccess(QStringLiteral("当前屏幕截图")))
                    return;
                QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
                if (!screen)
                    screen = QGuiApplication::primaryScreen();
                if (!screen)
                    return;
                QImage image = screen->grabWindow(0).toImage();
                image.setDevicePixelRatio(1.0);
                pasteManager.rememberCapture(image, screen->geometry());
                recordScreenshot(image, image);
                autoSaveScreenshot(image);
                QApplication::clipboard()->setImage(image);
                pasteManager.createPaste(image);
            } else if (selected == allScreensAction) {
                commands.execute(QStringLiteral("snip.fullScreen"));
            } else if (selected == lastRegionAction) {
                commands.execute(QStringLiteral("snip.last"));
            } else if (selected == windowAction) {
                commands.execute(QStringLiteral("snip.window"));
            } else if (selected == elementAction) {
                commands.execute(QStringLiteral("snip.element"));
            } else if (selected == historyAction) {
                commands.execute(QStringLiteral("history.open"));
            }
        },
    });
    commands.registerCommand({
        QStringLiteral("paste.clipboard"),
        QStringLiteral("贴图"),
        [&pasteManager, &config, &lastPastedFileClipboard] {
            QImage image = QApplication::clipboard()->image();
            QString sourceText;
            const QMimeData *mime = QApplication::clipboard()->mimeData();
            if (image.isNull()) {
                if (mime && mime->hasUrls()) {
                    QStringList paths;
                    for (const QUrl &url : mime->urls())
                        paths.append(url.toLocalFile().isEmpty() ? url.toString()
                                                                : url.toLocalFile());
                    for (const QString &path : paths) {
                        if (QFileInfo::exists(path)) {
                            QImage candidate(path);
                            if (!candidate.isNull()) {
                                image = std::move(candidate);
                                break;
                            }
                        }
                    }
                    if (!image.isNull() && !paths.isEmpty()) {
                        const QString joined = paths.join(QChar::LineFeed);
                        if (joined == lastPastedFileClipboard) {
                            sourceText = joined;
                            image = clipboardTextImage(joined);
                            lastPastedFileClipboard.clear();
                        } else {
                            lastPastedFileClipboard = joined;
                        }
                    } else if (image.isNull() && !paths.isEmpty()) {
                        sourceText = paths.join(QChar::LineFeed);
                        image = clipboardTextImage(sourceText);
                    }
                }
                if (image.isNull() && mime && mime->hasHtml()
                    && !config.value(QStringLiteral("paste/ignoreHtml"), false).toBool()) {
                    image = clipboardHtmlImage(
                        mime->html(),
                        config.value(QStringLiteral("paste/force_html_wrap"), true).toBool());
                }
                if (image.isNull() && mime && mime->hasText()) {
                    const QString text = mime->text();
                    const QColor color = parseClipboardColor(text);
                    if (color.isValid())
                        image = clipboardColorImage(color);
                    else {
                        sourceText = text;
                        image = clipboardTextImage(text);
                    }
                }
            }
            auto *window = pasteManager.pasteClipboard(image);
            if (window && !sourceText.isEmpty())
                window->setSourceText(sourceText);
            if (!window) {
                qInfo() << "clipboard does not contain an image or text";
            }
        },
    });
    commands.registerCommand({
        QStringLiteral("preferences.open"),
        QStringLiteral("首选项"),
        [&config, &pasteManager, applyHotkeys, configureSuperCapture, configuredPasteMouseActions] {
            snipaste::ui::PreferencesDialog dialog(config);
            if (dialog.exec() == QDialog::Accepted) {
                applyHotkeys();
                configureSuperCapture();
                pasteManager.setRestoreCount(config.value(QStringLiteral("paste/restoreCount"), 1).toInt());
                pasteManager.setUseCapturePosition(config.value(QStringLiteral("paste/useCapturePosition"), true).toBool());
                pasteManager.setDefaultOpacity(configuredPasteOpacity(config));
                pasteManager.setShadowEnabled(config.value(QStringLiteral("paste/shadow"), true).toBool());
                pasteManager.setResizeEnabled(!config.value(QStringLiteral("paste/disable_frame_resize"), false).toBool());
                pasteManager.setMagneticAttachThreshold(config.value(QStringLiteral("paste/magnetic_attach_threshold"),
                                                                     config.value(QStringLiteral("magnetic_attach_threshold"), 50)).toInt());
                pasteManager.setExchangeNavigationKeys(
                    config.value(QStringLiteral("hotkeys/exchange_wasd_and_arrow_keys"), false).toBool());
                pasteManager.setCopyAsFile(config.value(QStringLiteral("output/copyAsFileMode"), 2).toInt() == 0);
                pasteManager.setBackgroundMode(
                    config.value(QStringLiteral("paste/backgroundMode"), QStringLiteral("伪透明")).toString());
                pasteManager.setSmoothScale(config.value(QStringLiteral("paste/smoothScale"), true).toBool());
                pasteManager.setEscapeActions(
                    config.value(QStringLiteral("paste/escapeAction"), QStringLiteral("关闭贴图")).toString(),
                    config.value(QStringLiteral("paste/shiftEscapeAction"), QStringLiteral("销毁贴图")).toString());
                pasteManager.setMouseActions(configuredPasteMouseActions());
            }
        },
    });
    commands.registerCommand({
        QStringLiteral("paste.toggleAll"),
        QStringLiteral("隐藏/显示所有贴图"),
        [&pasteManager] { pasteManager.toggleAll(); },
    });
    commands.registerCommand({
        QStringLiteral("paste.showAll"),
        QStringLiteral("显示所有贴图"),
        [&pasteManager] { pasteManager.showAll(); },
    });
    commands.registerCommand({
        QStringLiteral("paste.hideAll"),
        QStringLiteral("隐藏所有贴图"),
        [&pasteManager] { pasteManager.hideAll(); },
    });
    commands.registerCommand({
        QStringLiteral("paste.switchGroup"),
        QStringLiteral("切换到另一贴图组"),
        [&pasteManager] {
            pasteManager.switchGroup();
            qInfo() << "active paste group count:" << pasteManager.groupCount();
        },
    });
    commands.registerCommand({
        QStringLiteral("paste.switchGroupPrev"),
        QStringLiteral("切换到上一个贴图组"),
        [&pasteManager] {
            const int count = pasteManager.groupCount();
            if (count > 1)
                pasteManager.switchGroup((pasteManager.activeGroup() + count - 1) % count);
        },
    });
    commands.registerCommand({
        QStringLiteral("paste.createGroup"),
        QStringLiteral("创建贴图组"),
        [&pasteManager] {
            bool ok = false;
            const QString name = QInputDialog::getText(nullptr,
                                                        QStringLiteral("创建贴图组"),
                                                        QStringLiteral("组名:"),
                                                        QLineEdit::Normal,
                                                        QString(),
                                                        &ok);
            if (ok)
                pasteManager.switchGroup(pasteManager.createGroup(name));
        },
    });
    commands.registerCommand({
        QStringLiteral("paste.emptyGroup"),
        QStringLiteral("清空当前贴图组"),
        [&pasteManager] { pasteManager.emptyCurrentGroup(); },
    });
    commands.registerCommand({
        QStringLiteral("paste.solo"),
        QStringLiteral("切换 Solo 模式"),
        [&pasteManager] { pasteManager.toggleSolo(); },
    });
    commands.registerCommand({
        QStringLiteral("paste.destroySelected"),
        QStringLiteral("销毁选中的贴图"),
        [&pasteManager] { pasteManager.destroySelected(); },
    });
    commands.registerCommand({
        QStringLiteral("paste.toggleClickThrough"),
        QStringLiteral("切换鼠标穿透"),
        [&pasteManager] { pasteManager.toggleClickThroughAtCursor(QCursor::pos()); },
    });
    commands.registerCommand({
        QStringLiteral("paste.noClickThrough"),
        QStringLiteral("取消所有贴图鼠标穿透"),
        [&pasteManager] { pasteManager.disableClickThroughAll(); },
    });
    commands.registerCommand({
        QStringLiteral("paste.groupManager"),
        QStringLiteral("管理贴图组"),
        [&pasteManager] {
            QDialog dialog;
            dialog.setWindowTitle(QStringLiteral("管理贴图组"));
            dialog.resize(420, 500);
            auto *list = new QListWidget(&dialog);
            list->addItems(pasteManager.groupNames());
            list->setCurrentRow(pasteManager.activeGroup());
            auto *pins = new QListWidget(&dialog);
            pins->setObjectName(QStringLiteral("pasteGroupThumbnails"));
            pins->setIconSize(QSize(64, 48));
            pins->setMaximumHeight(170);
            pins->setViewMode(QListView::IconMode);
            pins->setResizeMode(QListView::Adjust);
            const auto refreshPins = [&] {
                pins->clear();
                const int groupIndex = list->currentRow();
                const auto windows = pasteManager.windowsInGroup(groupIndex);
                int number = 1;
                for (snipaste::ui::PasteWindow *window : windows) {
                    if (!window)
                        continue;
                    QPixmap thumbnail = QPixmap::fromImage(window->displayedImage());
                    thumbnail = thumbnail.scaled(QSize(64, 48), Qt::KeepAspectRatio,
                                                 Qt::SmoothTransformation);
                    auto *item = new QListWidgetItem(QIcon(thumbnail),
                                                     QStringLiteral("贴图 %1").arg(number++), pins);
                    item->setToolTip(window->sourceText().isEmpty()
                                         ? QStringLiteral("图像贴图")
                                         : window->sourceText());
                }
            };
            refreshPins();
            auto *newButton = new QPushButton(QStringLiteral("新建"), &dialog);
            auto *renameButton = new QPushButton(QStringLiteral("重命名"), &dialog);
            auto *upButton = new QPushButton(QStringLiteral("上移"), &dialog);
            auto *downButton = new QPushButton(QStringLiteral("下移"), &dialog);
            auto *pinUpButton = new QPushButton(QStringLiteral("贴图上移"), &dialog);
            auto *pinDownButton = new QPushButton(QStringLiteral("贴图下移"), &dialog);
            auto *showButton = new QPushButton(QStringLiteral("显示此组"), &dialog);
            auto *emptyButton = new QPushButton(QStringLiteral("清空此组"), &dialog);
            auto *buttonsRow = new QHBoxLayout;
            buttonsRow->addWidget(newButton);
            buttonsRow->addWidget(renameButton);
            buttonsRow->addWidget(upButton);
            buttonsRow->addWidget(downButton);
            buttonsRow->addWidget(pinUpButton);
            buttonsRow->addWidget(pinDownButton);
            buttonsRow->addWidget(showButton);
            buttonsRow->addWidget(emptyButton);
            auto *dialogButtons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
            auto *layout = new QVBoxLayout(&dialog);
            layout->addWidget(list);
            layout->addWidget(new QLabel(QStringLiteral("当前组贴图顺序"), &dialog));
            layout->addWidget(pins);
            layout->addLayout(buttonsRow);
            layout->addWidget(dialogButtons);
            QObject::connect(list, &QListWidget::currentRowChanged, &dialog,
                             [&, refreshPins](int) { refreshPins(); });
            QObject::connect(newButton, &QPushButton::clicked, &dialog, [&] {
                bool ok = false;
                const QString name = QInputDialog::getText(&dialog,
                                                            QStringLiteral("新建贴图组"),
                                                            QStringLiteral("组名:"),
                                                            QLineEdit::Normal,
                                                            QString(),
                                                            &ok);
                if (!ok)
                    return;
                const int index = pasteManager.createGroup(name);
                list->addItem(pasteManager.groupNames().value(index));
                list->setCurrentRow(index);
                refreshPins();
            });
            QObject::connect(renameButton, &QPushButton::clicked, &dialog, [&] {
                const int index = list->currentRow();
                if (index < 0)
                    return;
                bool ok = false;
                const QString name = QInputDialog::getText(&dialog,
                                                            QStringLiteral("重命名贴图组"),
                                                            QStringLiteral("组名:"),
                                                            QLineEdit::Normal,
                                                            list->currentItem()->text(),
                                                            &ok);
                if (ok) {
                    pasteManager.renameGroup(index, name);
                    list->currentItem()->setText(pasteManager.groupNames().value(index));
                }
            });
            QObject::connect(upButton, &QPushButton::clicked, &dialog, [&] {
                const int from = list->currentRow();
                if (from <= 0)
                    return;
                pasteManager.moveGroup(from, from - 1);
                QListWidgetItem *item = list->takeItem(from);
                list->insertItem(from - 1, item);
                list->setCurrentRow(from - 1);
                refreshPins();
            });
            QObject::connect(downButton, &QPushButton::clicked, &dialog, [&] {
                const int from = list->currentRow();
                if (from < 0 || from + 1 >= list->count())
                    return;
                pasteManager.moveGroup(from, from + 1);
                QListWidgetItem *item = list->takeItem(from);
                list->insertItem(from + 1, item);
                list->setCurrentRow(from + 1);
                refreshPins();
            });
            QObject::connect(pinUpButton, &QPushButton::clicked, &dialog, [&] {
                const int groupIndex = list->currentRow();
                const int from = pins->currentRow();
                if (groupIndex < 0 || from <= 0)
                    return;
                pasteManager.moveWindowInGroup(groupIndex, from, from - 1);
                refreshPins();
                pins->setCurrentRow(from - 1);
            });
            QObject::connect(pinDownButton, &QPushButton::clicked, &dialog, [&] {
                const int groupIndex = list->currentRow();
                const int from = pins->currentRow();
                const int count = pasteManager.windowsInGroup(groupIndex).size();
                if (groupIndex < 0 || from < 0 || from + 1 >= count)
                    return;
                pasteManager.moveWindowInGroup(groupIndex, from, from + 1);
                refreshPins();
                pins->setCurrentRow(from + 1);
            });
            QObject::connect(showButton, &QPushButton::clicked, &dialog, [&] {
                pasteManager.showGroup(list->currentRow());
            });
            QObject::connect(emptyButton, &QPushButton::clicked, &dialog, [&] {
                if (list->currentRow() == pasteManager.activeGroup())
                    pasteManager.emptyCurrentGroup();
                refreshPins();
            });
            QObject::connect(dialogButtons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            dialog.exec();
        },
    });
    commands.registerCommand({
        QStringLiteral("paste.destroyAll"),
        QStringLiteral("销毁所有贴图"),
        [&pasteManager] { pasteManager.destroyAll(); },
    });
    commands.registerCommand({
        QStringLiteral("history.clear"),
        QStringLiteral("清空截屏历史"),
        [&history, &historyPath] {
            history.clear();
            saveHistorySnapshotAsync(history, historyPath);
            qInfo() << "screenshot history cleared";
        },
    });
    commands.registerCommand({
        QStringLiteral("history.open"),
        QStringLiteral("打开截屏历史"),
        [&history, &historyPath, &pasteManager, ensureHistoryLoaded] {
            ensureHistoryLoaded();
            snipaste::ui::ScreenshotHistoryDialog dialog(history);
            QObject::connect(&dialog,
                             &snipaste::ui::ScreenshotHistoryDialog::copyRequested,
                             [](const QImage &image) { QApplication::clipboard()->setImage(image); });
            QObject::connect(&dialog,
                             &snipaste::ui::ScreenshotHistoryDialog::pasteRequested,
                             [&pasteManager](const QImage &image) { pasteManager.createPaste(image); });
            QObject::connect(&dialog,
                             &snipaste::ui::ScreenshotHistoryDialog::clearRequested,
                             [&history, &historyPath] { saveHistorySnapshotAsync(history, historyPath); });
            dialog.exec();
        },
    });
    QList<snipaste::platform::GlobalHotkey *> customCommandHotkeys;
    commands.registerCommand({
        QStringLiteral("hotkeys.toggle"),
        QStringLiteral("禁用快捷键"),
        [&hotkeysEnabled, &screenshotHotkey, &screenshotCopyHotkey, &pasteHotkey,
         &customSnipHotkey, &toggleImagesHotkey, &switchGroupHotkey, &clickThroughHotkey,
         &customCommandHotkeys] {
            hotkeysEnabled = !hotkeysEnabled;
            screenshotHotkey.setEnabled(hotkeysEnabled);
            screenshotCopyHotkey.setEnabled(hotkeysEnabled);
            pasteHotkey.setEnabled(hotkeysEnabled);
            customSnipHotkey.setEnabled(hotkeysEnabled);
            toggleImagesHotkey.setEnabled(hotkeysEnabled);
            switchGroupHotkey.setEnabled(hotkeysEnabled);
            clickThroughHotkey.setEnabled(hotkeysEnabled);
            for (auto *hotkey : customCommandHotkeys)
                if (hotkey)
                    hotkey->setEnabled(hotkeysEnabled);
            qInfo() << (hotkeysEnabled ? "global hotkeys enabled" : "global hotkeys disabled");
        },
    });
    const auto setHotkeysEnabled = [&hotkeysEnabled, &screenshotHotkey, &screenshotCopyHotkey,
                                    &pasteHotkey, &customSnipHotkey, &toggleImagesHotkey,
                                    &switchGroupHotkey, &clickThroughHotkey,
                                    &customCommandHotkeys](bool enabled) {
        hotkeysEnabled = enabled;
        screenshotHotkey.setEnabled(enabled);
        screenshotCopyHotkey.setEnabled(enabled);
        pasteHotkey.setEnabled(enabled);
        customSnipHotkey.setEnabled(enabled);
        toggleImagesHotkey.setEnabled(enabled);
        switchGroupHotkey.setEnabled(enabled);
        clickThroughHotkey.setEnabled(enabled);
        for (auto *hotkey : customCommandHotkeys)
            if (hotkey)
                hotkey->setEnabled(enabled);
    };
    commands.registerCommand({
        QStringLiteral("hotkeys.disable"),
        QStringLiteral("禁用快捷键"),
        [setHotkeysEnabled] { setHotkeysEnabled(false); },
    });
    commands.registerCommand({
        QStringLiteral("hotkeys.enable"),
        QStringLiteral("启用快捷键"),
        [setHotkeysEnabled] { setHotkeysEnabled(true); },
    });
    commands.registerCommand({
        QStringLiteral("help.open"),
        QStringLiteral("帮助"),
        [] {
            QMessageBox::information(nullptr,
                                     QStringLiteral("帮助"),
                                     QStringLiteral("快捷键：F1 截图，F3 贴图。\n"
                                                    "项目文档位于 docs/ 目录。"));
        },
    });
    commands.registerCommand({
        QStringLiteral("app.about"),
        QStringLiteral("关于 snipaste"),
        [&app] {
            QMessageBox::about(nullptr,
                                QStringLiteral("关于 snipaste"),
                                QStringLiteral("snipaste %1\n开源截图、贴图和标注工具")
                                    .arg(QCoreApplication::applicationVersion()));
        },
    });
    commands.registerCommand({
        QStringLiteral("update.checkNow"),
        QStringLiteral("检查更新"),
        [] {
            QMessageBox::information(nullptr,
                                      QStringLiteral("检查更新"),
                                      QStringLiteral("当前已是最新版本。"));
        },
    });
    commands.registerCommand({
        QStringLiteral("app.purchase"),
        QStringLiteral("鎵撳紑涓撲笟鐗堣喘涔伴〉"),
        [] {
            QMessageBox::information(nullptr,
                                     QStringLiteral("snipaste"),
                                     QStringLiteral("snipaste 是开源版本，不需要购买授权。\n"
                                                    "如果你需要支持原 Snipaste，请访问其官方网站。"));
        },
    });
    commands.registerCommand({
        QStringLiteral("app.restart"),
        QStringLiteral("重新启动"),
        [&app] {
            const QString program = QCoreApplication::applicationFilePath();
            QStringList arguments = QCoreApplication::arguments().mid(1);
            arguments.removeAll(QStringLiteral("--snipaste-restart"));
            arguments.append(QStringLiteral("--snipaste-restart"));
            if (QProcess::startDetached(program, arguments))
                app.quit();
            else
                qWarning() << "failed to restart application" << program;
        },
    });
    commands.registerCommand({
        QStringLiteral("app.quit"),
        QStringLiteral("退出"),
        [&app] { app.quit(); },
    });
    const auto openWhiteboard = [](QColor color, bool transparent, bool activeScreenOnly) {
        QList<QScreen *> screens;
        if (activeScreenOnly) {
            QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
            if (!screen)
                screen = QGuiApplication::primaryScreen();
            if (screen)
                screens.append(screen);
        } else {
            screens = QGuiApplication::screens();
        }
        for (QScreen *screen : screens) {
            auto *window = new WhiteboardWindow(screen, color, transparent);
            window->show();
            window->raise();
            window->activateWindow();
            window->setFocus();
        }
    };
    commands.registerCommand({
        QStringLiteral("whiteboard"),
        QStringLiteral("打开白板"),
        [openWhiteboard] { openWhiteboard(QColor(Qt::white), false, false); },
    });
    commands.registerCommand({
        QStringLiteral("transparentWhiteboard"),
        QStringLiteral("打开透明白板"),
        [openWhiteboard] { openWhiteboard(QColor(Qt::white), true, false); },
    });
    commands.registerCommand({
        QStringLiteral("pickColor"),
        QStringLiteral("拾取颜色"),
        [] {
            const QColor color = colorAtCursor();
            if (color.isValid())
                QApplication::clipboard()->setText(color.name(QColor::HexArgb));
        },
    });
    commands.registerCommand({
        QStringLiteral("pasteAsImage"),
        QStringLiteral("将剪贴板转为图像并粘贴"),
        [] {
            const QMimeData *mime = QApplication::clipboard()->mimeData();
            if (!mime || !mime->hasText())
                return;
            QApplication::clipboard()->setImage(clipboardTextImage(mime->text()));
            pasteClipboardAsKeyboardInput();
        },
    });
    commands.registerCommand({
        QStringLiteral("pasteAsPlainText"),
        QStringLiteral("将剪贴板转为纯文本并粘贴"),
        [] {
            const QMimeData *mime = QApplication::clipboard()->mimeData();
            if (!mime)
                return;
            const QString text = mime->hasHtml() ? mime->text() : mime->text();
            if (text.isEmpty())
                return;
            QApplication::clipboard()->setText(text);
            pasteClipboardAsKeyboardInput();
        },
    });
    commands.registerCommand({
        QStringLiteral("barcodeDecode"),
        QStringLiteral("识别图片条码"),
        [] {
            QImage image = QApplication::clipboard()->image();
            if (image.isNull())
                image = captureAllScreens();
            const auto result = snipaste::core::BarcodeService::recognize(image);
            QApplication::clipboard()->setText(result.success ? result.text : QString());
        },
    });

    // Professional builds allow arbitrary commands to be bound from the
    // configuration file.  Each entry accepts either `command=Key` or
    // `Key=command`; command aliases are resolved by CommandRegistry.
    QStringList customBindings = configuredProcessList(config, QStringLiteral("hotkeys/commands"),
                                                        QStringLiteral("hotkeys/customCommands"));
    const bool allowSimpleHotkeys = config.value(QStringLiteral("hotkeys/allow_simple_hotkeys"),
                                                 config.value(QStringLiteral("allow_simple_hotkeys"), false)).toBool();
    int customHotkeyId = 100;
    for (const QString &binding : customBindings) {
        const int separator = binding.indexOf(QChar('='));
        if (separator <= 0 || separator >= binding.size() - 1)
            continue;
        QString command = binding.left(separator).trimmed();
        QString key = binding.mid(separator + 1).trimmed();
        HotkeySpec spec = parseHotkey(key);
        if (!spec.valid) {
            std::swap(command, key);
            spec = parseHotkey(key);
        }
        if (!spec.valid || (!allowSimpleHotkeys && spec.simple)
            || command.isEmpty() || !commands.contains(command))
            continue;
        auto *hotkey = new snipaste::platform::GlobalHotkey(&app);
        if (!hotkey->registerKey(customHotkeyId++, spec.modifiers, spec.key)) {
            hotkey->deleteLater();
            continue;
        }
        QObject::connect(hotkey, &snipaste::platform::GlobalHotkey::triggered, &app,
                         [&, command](int) {
                             if (!isHotkeyIgnored(config) && hotkeysEnabled)
                                 commands.execute(command);
                         });
        customCommandHotkeys.append(hotkey);
    }

    QTimer hotCornerTimer;
    snipaste::core::HotCornerState hotCornerState;
    QElapsedTimer hotCornerClock;
    hotCornerClock.start();
    QObject::connect(&hotCornerTimer, &QTimer::timeout, &app, [&] {
        const int interval = qBound(10, config.value(QStringLiteral("control/cornerInterval"), 125).toInt(), 2000);
        if (hotCornerTimer.interval() != interval) hotCornerTimer.setInterval(interval);
        const bool enabled = config.value(QStringLiteral("control/cornerEnabled"), false).toBool();
        if (!enabled || activeOverlay || activeWindowOverlay || QApplication::activeModalWidget()
            || QApplication::activePopupWidget() || QGuiApplication::mouseButtons() != Qt::NoButton
            || isApplicationIgnored(config, QStringLiteral("control/cornerIgnoredApplications"))) {
            hotCornerState.reset();
            return;
        }
        const QPoint cursor = QCursor::pos();
        QScreen *screen = QGuiApplication::screenAt(cursor);
        if (!screen) { hotCornerState.reset(); return; }
        // Screen corners, including the taskbar area. Convert the physical
        // pixel setting to the logical coordinates returned by QCursor.
        const int size = qMax(1, qCeil(qBound(1, config.value(QStringLiteral("control/cornerSize"), 10).toInt(), 200)
                                     / screen->devicePixelRatio()));
        const int corner = hotCornerState.sample(cursor, screen->geometry(), size,
            hotCornerClock.elapsed(), qBound(0, config.value(QStringLiteral("control/cornerDelay"), 300).toInt(), 5000), true);
        if (corner < 0) return;
        const QStringList keys = {QStringLiteral("control/cornerTopLeft"), QStringLiteral("control/cornerTopRight"),
                                 QStringLiteral("control/cornerBottomLeft"), QStringLiteral("control/cornerBottomRight")};
        const QString command = config.value(keys.at(corner), corner < 2 ? QStringLiteral("toggle-images") : QString()).toString();
        if (!command.trimmed().isEmpty()) commands.execute(command);
    });
    hotCornerTimer.setInterval(qBound(10, config.value(QStringLiteral("control/cornerInterval"), 125).toInt(), 2000));
    hotCornerTimer.start();
    const auto setCornersEnabled = [&](bool enabled) {
        config.setValue(QStringLiteral("control/cornerEnabled"), enabled);
        config.save();
        hotCornerState.reset();
    };
    commands.registerCommand({QStringLiteral("corners.disable"), QStringLiteral("禁用触发角"),
                              [setCornersEnabled] { setCornersEnabled(false); }});
    commands.registerCommand({QStringLiteral("corners.enable"), QStringLiteral("启用触发角"),
                              [setCornersEnabled] { setCornersEnabled(true); }});
    commands.registerCommand({QStringLiteral("corners.toggle"), QStringLiteral("切换触发角"),
                              [&config, setCornersEnabled] { setCornersEnabled(!config.value(QStringLiteral("control/cornerEnabled"), false).toBool()); }});

    // Handle commands sent by a second process invocation through the single
    // instance channel. This keeps shell integrations useful while retaining
    // one tray process.
    QObject::connect(&instance, &snipaste::platform::SingleInstance::messageReceivedWithSocket,
                     &app, [&app, &instance, &config, &commands, &pasteManager, &activeOverlay,
                            &activeWindowOverlay, startSnip, startWindowCapture,
                            openWhiteboard, outputCapture, recordScreenshot, autoSaveScreenshot]
                     (const QStringList &arguments, QLocalSocket *socket) {
        const QStringList args = normalizeMultiValueOptions(arguments);
        const auto has = [&args](const QString &name) {
            return args.contains(name) || std::any_of(args.cbegin(), args.cend(), [&name](const QString &value) {
                return value.startsWith(name + QStringLiteral("="));
            });
        };
        const auto value = [&args](const QString &name) {
            for (int i = 0; i < args.size(); ++i) {
                const QString argument = args.at(i);
                if (argument.startsWith(name + QStringLiteral("=")))
                    return argument.mid(name.size() + 1);
                if (argument == name && i + 1 < args.size())
                    return args.at(i + 1);
            }
            return QString();
        };
        const bool isSnip = args.contains(QStringLiteral("snip")) || has(QStringLiteral("--snip"));
        const bool isPaste = args.contains(QStringLiteral("paste")) || has(QStringLiteral("--paste"))
                             || has(QStringLiteral("--clipboard"));
        const bool awaitCompletion = has(QStringLiteral("--snipaste-await"));
        const auto finishImmediately = [&] {
            if (awaitCompletion)
                instance.finishMessage(socket);
        };
        const auto finishAfterFlow = [&] {
            if (!awaitCompletion)
                return;
            if (!has(QStringLiteral("--block"))) {
                instance.finishMessage(socket);
                return;
            }
            QString blockValue = value(QStringLiteral("--block"));
            bool secondsOk = false;
            const double seconds = blockValue.toDouble(&secondsOk);
            const qint64 deadline = secondsOk && seconds > 0.0
                ? QDateTime::currentMSecsSinceEpoch() + qRound64(seconds * 1000.0)
                : 0;
            auto *poll = new QTimer(&app);
            poll->setInterval(25);
            QObject::connect(poll, &QTimer::timeout, &app,
                             [poll, socket, &instance, &activeOverlay, &activeWindowOverlay, deadline] {
                const bool expired = deadline > 0 && QDateTime::currentMSecsSinceEpoch() >= deadline;
                if ((!activeOverlay && !activeWindowOverlay) || expired) {
                    instance.finishMessage(socket);
                    poll->stop();
                    poll->deleteLater();
                }
            });
            poll->start();
        };
        if (args.value(0) == QStringLiteral("external")) {
            startExternalCommand(args.mid(1));
            finishImmediately();
            return;
        }
        if (args.value(0) == QStringLiteral("whiteboard")
            || args.value(0) == QStringLiteral("transparent-whiteboard")) {
            const QString color = value(QStringLiteral("--color"));
            const bool transparent = args.value(0) == QStringLiteral("transparent-whiteboard");
            openWhiteboard(parseColorSpec(color), transparent, has(QStringLiteral("--active-screen")));
            finishImmediately();
            return;
        }
        if (isSnip) {
            const QString output = value(QStringLiteral("--output")).isEmpty()
                ? value(QStringLiteral("-o")) : value(QStringLiteral("--output"));
            const bool hold = has(QStringLiteral("--hold"));
            const bool noAutoSave = output.contains(QStringLiteral("no-auto-save"));
            const QRect area = parseRectSpec(value(QStringLiteral("--area")));
            const QSize size = parseSizeSpec(value(QStringLiteral("--size")));
            const bool autoOutput = !output.isEmpty() && !hold;
            if (has(QStringLiteral("--full")) || has(QStringLiteral("--full-screen"))) {
                const QImage image = captureAllScreens();
                if (!image.isNull()) {
                    recordScreenshot(image, image);
                    if (!noAutoSave)
                        autoSaveScreenshot(image);
                    if (output.isEmpty())
                        QApplication::clipboard()->setImage(image);
                    else
                        outputCapture(image, output);
                }
            } else if (has(QStringLiteral("--active-window"))) {
                startWindowCapture(false, output, noAutoSave);
            } else if (has(QStringLiteral("--active-screen"))) {
                QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
                if (!screen)
                    screen = QGuiApplication::primaryScreen();
                if (screen)
                    startSnip(autoOutput, false, size, screen->geometry(), false,
                              output, hold, noAutoSave);
            } else if (has(QStringLiteral("--window"))) {
                commands.execute(QStringLiteral("snip.window"));
            } else if (has(QStringLiteral("--element"))) {
                commands.execute(QStringLiteral("snip.element"));
            } else if (has(QStringLiteral("--last"))) {
                startSnip(autoOutput, true, size, area, false, output, hold, noAutoSave);
            } else if (has(QStringLiteral("--custom"))) {
                commands.execute(QStringLiteral("snip.custom"));
            } else if (has(QStringLiteral("--copy"))) {
                commands.execute(QStringLiteral("snip.copy"));
            } else {
                const QString delay = value(QStringLiteral("--delay"));
                bool delayOk = false;
                const int delayMs = qMax(0, qRound(delay.toDouble(&delayOk) * 1000.0));
                const auto launch = [startSnip, autoOutput, size, area, output, hold, noAutoSave] {
                    startSnip(autoOutput, false, size, area, false, output, hold, noAutoSave);
                };
                if (delayOk && delayMs > 0)
                    QTimer::singleShot(delayMs, launch);
                else
                    launch();
            }
            finishAfterFlow();
            return;
        }
        if (isPaste) {
            const QPoint position = parsePointSpec(value(QStringLiteral("--pos")));
            const QString plain = value(QStringLiteral("--plain"));
            const QString html = value(QStringLiteral("--html"));
            if (!plain.isEmpty()) {
                const QImage image = clipboardTextImage(plain);
                if (!position.isNull()) pasteManager.createPasteAtPosition(image, position);
                else pasteManager.createPaste(image);
            } else if (!html.isEmpty()) {
                const QImage image = clipboardHtmlImage(
                    html,
                    config.value(QStringLiteral("paste/force_html_wrap"),
                                 config.value(QStringLiteral("force_html_wrap"), true)).toBool());
                if (!position.isNull()) pasteManager.createPasteAtPosition(image, position);
                else pasteManager.createPaste(image);
            } else if (has(QStringLiteral("--files"))) {
                for (const QString &path : resolvePasteFileArguments(args.mid(args.indexOf(QStringLiteral("--files")) + 1))) {
                    const QImage image(path);
                    if (!image.isNull()) {
                        if (!position.isNull()) pasteManager.createPasteAtPosition(image, position);
                        else pasteManager.createPaste(image);
                    }
                }
            } else {
                commands.execute(QStringLiteral("paste.clipboard"));
            }
            finishImmediately();
            return;
        }
        for (const QString &argument : args) {
            if (commands.contains(argument)) {
                commands.execute(argument);
                finishImmediately();
                return;
            }
        }
        finishImmediately();
    });
    snipaste::ui::TrayController tray(commands, config);
    tray.show();
    if (config.value(QStringLiteral("interface/hideTrayIcon"),
                     config.value(QStringLiteral("hide_tray_icon"), false)).toBool())
        tray.hide();

    const bool commandLineCopy = parser.isSet(copyOption);
    const bool commandLineSnip = parser.isSet(snipOption);
    const bool commandLinePaste = parser.isSet(pasteOption) || parser.isSet(clipboardPasteOption);
    const bool commandLineHistory = parser.isSet(historyOption);
    const bool commandLineFullScreen = parser.isSet(fullScreenOption);
    const bool commandLineOfficialFull = parser.isSet(officialFullOption);
    const bool commandLineOfficialLast = parser.isSet(officialLastOption);
    const bool commandLineActiveWindow = parser.isSet(activeWindowOption);
    const bool commandLineActiveScreen = parser.isSet(activeScreenOption);
    const bool commandLineCustom = parser.isSet(customOption);
    const bool commandLineHold = parser.isSet(holdOption);
    const bool commandLineShadow = parser.isSet(shadowOption);
    const bool commandLineNoShadow = parser.isSet(noShadowOption);
    const bool commandLineKeepAspect = parser.isSet(keepAspectOption);
    const QString commandLineDelay = parser.value(delayOption);
    const QString commandLineOutput = parser.value(outputOption);
    const bool commandLineBlock = parser.isSet(blockOption);
    const QString commandLineBlockSeconds = parser.value(blockOption);
    const QRect commandLineArea = parseRectSpec(parser.value(areaOption));
    const QSize commandLineSize = parseSizeSpec(parser.value(sizeOption));
    const QPoint commandLinePosition = parsePointSpec(parser.value(positionOption));
    const bool commandLineWindow = parser.isSet(windowOption);
    const bool commandLineElement = parser.isSet(elementOption);
    const bool commandLineShowImages = parser.isSet(showImagesOption);
    const bool commandLineHideImages = parser.isSet(hideImagesOption);
    const bool commandLineToggleImages = parser.isSet(toggleImagesOption);
    const bool commandLineGroupManager = parser.isSet(groupManagerOption);
    const bool commandLineEmptyGroup = parser.isSet(emptyGroupOption);
    const bool commandLineSwitchGroupPrev = parser.isSet(switchGroupPrevOption);
    const bool commandLineSolo = parser.isSet(soloOption);
    const bool commandLineToggleClickThrough = parser.isSet(toggleClickThroughOption);
    const bool commandLineNoClickThrough = parser.isSet(noClickThroughOption);
    const bool commandLineDisableHotkeys = parser.isSet(disableHotkeysOption);
    const bool commandLineEnableHotkeys = parser.isSet(enableHotkeysOption);
    const bool commandLineToggleHotkeys = parser.isSet(toggleHotkeysOption);
    const bool commandLinePickColor = parser.isSet(pickColorOption);
    const bool commandLineBarcodeDecode = parser.isSet(barcodeDecodeOption);
    const bool commandLineWhiteboard = parser.isSet(whiteboardOption);
    const bool commandLineTransparentWhiteboard = parser.isSet(transparentWhiteboardOption);
    const QString commandLineWhiteboardColor = parser.value(whiteboardColorOption);
    const bool commandLinePlain = parser.isSet(plainOption);
    const bool commandLineFiles = parser.isSet(filesOption);
    const bool commandLineHtml = parser.isSet(htmlOption);
    const QString barcodeFile = parser.value(barcodeDecodeOption);
    const QStringList positionalArguments = parser.positionalArguments();
    const QString positionalCommand = positionalArguments.value(0);
    const QString positionalArgument = positionalArguments.value(1);
    const bool hasPositionalCommand = !positionalCommand.isEmpty();
    if (commandLineCopy || commandLineSnip || commandLinePaste || commandLineHistory
        || commandLineFullScreen || commandLineOfficialFull || commandLineOfficialLast
        || commandLineActiveWindow || commandLineActiveScreen || commandLineCustom
        || commandLineHold || !commandLineDelay.isEmpty() || !commandLineOutput.isEmpty()
        || commandLineBlock
        || parser.isSet(areaOption) || parser.isSet(sizeOption) || parser.isSet(positionOption)
        || commandLineShadow || commandLineNoShadow
        || commandLineWindow || commandLineElement
        || commandLineShowImages || commandLineHideImages || commandLineToggleImages
        || commandLineGroupManager || commandLineEmptyGroup || commandLineSwitchGroupPrev
        || commandLineSolo || commandLineToggleClickThrough || commandLineDisableHotkeys
        || commandLineNoClickThrough
        || commandLineEnableHotkeys || commandLineToggleHotkeys || commandLinePickColor
        || commandLineBarcodeDecode || commandLineWhiteboard || commandLineTransparentWhiteboard
        || commandLinePlain || commandLineHtml || commandLineFiles
        || parser.isSet(whiteboardColorOption) || hasPositionalCommand) {
         QTimer::singleShot(0, [&app, &config,
                                &activeOverlay,
                                &activeWindowOverlay,
                                &commands,
                               &pasteManager,
                                startSnip,
                                startWindowCapture,
                               &screenshotHotkey,
                               &screenshotCopyHotkey,
                               &pasteHotkey,
                               &customSnipHotkey,
                               &toggleImagesHotkey,
                               &switchGroupHotkey,
                               &clickThroughHotkey,
                               commandLineCopy,
                               commandLineSnip,
                               commandLinePaste,
                               commandLineHistory,
                               commandLineFullScreen,
                               commandLineOfficialFull,
                               commandLineOfficialLast,
                               commandLineActiveWindow,
                               commandLineActiveScreen,
                               commandLineCustom,
                               commandLineHold,
                               commandLineShadow,
                               commandLineNoShadow,
                                commandLineKeepAspect,
                               commandLineDelay,
                               commandLineOutput,
                               commandLineArea,
                               commandLineSize,
                               commandLinePosition,
                               commandLineWindow,
                               commandLineElement,
                               commandLineShowImages,
                               commandLineHideImages,
                               commandLineToggleImages,
                               commandLineGroupManager,
                               commandLineEmptyGroup,
                               commandLineSwitchGroupPrev,
                               commandLineSolo,
                               commandLineToggleClickThrough,
                               commandLineNoClickThrough,
                               commandLineDisableHotkeys,
                               commandLineEnableHotkeys,
                               commandLineToggleHotkeys,
                               commandLinePickColor,
                               commandLineBarcodeDecode,
                               commandLineWhiteboard,
                               commandLineTransparentWhiteboard,
                               commandLineWhiteboardColor,
                               commandLinePlain,
                               commandLineHtml,
                               commandLineFiles,
                                commandLineBlock,
                                commandLineBlockSeconds,
                               barcodeFile,
                               positionalArguments,
                               positionalCommand,
                                positionalArgument,
                                openWhiteboard,
                                outputCapture,
                                recordScreenshot,
                                autoSaveScreenshot] {
             const bool externalCommand = positionalCommand == QStringLiteral("external");
             if (externalCommand)
                 startExternalCommand(positionalArguments.mid(1));
             const bool whiteboardCommand = positionalCommand == QStringLiteral("whiteboard")
                                            || positionalCommand == QStringLiteral("transparent-whiteboard");
             if (whiteboardCommand || commandLineWhiteboard || commandLineTransparentWhiteboard) {
                 const QString colorSpec = commandLineWhiteboardColor.isEmpty()
                     ? (whiteboardCommand ? positionalArgument : QString())
                     : commandLineWhiteboardColor;
                 openWhiteboard(parseColorSpec(colorSpec),
                                commandLineTransparentWhiteboard
                                    || positionalCommand == QStringLiteral("transparent-whiteboard"),
                                commandLineActiveScreen);
             }
             const bool officialSnip = positionalCommand == QStringLiteral("snip") || commandLineSnip
                                       || commandLineCopy;
            const bool officialPaste = positionalCommand == QStringLiteral("paste") || commandLinePaste;
            const QStringList outputTokens = splitOutputSpec(commandLineOutput);
            const bool copyOutput = outputTokens.contains(QStringLiteral("clipboard"));
            const bool pinOutput = outputTokens.contains(QStringLiteral("pin"));
            const bool noAutoSave = outputTokens.contains(QStringLiteral("no-auto-save"));
             const bool automaticOutput = !commandLineOutput.isEmpty() && !commandLineHold;
             QString effectiveOutput = commandLineOutput;
             if (commandLineShadow && !effectiveOutput.contains(QStringLiteral("shadow")))
                 effectiveOutput += (effectiveOutput.isEmpty() ? QString() : QStringLiteral(";")) + QStringLiteral("shadow");
             if (commandLineNoShadow && !effectiveOutput.contains(QStringLiteral("no-shadow")))
                 effectiveOutput += (effectiveOutput.isEmpty() ? QString() : QStringLiteral(";")) + QStringLiteral("no-shadow");
            const auto captureWithOutputs = [&](const QImage &image) {
                if (image.isNull())
                    return;
                recordScreenshot(image, image);
                if (!noAutoSave)
                    autoSaveScreenshot(image);
                 if (!effectiveOutput.isEmpty())
                     outputCapture(image, effectiveOutput);
            };
            if (officialSnip && (commandLineOfficialFull || commandLineFullScreen))
                captureWithOutputs(captureAllScreens());
            else if (officialSnip && commandLineOfficialLast)
                startSnip(automaticOutput || copyOutput || pinOutput, true, commandLineSize, commandLineArea,
                          false, effectiveOutput, commandLineHold, noAutoSave, commandLineKeepAspect);
            else if (officialSnip && commandLineActiveWindow)
                startWindowCapture(false, effectiveOutput, noAutoSave);
             else if (officialSnip && commandLineActiveScreen)
                 [&] {
                     QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
                     if (!screen)
                         screen = QGuiApplication::primaryScreen();
                     if (screen)
                         startSnip(automaticOutput || copyOutput || pinOutput, false,
                                   commandLineSize, screen->geometry(), false,
                                   effectiveOutput, commandLineHold, noAutoSave,
                                   commandLineKeepAspect);
                 }();
            else if (officialSnip && commandLineCustom)
                commands.execute(QStringLiteral("snip.custom"));
            else if (officialSnip && (commandLineHold || !commandLineDelay.isEmpty() || !effectiveOutput.isEmpty()
                                      || commandLineArea.isValid() || commandLineSize.isValid())) {
                bool delayOk = false;
                const int delayMs = qMax(0, qRound(commandLineDelay.toDouble(&delayOk) * 1000.0));
                const auto launch = [startSnip, automaticOutput, copyOutput, pinOutput, commandLineSize,
                                     commandLineArea, effectiveOutput, commandLineHold, noAutoSave,
                                     commandLineKeepAspect] {
                    startSnip(automaticOutput || copyOutput || pinOutput, false, commandLineSize, commandLineArea,
                              false, effectiveOutput, commandLineHold, noAutoSave, commandLineKeepAspect);
                };
                if (delayOk && delayMs > 0)
                    QTimer::singleShot(delayMs, launch);
                else
                    launch();
            } else if (commandLineCopy)
                commands.execute(QStringLiteral("snip.copy"));
            else if (commandLineSnip)
                commands.execute(QStringLiteral("snip.start"));
            if (officialPaste && commandLinePlain) {
                const QString text = positionalArguments.mid(1).join(QChar(' '));
                if (!text.isEmpty()) {
                    const QImage image = clipboardTextImage(text);
                    if (!commandLinePosition.isNull())
                        pasteManager.createPasteAtPosition(image, commandLinePosition);
                    else
                        pasteManager.createPaste(image);
                }
            } else if (officialPaste && commandLineHtml) {
                const QString html = positionalArguments.mid(1).join(QChar(' '));
                if (!html.isEmpty()) {
                    const QImage image = clipboardHtmlImage(
                        html,
                        config.value(QStringLiteral("paste/force_html_wrap"),
                                     config.value(QStringLiteral("force_html_wrap"), true)).toBool());
                    if (!commandLinePosition.isNull())
                        pasteManager.createPasteAtPosition(image, commandLinePosition);
                    else
                        pasteManager.createPaste(image);
                }
            } else if (commandLineFiles || (officialPaste && commandLineFiles)) {
                const QStringList fileArguments = officialPaste ? positionalArguments.mid(1)
                                                                 : positionalArguments;
                for (const QString &fileArgument : resolvePasteFileArguments(fileArguments)) {
                    const QImage image(fileArgument);
                    if (!image.isNull()) {
                        if (!commandLinePosition.isNull())
                            pasteManager.createPasteAtPosition(image, commandLinePosition);
                        else
                            pasteManager.createPaste(image);
                    }
                }
            } else if (commandLinePaste) {
                if (!commandLinePosition.isNull()) {
                    const QImage image = QApplication::clipboard()->image();
                    if (!image.isNull())
                        pasteManager.createPasteAtPosition(image, commandLinePosition);
                    else
                        commands.execute(QStringLiteral("paste.clipboard"));
                } else {
                    commands.execute(QStringLiteral("paste.clipboard"));
                }
            }
            if (commandLineHistory)
                commands.execute(QStringLiteral("history.open"));
            if (commandLineFullScreen)
                commands.execute(QStringLiteral("snip.fullScreen"));
            if (commandLineWindow)
                commands.execute(QStringLiteral("snip.window"));
            if (commandLineElement)
                commands.execute(QStringLiteral("snip.element"));
            if (commandLineShowImages)
                pasteManager.showAll();
            if (commandLineHideImages)
                pasteManager.hideAll();
            if (commandLineToggleImages)
                commands.execute(QStringLiteral("paste.toggleAll"));
            if (commandLineGroupManager)
                commands.execute(QStringLiteral("paste.groupManager"));
            if (commandLineEmptyGroup)
                commands.execute(QStringLiteral("paste.emptyGroup"));
            if (commandLineSwitchGroupPrev)
                commands.execute(QStringLiteral("paste.switchGroupPrev"));
            if (commandLineSolo)
                commands.execute(QStringLiteral("paste.solo"));
            if (commandLineToggleClickThrough)
                commands.execute(QStringLiteral("paste.toggleClickThrough"));
            if (commandLineNoClickThrough)
                pasteManager.disableClickThroughAll();
            if (commandLineDisableHotkeys) {
                screenshotHotkey.setEnabled(false);
                screenshotCopyHotkey.setEnabled(false);
                pasteHotkey.setEnabled(false);
                customSnipHotkey.setEnabled(false);
                toggleImagesHotkey.setEnabled(false);
                switchGroupHotkey.setEnabled(false);
                clickThroughHotkey.setEnabled(false);
            }
            if (commandLineEnableHotkeys) {
                screenshotHotkey.setEnabled(true);
                screenshotCopyHotkey.setEnabled(true);
                pasteHotkey.setEnabled(true);
                customSnipHotkey.setEnabled(true);
                toggleImagesHotkey.setEnabled(true);
                switchGroupHotkey.setEnabled(true);
                clickThroughHotkey.setEnabled(true);
            }
            if (commandLineToggleHotkeys)
                commands.execute(QStringLiteral("hotkeys.toggle"));
            if (commandLinePickColor)
                commands.execute(QStringLiteral("pickColor"));
            if (commandLineBarcodeDecode && !barcodeFile.isEmpty()) {
                const QImage image(barcodeFile);
                const auto result = snipaste::core::BarcodeService::recognize(image);
                QApplication::clipboard()->setText(result.success ? result.text : QString());
            }
             if (commandLinePlain && !officialPaste)
                commands.execute(QStringLiteral("pasteAsImage"));
            if (commandLineFiles && !officialPaste)
                commands.execute(QStringLiteral("paste.clipboard"));
            if (positionalCommand == QStringLiteral("show-images"))
                pasteManager.showAll();
            else if (positionalCommand == QStringLiteral("hide-images"))
                pasteManager.hideAll();
            else if (positionalCommand == QStringLiteral("toggle-images"))
                pasteManager.toggleAll();
            else if (positionalCommand == QStringLiteral("no-click-through"))
                pasteManager.disableClickThroughAll();
            else if (positionalCommand == QStringLiteral("toggle-click-through"))
                pasteManager.toggleClickThroughAtCursor(QCursor::pos());
            else if (positionalCommand == QStringLiteral("create-group"))
                pasteManager.createGroup(positionalArgument);
            else if (positionalCommand == QStringLiteral("switch-group")) {
                bool ok = false;
                const int index = positionalArgument.toInt(&ok);
                if (ok)
                    pasteManager.switchGroup(index);
                else {
                    const int namedIndex = pasteManager.groupNames().indexOf(positionalArgument);
                    if (namedIndex >= 0)
                        pasteManager.switchGroup(namedIndex);
                    else
                        pasteManager.switchGroup();
                }
            } else if (positionalCommand == QStringLiteral("switch-group-prev"))
                commands.execute(QStringLiteral("paste.switchGroupPrev"));
            else if (positionalCommand == QStringLiteral("empty-group"))
                pasteManager.emptyCurrentGroup();
            else if (positionalCommand == QStringLiteral("show-group-manager"))
                commands.execute(QStringLiteral("paste.groupManager"));
            else if (positionalCommand == QStringLiteral("pick-color"))
                commands.execute(QStringLiteral("pickColor"));
            else if (positionalCommand == QStringLiteral("barcode-decode")
                     && !positionalArgument.isEmpty()) {
                const auto result = snipaste::core::BarcodeService::recognize(QImage(positionalArgument));
                QApplication::clipboard()->setText(result.success ? result.text : QString());
            }
            else if (positionalCommand == QStringLiteral("paste-as-image"))
                commands.execute(QStringLiteral("pasteAsImage"));
            else if (positionalCommand == QStringLiteral("paste-as-plain-text"))
                commands.execute(QStringLiteral("pasteAsPlainText"));
            else if (positionalCommand == QStringLiteral("open-preferences"))
                commands.execute(QStringLiteral("preferences.open"));
            else if (positionalCommand == QStringLiteral("disable-hotkeys")) {
                screenshotHotkey.setEnabled(false);
                screenshotCopyHotkey.setEnabled(false);
                pasteHotkey.setEnabled(false);
                customSnipHotkey.setEnabled(false);
                toggleImagesHotkey.setEnabled(false);
                switchGroupHotkey.setEnabled(false);
                clickThroughHotkey.setEnabled(false);
            }
            else if (positionalCommand == QStringLiteral("enable-hotkeys")) {
                screenshotHotkey.setEnabled(true);
                screenshotCopyHotkey.setEnabled(true);
                pasteHotkey.setEnabled(true);
                customSnipHotkey.setEnabled(true);
                toggleImagesHotkey.setEnabled(true);
                switchGroupHotkey.setEnabled(true);
                clickThroughHotkey.setEnabled(true);
            }
            else if (positionalCommand == QStringLiteral("toggle-hotkeys"))
                commands.execute(QStringLiteral("hotkeys.toggle"));
            else if (positionalCommand == QStringLiteral("clear-snip-history"))
                commands.execute(QStringLiteral("history.clear"));
            else if (positionalCommand == QStringLiteral("docs"))
                commands.execute(QStringLiteral("help.open"));
            else if (positionalCommand == QStringLiteral("exit"))
                commands.execute(QStringLiteral("app.quit"));

            if (commandLineBlock) {
                bool secondsOk = false;
                const double seconds = commandLineBlockSeconds.toDouble(&secondsOk);
                if (secondsOk && seconds > 0.0) {
                    QTimer::singleShot(qMax(1, qRound(seconds * 1000.0)),
                                       &app, &QCoreApplication::quit);
                } else if (activeOverlay) {
                    QObject::connect(activeOverlay, &QObject::destroyed,
                                     &app, &QCoreApplication::quit,
                                     Qt::SingleShotConnection);
                } else if (activeWindowOverlay) {
                    QObject::connect(activeWindowOverlay, &QObject::destroyed,
                                     &app, &QCoreApplication::quit,
                                     Qt::SingleShotConnection);
                } else {
                    QTimer::singleShot(0, &app, &QCoreApplication::quit);
                }
            }
        });
    }

    qInfo() << "snipaste started" << QCoreApplication::applicationVersion();
    return app.exec();
}
