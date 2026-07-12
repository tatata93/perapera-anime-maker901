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

namespace {

// 塗分け線(色トレス線)の同化テスト用セルを組み立てる: 彩色レイヤー(左半分x0-7=赤、
// 右半分x12-19=青、中央に幅4px[x8-11]の縦の透明ギャップ)+そのギャップを覆う幅4pxの
// 縦の色トレス線(黒、不透明)を持つセル
void addColorTraceGapCel(core::Cut& cut) {
    core::Cel& cel = cut.addCel("A");

    core::Layer& paint = cel.addLayer("彩色");
    core::Bitmap art(20, 8);
    art.fill({0, 0, 0, 0});
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) art.setPixel(x, y, {255, 0, 0, 255});
        for (int x = 12; x < 20; ++x) art.setPixel(x, y, {0, 0, 255, 255});
    }
    paint.addFrame().bitmap() = std::move(art);

    core::Layer& trace = cel.addLayer("トレス");
    trace.setRole(core::LayerRole::ColorTrace);
    core::Bitmap traceArt(20, 8);
    traceArt.fill({0, 0, 0, 0});
    for (int y = 0; y < 8; ++y) {
        for (int x = 8; x < 12; ++x) traceArt.setPixel(x, y, {0, 0, 0, 255});
    }
    trace.addFrame().bitmap() = std::move(traceArt);

    cut.setFrameCount(1);
    cel.setExposure(0, 0);
}

}  // namespace

TEST_CASE("renderCutFrame dissolves excluded color-trace lines into the surrounding fill color",
          "[core][compositor][colortrace]") {
    core::Cut cut("Cut 1");
    addColorTraceGapCel(cut);

    // 既定(トレス除外): 線の下は白(紙)のまま残らず、両側の塗り色(赤/青)のどちらかで埋まる
    const core::Bitmap out = core::renderCutFrame(cut, 0, 20, 8);
    for (int x = 8; x < 12; ++x) {
        const auto p = out.pixel(x, 4);
        REQUIRE(p.a == 255);
        const bool isRed = (p.r == 255 && p.g == 0 && p.b == 0);
        const bool isBlue = (p.r == 0 && p.g == 0 && p.b == 255);
        REQUIRE((isRed || isBlue));  // 白((255,255,255))にはならない
    }
    // 境界の左寄り(x8,9)は赤、右寄り(x10,11)は青に寄る(両側から同時に埋まる=線の中央付近が境界)
    REQUIRE(out.pixel(8, 4).r == 255);
    REQUIRE(out.pixel(11, 4).b == 255);
}

TEST_CASE("renderCutFrame keeps the raw color-trace line when includeColorTrace is true",
          "[core][compositor][colortrace]") {
    core::Cut cut("Cut 1");
    addColorTraceGapCel(cut);

    core::RenderOptions withTrace;
    withTrace.includeColorTrace = true;
    const core::Bitmap out = core::renderCutFrame(cut, 0, 20, 8, withTrace);
    for (int x = 8; x < 12; ++x) {
        const auto p = out.pixel(x, 4);
        REQUIRE(p.r == 0);
        REQUIRE(p.g == 0);
        REQUIRE(p.b == 0);
        REQUIRE(p.a == 255);
    }
}

TEST_CASE("renderCutFrame leaves cels without a ColorTrace layer byte-identical (no dissolve)",
          "[core][compositor][colortrace]") {
    core::Cut cut("Cut 1");
    core::Cel& cel = cut.addCel("A");
    core::Layer& paint = cel.addLayer("彩色");
    core::Bitmap art(20, 8);
    art.fill({0, 0, 0, 0});
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) art.setPixel(x, y, {255, 0, 0, 255});
        for (int x = 12; x < 20; ++x) art.setPixel(x, y, {0, 0, 255, 255});
    }
    paint.addFrame().bitmap() = std::move(art);
    cut.setFrameCount(1);
    cel.setExposure(0, 0);

    const core::Bitmap out = core::renderCutFrame(cut, 0, 20, 8);
    // ColorTraceレイヤーが無いので同化は起きず、ギャップ(x8-11)は従来どおり紙の白のまま
    for (int x = 8; x < 12; ++x) {
        const auto p = out.pixel(x, 4);
        REQUIRE(p.r == 255);
        REQUIRE(p.g == 255);
        REQUIRE(p.b == 255);
        REQUIRE(p.a == 255);
    }
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

