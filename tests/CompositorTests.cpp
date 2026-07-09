#include <catch2/catch_test_macros.hpp>

#include "core/Compositor.h"

namespace {

// 指定色の1点だけを持つ透明セルを動画として持つレイヤーを作る
void addDrawingWithDot(core::Layer& layer, int x, int y, core::Bitmap::Pixel color, int w = 8, int h = 8) {
    core::Bitmap bitmap(w, h);
    bitmap.fill({0, 0, 0, 0});
    bitmap.setPixel(x, y, color);
    layer.addFrame().bitmap() = std::move(bitmap);
}

}  // namespace

TEST_CASE("renderCutFrame composites paper, cels and layers", "[core][compositor]") {
    core::Cut cut("Cut 1");
    core::Cel& celA = cut.addCel("A");
    core::Layer& lineA = celA.addLayer("線画");
    addDrawingWithDot(lineA, 2, 2, {0, 0, 0, 255});

    core::Cel& celB = cut.addCel("B");
    core::Layer& lineB = celB.addLayer("線画");
    addDrawingWithDot(lineB, 5, 5, {255, 0, 0, 255});

    cut.setFrameCount(1);
    celA.setExposure(0, 0);
    celB.setExposure(0, 0);

    const core::Bitmap out = core::renderCutFrame(cut, 0, 8, 8);

    REQUIRE(out.pixel(0, 0).r == 255);  // 紙は白
    REQUIRE(out.pixel(0, 0).a == 255);  // 不透明
    REQUIRE(out.pixel(2, 2).r == 0);    // セルAの黒点
    REQUIRE(out.pixel(5, 5).r == 255);  // セルBの赤点
    REQUIRE(out.pixel(5, 5).g == 0);
}

TEST_CASE("renderCutFrame respects exposure and empty frames", "[core][compositor]") {
    core::Cut cut("Cut 1");
    core::Cel& cel = cut.addCel("A");
    core::Layer& layer = cel.addLayer("線画");
    addDrawingWithDot(layer, 1, 1, {0, 0, 0, 255});  // 動画1
    addDrawingWithDot(layer, 6, 6, {0, 0, 0, 255});  // 動画2

    cut.setFrameCount(3);
    cel.setExposure(0, 0);
    cel.setExposure(1, 1);
    // コマ3は空欄(-1)

    const auto f0 = core::renderCutFrame(cut, 0, 8, 8);
    const auto f1 = core::renderCutFrame(cut, 1, 8, 8);
    const auto f2 = core::renderCutFrame(cut, 2, 8, 8);

    REQUIRE(f0.pixel(1, 1).r == 0);
    REQUIRE(f0.pixel(6, 6).r == 255);  // 動画2は出ない
    REQUIRE(f1.pixel(6, 6).r == 0);
    REQUIRE(f1.pixel(1, 1).r == 255);
    REQUIRE(f2.pixel(1, 1).r == 255);  // 空欄コマは紙のみ
    REQUIRE(f2.pixel(6, 6).r == 255);
}

TEST_CASE("renderCutFrame excludes trace/correction roles by default", "[core][compositor]") {
    core::Cut cut("Cut 1");
    core::Cel& cel = cut.addCel("A");
    core::Layer& normal = cel.addLayer("線画");
    addDrawingWithDot(normal, 1, 1, {0, 0, 0, 255});
    core::Layer& trace = cel.addLayer("トレス");
    trace.setRole(core::LayerRole::ColorTrace);
    addDrawingWithDot(trace, 3, 3, {255, 0, 0, 255});
    core::Layer& corr = cel.addLayer("修正");
    corr.setRole(core::LayerRole::Correction);
    addDrawingWithDot(corr, 5, 5, {0, 0, 255, 255});

    cut.setFrameCount(1);
    cel.setExposure(0, 0);

    const auto final = core::renderCutFrame(cut, 0, 8, 8);
    REQUIRE(final.pixel(1, 1).r == 0);    // 通常レイヤーは出る
    REQUIRE(final.pixel(3, 3).r == 255);  // トレス線は出ない(白のまま)
    REQUIRE(final.pixel(3, 3).g == 255);
    REQUIRE(final.pixel(5, 5).b == 255);  // 修正は出ない(白のまま=bも255)
    REQUIRE(final.pixel(5, 5).r == 255);

    core::RenderOptions withTrace;
    withTrace.includeColorTrace = true;
    const auto check = core::renderCutFrame(cut, 0, 8, 8, withTrace);
    REQUIRE(check.pixel(3, 3).r == 255);  // トレス線(赤)が出る
    REQUIRE(check.pixel(3, 3).g == 0);
}

TEST_CASE("renderCutFrame onlyCel exports a single cel", "[core][compositor]") {
    core::Cut cut("Cut 1");
    core::Cel& celA = cut.addCel("A");
    addDrawingWithDot(celA.addLayer("L"), 2, 2, {0, 0, 0, 255});
    core::Cel& celB = cut.addCel("B");
    addDrawingWithDot(celB.addLayer("L"), 5, 5, {255, 0, 0, 255});
    cut.setFrameCount(1);
    celA.setExposure(0, 0);
    celB.setExposure(0, 0);

    core::RenderOptions onlyB;
    onlyB.onlyCel = 1;
    const auto out = core::renderCutFrame(cut, 0, 8, 8, onlyB);
    REQUIRE(out.pixel(2, 2).r == 255);  // セルAは出ない
    REQUIRE(out.pixel(5, 5).r == 255);  // セルBの赤
    REQUIRE(out.pixel(5, 5).g == 0);
}

TEST_CASE("renderCutFrame blends semi-transparent pixels over paper", "[core][compositor]") {
    core::Cut cut("Cut 1");
    core::Cel& cel = cut.addCel("A");
    core::Layer& layer = cel.addLayer("L");
    addDrawingWithDot(layer, 4, 4, {0, 0, 0, 128});  // 半透明の黒
    cut.setFrameCount(1);
    cel.setExposure(0, 0);

    const auto out = core::renderCutFrame(cut, 0, 8, 8);
    // 白紙(255)と黒(0)のほぼ中間になる
    REQUIRE(out.pixel(4, 4).r > 100);
    REQUIRE(out.pixel(4, 4).r < 155);
    REQUIRE(out.pixel(4, 4).a == 255);
}
