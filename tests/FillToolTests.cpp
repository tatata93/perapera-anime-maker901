#include <catch2/catch_test_macros.hpp>

#include "core/BrushEngine.h"
#include "core/FillTool.h"

namespace {

core::Bitmap makeTransparent(int w, int h) {
    core::Bitmap bitmap(w, h);
    bitmap.fill({0, 0, 0, 0});
    return bitmap;
}

// lineLayerに矩形の枠線(不透明)を描く
void drawRectOutline(core::Bitmap& bitmap, int x0, int y0, int x1, int y1) {
    for (int x = x0; x <= x1; ++x) {
        bitmap.setPixel(x, y0, {0, 0, 0, 255});
        bitmap.setPixel(x, y1, {0, 0, 0, 255});
    }
    for (int y = y0; y <= y1; ++y) {
        bitmap.setPixel(x0, y, {0, 0, 0, 255});
        bitmap.setPixel(x1, y, {0, 0, 0, 255});
    }
}

}  // namespace

TEST_CASE("floodFill fills a closed region bounded by another layer's lines", "[core][fill]") {
    auto lineLayer = makeTransparent(64, 64);
    auto paintLayer = makeTransparent(64, 64);
    drawRectOutline(lineLayer, 10, 10, 50, 50);

    const auto dirty =
        core::floodFill(paintLayer, {&lineLayer, &paintLayer}, 30, 30, {255, 0, 0, 255}, 64, /*dilatePx=*/0);

    REQUIRE_FALSE(dirty.isEmpty());
    // 枠内は塗られる
    REQUIRE(paintLayer.pixel(30, 30).r == 255);
    REQUIRE(paintLayer.pixel(11, 11).r == 255);
    // 枠外は塗られない
    REQUIRE(paintLayer.pixel(5, 5).a == 0);
    REQUIRE(paintLayer.pixel(55, 30).a == 0);
    // 線レイヤー自体は書き換えない
    REQUIRE(lineLayer.pixel(10, 10).a == 255);
}

TEST_CASE("floodFill recolors an already filled region", "[core][fill]") {
    auto lineLayer = makeTransparent(64, 64);
    auto paintLayer = makeTransparent(64, 64);
    drawRectOutline(lineLayer, 10, 10, 50, 50);

    core::floodFill(paintLayer, {&lineLayer, &paintLayer}, 30, 30, {255, 0, 0, 255}, 64, /*dilatePx=*/0);
    const auto dirty =
        core::floodFill(paintLayer, {&lineLayer, &paintLayer}, 30, 30, {0, 0, 255, 255}, 64, /*dilatePx=*/0);

    REQUIRE_FALSE(dirty.isEmpty());
    REQUIRE(paintLayer.pixel(30, 30).b == 255);
    REQUIRE(paintLayer.pixel(11, 11).b == 255);
    REQUIRE(paintLayer.pixel(5, 5).a == 0);
}

TEST_CASE("floodFill recolors one side while color-trace boundaries still block", "[core][fill]") {
    auto lineLayer = makeTransparent(64, 64);
    auto colorTraceLayer = makeTransparent(64, 64);
    auto paintLayer = makeTransparent(64, 64);
    drawRectOutline(lineLayer, 10, 10, 50, 50);
    for (int y = 11; y < 50; ++y) {
        colorTraceLayer.setPixel(32, y, {255, 0, 0, 255});
    }

    std::vector<const core::Bitmap*> boundary{&lineLayer, &colorTraceLayer, &paintLayer};
    core::floodFill(paintLayer, boundary, 20, 30, {255, 0, 0, 255}, 64, /*dilatePx=*/0);
    core::floodFill(paintLayer, boundary, 40, 30, {0, 0, 255, 255}, 64, /*dilatePx=*/0);
    const auto dirty = core::floodFill(paintLayer, boundary, 20, 30, {0, 255, 0, 255}, 64, /*dilatePx=*/0);

    REQUIRE_FALSE(dirty.isEmpty());
    REQUIRE(paintLayer.pixel(20, 30).g == 255);
    REQUIRE(paintLayer.pixel(40, 30).b == 255);
    REQUIRE(paintLayer.pixel(32, 30).a == 0);
}

TEST_CASE("floodFill on a line pixel is a no-op", "[core][fill]") {
    auto lineLayer = makeTransparent(32, 32);
    auto paintLayer = makeTransparent(32, 32);
    drawRectOutline(lineLayer, 5, 5, 25, 25);

    const auto dirty = core::floodFill(paintLayer, {&lineLayer}, 5, 5, {255, 0, 0, 255});
    REQUIRE(dirty.isEmpty());
    REQUIRE(paintLayer.pixel(15, 15).a == 0);
}

TEST_CASE("floodFill dilation covers anti-aliased edges", "[core][fill]") {
    auto lineLayer = makeTransparent(64, 64);
    auto paintLayer = makeTransparent(64, 64);
    drawRectOutline(lineLayer, 10, 10, 50, 50);

    core::floodFill(paintLayer, {&lineLayer, &paintLayer}, 30, 30, {0, 255, 0, 255}, 64, /*dilatePx=*/2);

    // 膨張により線の直下(枠線位置)まで塗りが届く
    REQUIRE(paintLayer.pixel(10, 30).a == 255);
    // 枠のさらに外(2px超)へは漏れない
    REQUIRE(paintLayer.pixel(7, 30).a == 0);
}

TEST_CASE("floodFill without boundary fills the whole bitmap", "[core][fill]") {
    auto paintLayer = makeTransparent(16, 16);
    const auto dirty = core::floodFill(paintLayer, {&paintLayer}, 8, 8, {1, 2, 3, 255}, 64, 0);
    REQUIRE(dirty.width() == 16);
    REQUIRE(dirty.height() == 16);
    REQUIRE(paintLayer.pixel(0, 0).b == 3);
    REQUIRE(paintLayer.pixel(15, 15).b == 3);
}
