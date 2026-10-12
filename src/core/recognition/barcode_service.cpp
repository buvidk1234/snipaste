#include "core/recognition/barcode_service.h"

#include <QDir>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>

namespace snipaste::core {

BarcodeResult BarcodeService::recognize(const QImage &image)
{
    if (image.isNull())
        return {false, {}, QStringLiteral("没有可识别的图像")};

    const QString executable = QStandardPaths::findExecutable(QStringLiteral("zbarimg"));
    if (executable.isEmpty()) {
        return {false,
                {},
                QStringLiteral("未找到 zbarimg。请安装 ZBar 并将 zbarimg.exe 加入 PATH。")};
    }

    QTemporaryFile inputFile;
    inputFile.setFileTemplate(QDir::tempPath() + QStringLiteral("/snipaste-barcode-XXXXXX.png"));
    if (!inputFile.open() || !image.save(inputFile.fileName(), "PNG"))
        return {false, {}, QStringLiteral("无法创建识别临时图像")};
    inputFile.close();

    QProcess process;
    process.setProgram(executable);
    process.setArguments({QStringLiteral("--quiet"), QStringLiteral("--raw"), inputFile.fileName()});
    process.start();
    if (!process.waitForStarted(1000))
        return {false, {}, QStringLiteral("无法启动 zbarimg")};
    if (!process.waitForFinished(10000)) {
        process.kill();
        return {false, {}, QStringLiteral("二维码/条码识别超时")};
    }

    const QString output = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    if (output.isEmpty())
        return {false, {}, QStringLiteral("没有识别到二维码或条码")};
    return {true, output, {}};
}

} // namespace snipaste::core