TEST_CASE("renderCutFrame crops and resamples the camera frame", "[core][compositor][camera]") {
    core::Cut cut("Cut 1");
    core::Cel& cel = cut.addCel("A");
    core::Layer& layer = cel.addLayer("L");

    // 右下クオドラント(x,y: 4..7)だけ赤、残りは透明(=紙の白が見える)にする
    core::Bitmap bitmap(8, 8);
    bitmap.fill({0, 0, 0, 0});
    for (int y = 4; y < 8; ++y) {
        for (int x = 4; x < 8; ++x) bitmap.setPixel(x, y, {255, 0, 0, 255});
    }
    layer.addFrame().bitmap() = std::move(bitmap);
    cut.setFrameCount(1);
    cel.setExposure(0, 0);

    // キー無しは従来どおり(左上は白、右下は赤)。バイト単位で同一のはず
    const auto noKey = core::renderCutFrame(cut, 0, 8, 8);
    REQUIRE(noKey.pixel(1, 1).r == 255);
    REQUIRE(noKey.pixel(1, 1).g == 255);  // 白
    REQUIRE(noKey.pixel(6, 6).r == 255);
    REQUIRE(noKey.pixel(6, 6).g == 0);  // 赤

    // カメラフレーム: 右下クオドラント(中心(6,6)・scale=0.5)にズームインする
    cut.setCameraKey(0, core::CameraFrameState{{6.0f, 6.0f}, 0.5});
    const auto zoomed = core::renderCutFrame(cut, 0, 8, 8);
    // 元は白だった位置(左上寄り)がズームインにより赤になる
    REQUIRE(zoomed.pixel(1, 1).r == 255);
    REQUIRE(zoomed.pixel(1, 1).g == 0);
    REQUIRE(zoomed.pixel(4, 4).g == 0);
}

TEST_CASE("renderCutFrame supports oversized paper cels panned via position keys (引きセル)",
          "[core][compositor][paper]") {
    // 引きセル: キャンバス幅Wの2倍(横パン用)の背景セルを作り、左半分と右半分に色違いの目印を置く。
    // コマ0=オフセット0(セル左端がキャンバス左端、右半分の目印が画面内)、
    // コマN=オフセット-W(セルが左へずれ、右半分の紙の中身がキャンバス右側に来る=左パン)
    constexpr int kCanvasW = 8;
    constexpr int kCanvasH = 8;
    constexpr int kPaperW = kCanvasW * 2;

    core::Cut cut("Cut 1");
    core::Cel& cel = cut.addCel("BG");
    cel.setPaperSize(kPaperW, kCanvasH);
    core::Layer& layer = cel.addLayer("背景");

    core::Bitmap bitmap(kPaperW, kCanvasH);
    bitmap.fill({0, 0, 0, 0});
    // 左半分(紙のx=2)に赤、右半分(紙のx=10)に青の目印を置く
    bitmap.setPixel(2, 4, {255, 0, 0, 255});
    bitmap.setPixel(kCanvasW + 2, 4, {0, 0, 255, 255});
    layer.addFrame().bitmap() = std::move(bitmap);

    cut.setFrameCount(3);
    cel.setExposure(0, 0);
    cel.setExposure(1, 0);
    cel.setExposure(2, 0);
    cel.setPositionKey(0, {0.0f, 0.0f});                          // コマ0: オフセット0
    cel.setPositionKey(2, {static_cast<float>(-kCanvasW), 0.0f});  // コマ2: 左へキャンバス幅ぶんパン

    const auto f0 = core::renderCutFrame(cut, 0, kCanvasW, kCanvasH);
    // コマ0: 紙のx=2(赤)がキャンバスx=2に、紙のx=10(青)は画面外(クリップ)
    REQUIRE(f0.pixel(2, 4).r == 255);
    REQUIRE(f0.pixel(2, 4).b == 0);
    // 右半分(紙のx=10相当のキャンバス位置)は紙(白)のまま
    REQUIRE(f0.pixel(6, 4).r == 255);
    REQUIRE(f0.pixel(6, 4).g == 255);
    REQUIRE(f0.pixel(6, 4).b == 255);

    const auto f2 = core::renderCutFrame(cut, 2, kCanvasW, kCanvasH);
    // コマ2: オフセット-8適用後、紙のx=10(青)がキャンバスx=2に来る。紙のx=2(赤)は画面外
    REQUIRE(f2.pixel(2, 4).b == 255);
    REQUIRE(f2.pixel(2, 4).r == 0);
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
