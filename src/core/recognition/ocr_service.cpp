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

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
#include "platform/macos/vision_ocr.h"
#endif

namespace snipaste::core {

namespace {

#ifdef Q_OS_WIN
OcrResult recognizeWithWindowsOcr(const QImage &image)
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

            auto engine = winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromUserProfileLanguages();
            if (!engine) {
                // A user may have an OCR language pack installed without it
                // being the current Windows display language. Prefer Chinese
                // and English before falling back to the first installed pack.
                const auto available =
                    winrt::Windows::Media::Ocr::OcrEngine::AvailableRecognizerLanguages();
                const auto tryLanguage = [&engine, &available](const QString &prefix) {
                    for (const auto &language : available) {
                        const QString tag = QString::fromWCharArray(language.LanguageTag().c_str());
                        if (tag.startsWith(prefix, Qt::CaseInsensitive)) {
                            engine = winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromLanguage(language);
                            if (engine)
                                return;
                        }
                    }
                };
                tryLanguage(QStringLiteral("zh"));
                if (!engine)
                    tryLanguage(QStringLiteral("en"));
                if (!engine) {
                    for (const auto &language : available) {
                        engine = winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromLanguage(language);
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

OcrResult recognizeWithTesseract(const QImage &image)
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
                          QStringLiteral("-l"), QStringLiteral("eng+chi_sim")});
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

OcrResult OcrService::recognize(const QImage &image)
{
    if (image.isNull())
        return {false, {}, QStringLiteral("没有可识别的图像")};

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    const OcrResult visionResult = platform::recognizeWithMacVision(image);
    if (visionResult.success)
        return visionResult;
#endif

#ifdef Q_OS_WIN
    const OcrResult windowsResult = recognizeWithWindowsOcr(image);
    if (windowsResult.success)
        return windowsResult;
#endif

    // Keep Tesseract as a portable fallback for systems without a Windows
    // OCR language pack, and include the native error if both backends fail.
    const OcrResult tesseractResult = recognizeWithTesseract(image);
    if (tesseractResult.success)
        return tesseractResult;

#ifdef Q_OS_WIN
    if (!windowsResult.error.isEmpty() && !tesseractResult.error.isEmpty())
        return {false, {}, windowsResult.error + QStringLiteral("\n") + tesseractResult.error};
#endif
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    if (!visionResult.error.isEmpty() && !tesseractResult.error.isEmpty())
        return {false, {}, visionResult.error + QStringLiteral("\n") + tesseractResult.error};
#endif
    return tesseractResult;
}

} // namespace snipaste::core
