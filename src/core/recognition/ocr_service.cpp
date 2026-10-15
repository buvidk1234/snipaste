#include "core/recognition/ocr_service.h"
#include "core/recognition/ocr_image.h"

#include <QDir>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTransform>

#include <exception>

#ifdef Q_OS_WIN
#include <windows.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/base.h>
#endif

#if defined(Q_OS_MACOS)
#include "platform/macos/vision_ocr.h"
#endif

namespace snipaste::core {

namespace {

QString normalizedOcrLanguage(const QString &value)
{
    const QString language = value.trimmed();
    if (language.isEmpty())
        return {};
    const QString folded = language.toCaseFolded();
    if (folded == QStringLiteral("system")
        || folded == QStringLiteral("default")
        || language.contains(QStringLiteral("默认")))
        return {};
    if (folded == QStringLiteral("eng")
        || folded == QStringLiteral("en")
        || folded.startsWith(QStringLiteral("en-"))
        || folded.contains(QStringLiteral("english")))
        return QStringLiteral("en");
    if (folded == QStringLiteral("chi_sim")
        || folded == QStringLiteral("chi-sim")
        || folded == QStringLiteral("zh")
        || folded.startsWith(QStringLiteral("zh-"))
        || language.contains(QStringLiteral("中文")))
        return QStringLiteral("zh");
    // The preferences page stores localized labels. Older config files may
    // contain a GBK-to-UTF-8 display form, so unknown non-ASCII labels map to
    // the supported Simplified Chinese option.
    for (const QChar character : language)
        if (character.unicode() > 0x7f)
            return QStringLiteral("zh");
    return language;
}

bool requestsTesseract(const QString &engine)
{
    return engine.trimmed().compare(QStringLiteral("tesseract"), Qt::CaseInsensitive) == 0;
}

QString tesseractLanguage(const QString &language)
{
    const QString normalized = normalizedOcrLanguage(language);
    if (normalized.isEmpty())
        return QStringLiteral("eng+chi_sim");
    if (normalized == QStringLiteral("en"))
        return QStringLiteral("eng");
    if (normalized == QStringLiteral("zh"))
        return QStringLiteral("chi_sim");
    return normalized;
}

#ifdef Q_OS_WIN
struct WindowsOcrImage {
    QImage pixels;
    QSize contentSize;
    int padding = 16;
};

WindowsOcrImage prepareWindowsOcrImage(const QImage &source, int maxDimension)
{
    WindowsOcrImage prepared;
    QImage image = source.convertToFormat(QImage::Format_RGB32);
    const int available = maxDimension - 2 * prepared.padding;
    const qreal scale = qMin<qreal>(1, qreal(available) / qMax(image.width(), image.height()));
    const QSize target(qMax(1, qRound(image.width() * scale)), qMax(1, qRound(image.height() * scale)));
    if (target != image.size())
        image = image.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    image.setDevicePixelRatio(1);
    prepared.contentSize = image.size();
    prepared.pixels = QImage(image.width() + prepared.padding * 2,
                             image.height() + prepared.padding * 2, QImage::Format_RGB32);
    prepared.pixels.fill(Qt::white);
    QPainter painter(&prepared.pixels);
    painter.drawImage(prepared.padding, prepared.padding, image);
    return prepared;
}

OcrResult recognizeWindowsImage(const QImage &image, int maxDimension,
                               const winrt::Windows::Media::Ocr::OcrEngine &engine)
{
    const auto prepared = prepareWindowsOcrImage(image, maxDimension);
    const QImage &rgba = prepared.pixels;
    const auto writer = winrt::Windows::Storage::Streams::DataWriter();
    const auto *begin = reinterpret_cast<const uint8_t *>(rgba.constBits());
    writer.WriteBytes(winrt::array_view<const uint8_t>(begin, begin + rgba.sizeInBytes()));
    const auto bitmap = winrt::Windows::Graphics::Imaging::SoftwareBitmap::CreateCopyFromBuffer(
        writer.DetachBuffer(), winrt::Windows::Graphics::Imaging::BitmapPixelFormat::Bgra8,
        rgba.width(), rgba.height(), winrt::Windows::Graphics::Imaging::BitmapAlphaMode::Ignore);
    const auto ocr = engine.RecognizeAsync(bitmap).get();
    QTransform orientation;
    if (const auto angle = ocr.TextAngle()) {
        // Windows reports word rectangles in its deskewed coordinate space.
        // Apply the reported rotation about the OCR bitmap's center before
        // removing padding or mapping back to the original source pixels.
        orientation.translate(rgba.width() / 2.0, rgba.height() / 2.0);
        orientation.rotate(angle.Value());
        orientation.translate(-rgba.width() / 2.0, -rgba.height() / 2.0);
    }
    OcrResult result;
    const qreal sx = qreal(image.width()) / prepared.contentSize.width();
    const qreal sy = qreal(image.height()) / prepared.contentSize.height();
    int lineIndex = 0;
    for (const auto &line : ocr.Lines()) {
        const QString text = QString::fromWCharArray(line.Text().c_str());
        if (text.isEmpty())
            continue;
        if (!result.text.isEmpty())
            result.text += QChar('\n');
        const qsizetype lineStart = result.text.size();
        result.text += text;
        qsizetype searchFrom = 0;
        for (const auto &word : line.Words()) {
            const QString wordText = QString::fromWCharArray(word.Text().c_str());
            if (wordText.isEmpty())
                continue;
            const qsizetype offset = text.indexOf(wordText, searchFrom);
            if (offset < 0)
                continue;
            const auto rect = word.BoundingRect();
            const QRectF positioned = orientation.mapRect(QRectF(rect.X, rect.Y, rect.Width, rect.Height));
            const QRectF bounds((positioned.x() - prepared.padding) * sx,
                                (positioned.y() - prepared.padding) * sy,
                                positioned.width() * sx, positioned.height() * sy);
            result.boxes.append({bounds.intersected(QRectF(image.rect())),
                                 lineStart + offset, wordText.size(), lineIndex});
            searchFrom = offset + wordText.size();
        }
        ++lineIndex;
    }
    if (result.text.isEmpty())
        result.text = QString::fromWCharArray(ocr.Text().c_str()).trimmed();
    result.success = !result.text.isEmpty();
    if (!result.success)
        result.error = QStringLiteral("没有识别到文字");
    return result;
}

OcrResult recognizeWithWindowsOcr(const QImage &image, const QString &language)
{
    // QtConcurrent runs this function on a worker thread. That thread does
    // not necessarily have a COM apartment yet, while WinRT requires one.
    // If Qt already initialized the thread in a different apartment, WinRT
    // can still use the existing apartment and reports RPC_E_CHANGED_MODE.
    bool initializedApartment = false;
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        initializedApartment = true;
    } catch (const winrt::hresult_error &error) {
        if (error.code() != RPC_E_CHANGED_MODE)
            return {false, {}, QStringLiteral("Windows 原生 OCR 初始化失败: %1")
                                     .arg(QString::fromWCharArray(error.message().c_str()))};
    }

