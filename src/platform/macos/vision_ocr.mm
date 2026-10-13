#include "platform/macos/vision_ocr.h"

#include <QBuffer>
#include <QByteArray>
#include <QStringList>

#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <Vision/Vision.h>

namespace snipaste::platform {

namespace {

QString qStringFromNSString(NSString *value)
{
    return value ? QString::fromUtf8([value UTF8String]) : QString();
}

void releaseObject(id object)
{
#if !__has_feature(objc_arc)
    [object release];
#else
    Q_UNUSED(object)
#endif
}

} // namespace

core::OcrResult recognizeWithMacVision(const QImage &image, const QString &language)
{
    if (image.isNull())
        return {false, {}, QStringLiteral("没有可识别的图像")};

    @autoreleasepool {
        // Encoding to PNG keeps the bridge independent of QImage's pixel
        // format and lets ImageIO handle color profiles and alpha correctly.
        QByteArray encoded;
        QBuffer buffer(&encoded);
        if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
            return {false, {}, QStringLiteral("无法转换 OCR 图像")};

        NSData *data = [NSData dataWithBytes:encoded.constData()
                                      length:static_cast<NSUInteger>(encoded.size())];
        CGImageSourceRef source = CGImageSourceCreateWithData((__bridge CFDataRef)data, nullptr);
        if (!source)
            return {false, {}, QStringLiteral("无法读取 OCR 图像")};
        CGImageRef cgImage = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
        CFRelease(source);
        if (!cgImage)
            return {false, {}, QStringLiteral("无法读取 OCR 图像")};

        __block QStringList lines;
        __block NSString *requestError = nil;
        VNRecognizeTextRequest *request = [[VNRecognizeTextRequest alloc]
            initWithCompletionHandler:^(VNRequest *completedRequest, NSError *error) {
                if (error) {
                    requestError = [error.localizedDescription copy];
                    return;
                }
                NSArray<VNRecognizedTextObservation *> *observations =
                    (NSArray<VNRecognizedTextObservation *> *)completedRequest.results;
                for (VNRecognizedTextObservation *observation in observations) {
                    VNRecognizedText *candidate = [[observation topCandidates:1] firstObject];
                    if (candidate.string.length > 0)
                        lines.append(qStringFromNSString(candidate.string));
                }
            }];
        request.recognitionLevel = VNRequestTextRecognitionLevelAccurate;
        request.usesLanguageCorrection = YES;
        if (@available(macOS 10.15, *)) {
            // Supplying a selected language improves recognition while the
            // default keeps the historical mixed Chinese/English behavior.
            const QString folded = language.trimmed().toCaseFolded();
            if (folded == QStringLiteral("en") || folded.startsWith(QStringLiteral("en-"))
                || folded.contains(QStringLiteral("english")))
                request.recognitionLanguages = @[ @"en-US" ];
            else if (folded == QStringLiteral("zh") || folded.startsWith(QStringLiteral("zh-"))
                     || folded.contains(QStringLiteral("中文")))
                request.recognitionLanguages = @[ @"zh-Hans" ];
            else
                request.recognitionLanguages = @[ @"zh-Hans", @"en-US" ];
        }

        VNImageRequestHandler *handler = [[VNImageRequestHandler alloc]
            initWithCGImage:cgImage options:@{}];
        NSError *error = nil;
        const BOOL performed = [handler performRequests:@[ request ] error:&error];
        CGImageRelease(cgImage);

        core::OcrResult result;
        if (!performed || error) {
            NSString *message = error ? error.localizedDescription : requestError;
            result.error = QStringLiteral("macOS Vision OCR 失败: %1")
                               .arg(qStringFromNSString(message));
        } else {
            result.text = lines.join(QChar('\n')).trimmed();
            if (result.text.isEmpty())
                result.error = QStringLiteral("没有识别到文字");
            else
                result.success = true;
        }

        releaseObject(handler);
        releaseObject(request);
#if !__has_feature(objc_arc)
        [requestError release];
#endif
        return result;
    }
}

} // namespace snipaste::platform
