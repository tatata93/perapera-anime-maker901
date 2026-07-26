#include <catch2/catch_test_macros.hpp>

#include "ui/ExportSettings.h"

using namespace perapera::ui;

TEST_CASE("export formats report sequence and FFmpeg requirements", "[export]") {
    REQUIRE(isImageSequence(ExportFormat::PngSequence));
    REQUIRE(isImageSequence(ExportFormat::OpenExrSequence));
    REQUIRE_FALSE(isImageSequence(ExportFormat::Mp4H264));

    REQUIRE_FALSE(requiresFfmpeg(ExportFormat::PngSequence));
    REQUIRE(requiresFfmpeg(ExportFormat::OpenExrSequence));
    REQUIRE(requiresFfmpeg(ExportFormat::MovProRes));
}

TEST_CASE("export alpha support follows format and ProRes profile", "[export]") {
    REQUIRE(supportsAlpha(ExportFormat::PngSequence, 0));
    REQUIRE(supportsAlpha(ExportFormat::OpenExrSequence, 0));
    REQUIRE_FALSE(supportsAlpha(ExportFormat::MovProRes, 3));
    REQUIRE(supportsAlpha(ExportFormat::MovProRes, 4));
    REQUIRE_FALSE(supportsAlpha(ExportFormat::MovDnxhr, 5));
}

TEST_CASE("sequence prefixes are safe file names", "[export]") {
    REQUIRE(sanitizedSequencePrefix(QStringLiteral(" cut:A/ ")) == QStringLiteral("cut_A_"));
    REQUIRE(sanitizedSequencePrefix(QStringLiteral("   ")) == QStringLiteral("frame_"));
}

TEST_CASE("retiming duplicates and skips frames deterministically", "[export]") {
    REQUIRE(retimedFrameCount(3, 50) == 6);
    REQUIRE(retimedSourceIndex(0, 3, 50) == 0);
    REQUIRE(retimedSourceIndex(1, 3, 50) == 0);
    REQUIRE(retimedSourceIndex(2, 3, 50) == 1);

    REQUIRE(retimedFrameCount(5, 200) == 3);
    REQUIRE(retimedSourceIndex(0, 5, 200) == 0);
    REQUIRE(retimedSourceIndex(1, 5, 200) == 2);
    REQUIRE(retimedSourceIndex(2, 5, 200) == 4);
}