    OcrResult result;
    try {
        {
            const QString requestedLanguage = normalizedOcrLanguage(language);
            auto engine = winrt::Windows::Media::Ocr::OcrEngine{nullptr};
            if (!requestedLanguage.isEmpty()) {
                const auto available =
                    winrt::Windows::Media::Ocr::OcrEngine::AvailableRecognizerLanguages();
                for (const auto &availableLanguage : available) {
                    const QString tag = QString::fromWCharArray(availableLanguage.LanguageTag().c_str());
                    if (tag.startsWith(requestedLanguage, Qt::CaseInsensitive)) {
                        engine = winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromLanguage(
                            availableLanguage);
                        if (engine)
                            break;
                    }
                }
                if (!engine)
                    result.error = QStringLiteral("Windows OCR 未安装所选语言包");
            } else {
                engine = winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromUserProfileLanguages();
            }
            if (!engine && result.error.isEmpty()) {
                // A user may have an OCR language pack installed without it
                // being the current Windows display language. Prefer Chinese
                // and English before falling back to the first installed pack.
                const auto available =
                    winrt::Windows::Media::Ocr::OcrEngine::AvailableRecognizerLanguages();
                const auto tryLanguage = [&engine, &available](const QString &prefix) {
                    for (const auto &availableLanguage : available) {
                        const QString tag = QString::fromWCharArray(availableLanguage.LanguageTag().c_str());
                        if (tag.startsWith(prefix, Qt::CaseInsensitive)) {
                            engine = winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromLanguage(availableLanguage);
                            if (engine)
                                return;
                        }
                    }
                };
                tryLanguage(QStringLiteral("zh"));
                if (!engine)
                    tryLanguage(QStringLiteral("en"));
                if (!engine) {
                    for (const auto &availableLanguage : available) {
                        engine = winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromLanguage(
                            availableLanguage);
                        if (engine)
                            break;
                    }
                }
            }
            if (!engine) {
                if (result.error.isEmpty())
                    result.error = QStringLiteral(
                        "Windows 未安装可用的 OCR 语言包。请在 Windows 设置中安装中文或英文语言包，"
                        "或将 Tesseract 加入 PATH。");
            } else {
                const auto limit = winrt::Windows::Media::Ocr::OcrEngine::MaxImageDimension();
                const int maxDimension = limit > 0 ? int(limit) : 2600;
                const QImage source = image.convertToFormat(QImage::Format_RGB32);
                result = recognizeWindowsImage(source, maxDimension, engine);
                const QImage enhanced = detail::enhanceOcrContrast(source);
                if (enhanced.cacheKey() != source.cacheKey()) {
                    // Retain the original pass for high-contrast words. A
                    // second system-OCR pass only repairs dim text regions.
                    try {
                        const auto recovered = recognizeWindowsImage(enhanced, maxDimension, engine);
                        result = detail::mergeLowContrastText(source, result, recovered);
                    } catch (const winrt::hresult_error &) {
                        // An optional retry cannot discard a usable result.
                    }
                }
            }
        }
    } catch (const winrt::hresult_error &error) {
        result.error = QStringLiteral("Windows 原生 OCR 失败: %1")
                           .arg(QString::fromWCharArray(error.message().c_str()));
    } catch (const std::exception &error) {
        result.error = QStringLiteral("Windows 原生 OCR 失败: %1")
                           .arg(QString::fromLocal8Bit(error.what()));
    }

