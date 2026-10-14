#include "core/recognition/ocr_service.h"

#include <QDir>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>

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
        QImage rgba = image.convertToFormat(QImage::Format_RGB32);
        const uint32_t maxDimension = winrt::Windows::Media::Ocr::OcrEngine::MaxImageDimension();
        if (maxDimension > 0
            && (rgba.width() > static_cast<int>(maxDimension)
                || rgba.height() > static_cast<int>(maxDimension))) {
            rgba = rgba.scaled(static_cast<int>(maxDimension),
                               static_cast<int>(maxDimension),
                               Qt::KeepAspectRatio,
                               Qt::SmoothTransformation);
        }
        if (rgba.isNull() || rgba.width() <= 0 || rgba.height() <= 0) {
            result.error = QStringLiteral("无法转换 OCR 图像");
        } else {
            const auto writer = winrt::Windows::Storage::Streams::DataWriter();
            const auto *begin = reinterpret_cast<const uint8_t *>(rgba.constBits());
            const auto *end = begin + rgba.sizeInBytes();
            writer.WriteBytes(winrt::array_view<const uint8_t>(begin, end));
            const auto buffer = writer.DetachBuffer();

            const auto bitmap = winrt::Windows::Graphics::Imaging::SoftwareBitmap::CreateCopyFromBuffer(
                buffer,
                winrt::Windows::Graphics::Imaging::BitmapPixelFormat::Bgra8,
                rgba.width(),
                rgba.height(),
                winrt::Windows::Graphics::Imaging::BitmapAlphaMode::Ignore);

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
                result.error = QStringLiteral(
                    "Windows 未安装可用的 OCR 语言包。请在 Windows 设置中安装中文或英文语言包，"
                    "或将 Tesseract 加入 PATH。");
            } else {
                const auto ocr = engine.RecognizeAsync(bitmap).get();
                const QString text = QString::fromWCharArray(ocr.Text().c_str()).trimmed();
                if (text.isEmpty())
                    result.error = QStringLiteral("没有识别到文字");
                else {
                    result.success = true;
                    result.text = text;
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

    if (initializedApartment)
        winrt::uninit_apartment();
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
                          QStringLiteral("-l"), tesseractLanguage(language)});
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

    const QString text = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    if (text.isEmpty())
        return {false, {}, QStringLiteral("没有识别到文字")};
    return {true, text, {}};
}

} // namespace

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
