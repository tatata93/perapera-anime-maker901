#include "ExportSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QRegularExpression>
#include <QStandardPaths>
#include <algorithm>

namespace perapera::ui {

bool isImageSequence(ExportFormat format) {
    switch (format) {
        case ExportFormat::PngSequence:
        case ExportFormat::TiffSequence:
        case ExportFormat::JpegSequence:
        case ExportFormat::OpenExrSequence:
        case ExportFormat::DpxSequence:
            return true;
        default:
            return false;
    }
}

bool requiresFfmpeg(ExportFormat format) {
    return format == ExportFormat::OpenExrSequence || format == ExportFormat::DpxSequence ||
           !isImageSequence(format);
}

bool supportsAlpha(ExportFormat format, int proResProfile) {
    switch (format) {
        case ExportFormat::PngSequence:
        case ExportFormat::TiffSequence:
        case ExportFormat::OpenExrSequence:
            return true;
        case ExportFormat::MovProRes:
            return proResProfile >= 4;
        default:
            return false;
    }
}

QString exportExtension(ExportFormat format) {
    switch (format) {
        case ExportFormat::PngSequence:
            return QStringLiteral("png");
        case ExportFormat::TiffSequence:
            return QStringLiteral("tif");
        case ExportFormat::JpegSequence:
            return QStringLiteral("jpg");
        case ExportFormat::OpenExrSequence:
            return QStringLiteral("exr");
        case ExportFormat::DpxSequence:
            return QStringLiteral("dpx");
        case ExportFormat::Mp4H264:
            return QStringLiteral("mp4");
        case ExportFormat::MovProRes:
        case ExportFormat::MovDnxhr:
            return QStringLiteral("mov");
    }
    return QStringLiteral("png");
}

QString findFfmpegExecutable() {
    const QString appDir = QCoreApplication::applicationDirPath();
    QString path = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"), QStringList{appDir});
    if (path.isEmpty()) path = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    return QDir::toNativeSeparators(path);
}

QString sanitizedSequencePrefix(const QString& prefix) {
    QString result = prefix.trimmed();
    result.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|%])")), QStringLiteral("_"));
    if (result.isEmpty()) result = QStringLiteral("frame_");
    return result;
}

int retimedFrameCount(int sourceFrameCount, int playbackSpeedPercent) {
    if (sourceFrameCount <= 0) return 0;
    const int speed = std::clamp(playbackSpeedPercent, 10, 800);
    return std::max(1, (sourceFrameCount * 100 + speed - 1) / speed);
}

int retimedSourceIndex(int outputFrame, int sourceFrameCount, int playbackSpeedPercent) {
    if (sourceFrameCount <= 0) return 0;
    const int speed = std::clamp(playbackSpeedPercent, 10, 800);
    const long long source = static_cast<long long>(std::max(0, outputFrame)) * speed / 100;
    return std::min(sourceFrameCount - 1, static_cast<int>(source));
}

}  // namespace perapera::ui