    if (initializedApartment) {
        // Cached activation factories must not outlive the last COM apartment.
        // The next OCR request can run after this worker's apartment is gone.
        winrt::clear_factory_cache();
        winrt::uninit_apartment();
    }
    return result;
}
#endif

OcrResult recognizeWithTesseract(const QImage &image, const QString &language)
{
    const QString executable = QStandardPaths::findExecutable(QStringLiteral("tesseract"));
    if (executable.isEmpty()) {
        return {false,
                {},
                QStringLiteral("未找到可用的 OCR 引擎。请启用系统 OCR，或安装 Tesseract 并将其加入 PATH。")};
    }

    QTemporaryFile inputFile;
    inputFile.setFileTemplate(QDir::tempPath() + QStringLiteral("/snipaste-ocr-XXXXXX.png"));
    if (!inputFile.open() || !image.save(inputFile.fileName(), "PNG"))
        return {false, {}, QStringLiteral("无法创建 OCR 临时图像")};
    inputFile.close();

    QProcess process;
    process.setProgram(executable);
    process.setArguments({inputFile.fileName(), QStringLiteral("stdout"),
                          QStringLiteral("-l"), tesseractLanguage(language), QStringLiteral("tsv")});
    process.start();
    if (!process.waitForStarted(1000))
        return {false, {}, QStringLiteral("无法启动 Tesseract OCR")};
    if (!process.waitForFinished(15000)) {
        process.kill();
        return {false, {}, QStringLiteral("OCR 超时")};
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString error = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        return {false,
                {},
                error.isEmpty() ? QStringLiteral("Tesseract OCR 执行失败") : error};
    }

    return OcrService::parseTesseractTsv(process.readAllStandardOutput());
}

} // namespace

OcrResult OcrService::parseTesseractTsv(const QByteArray &data)
{
    OcrResult result;
    QByteArray previousLine;
    int lineIndex = -1;
    for (const QByteArray &row : data.split('\n')) {
        const auto columns = row.trimmed().split('\t');
        if (columns.size() < 12 || columns[0] != "5")
            continue;
        const QString word = QString::fromUtf8(columns[11]).trimmed();
        bool xOk = false, yOk = false, widthOk = false, heightOk = false;
        const int x = columns[6].toInt(&xOk), y = columns[7].toInt(&yOk);
        const int width = columns[8].toInt(&widthOk), height = columns[9].toInt(&heightOk);
        if (word.isEmpty() || !xOk || !yOk || !widthOk || !heightOk || width <= 0 || height <= 0)
            continue;
        const QByteArray line = columns[1] + '/' + columns[2] + '/' + columns[3] + '/' + columns[4];
        if (line != previousLine) {
            if (!result.text.isEmpty())
                result.text += QChar('\n');
            previousLine = line;
            ++lineIndex;
        } else {
            // Tesseract separates CJK words too; those separators are not
            // spaces in the source language.
            const auto cjk = [](QChar ch) { return ch.unicode() >= 0x2e80 && ch.unicode() <= 0x9fff; };
            if (!cjk(result.text.back()) || !cjk(word.front()))
                result.text += QChar(' ');
        }
        const qsizetype start = result.text.size();
        result.text += word;
        result.boxes.append({QRectF(x, y, width, height), start, word.size(), lineIndex});
    }
    result.success = !result.text.isEmpty();
    if (!result.success)
        result.error = QStringLiteral("没有识别到文字");
    return result;
}

OcrResult OcrService::recognize(const QImage &image,
                                const QString &engine,
                                const QString &language)
{
    if (image.isNull())
        return {false, {}, QStringLiteral("没有可识别的图像")};

    const bool forceTesseract = requestsTesseract(engine);
    const QString normalizedLanguage = normalizedOcrLanguage(language);
    OcrResult nativeResult;

#if defined(Q_OS_MACOS)
    if (!forceTesseract) {
        nativeResult = platform::recognizeWithMacVision(image, normalizedLanguage);
        if (nativeResult.success)
            return nativeResult;
    }
#endif

#ifdef Q_OS_WIN
    if (!forceTesseract) {
        nativeResult = recognizeWithWindowsOcr(image, normalizedLanguage);
        if (nativeResult.success)
            return nativeResult;
    }
#endif

    // Keep Tesseract as a portable fallback for the automatic/native mode,
    // while an explicit Tesseract selection always uses that engine.
    const OcrResult tesseractResult = recognizeWithTesseract(image, normalizedLanguage);
    if (tesseractResult.success)
        return tesseractResult;

    if (!nativeResult.error.isEmpty() && !tesseractResult.error.isEmpty())
        return {false, {}, nativeResult.error + QStringLiteral("\n") + tesseractResult.error};
    return tesseractResult;
}

} // namespace snipaste::core
