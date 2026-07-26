#pragma once

#include <QString>

namespace perapera::ui {

enum class ExportFormat {
    PngSequence,
    TiffSequence,
    JpegSequence,
    OpenExrSequence,
    DpxSequence,
    Mp4H264,
    MovProRes,
    MovDnxhr,
};

enum class ExportScope {
    CurrentFrame,
    CurrentCutRange,
    CurrentCut,
    AllCuts,
};

enum class ExportContent {
    Drawing,
    Previz,
    Both,
};

struct ExportSettings {
    ExportFormat format = ExportFormat::Mp4H264;
    ExportScope scope = ExportScope::CurrentCut;
    ExportContent content = ExportContent::Drawing;
    QString outputPath;

    int fromFrame = 1;
    int toFrame = 1;
    int onlyCel = -1;
    bool includeColorTrace = false;
    bool includeCorrection = false;
    bool transparentBackground = false;

    int outputWidth = 1920;
    int outputHeight = 1080;
    bool preserveAspectRatio = true;
    double fps = 24.0;
    int playbackSpeedPercent = 100;

    int jpegQuality = 95;
    int pngCompression = 6;
    int exrCompression = 3;
    int dpxBitDepth = 10;
    int h264Crf = 18;
    QString h264Preset = QStringLiteral("medium");
    int proResProfile = 3;
    QString dnxhrProfile = QStringLiteral("dnxhr_hqx");

    QString sequencePrefix = QStringLiteral("frame_");
    int sequenceStartNumber = 1;
    int sequencePadding = 4;
};

bool isImageSequence(ExportFormat format);
bool requiresFfmpeg(ExportFormat format);
bool supportsAlpha(ExportFormat format, int proResProfile);
QString exportExtension(ExportFormat format);
QString findFfmpegExecutable();
QString sanitizedSequencePrefix(const QString& prefix);
int retimedFrameCount(int sourceFrameCount, int playbackSpeedPercent);
int retimedSourceIndex(int outputFrame, int sourceFrameCount, int playbackSpeedPercent);

}  // namespace perapera::ui
