#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstring>

#include "core/Compositor.h"
#include "core/Cut.h"
#include "core/Multiplane.h"

namespace {

// 全面透明のアートワークを作る
core::Bitmap makeTransparent(int w, int h) {
    core::Bitmap bmp(w, h);
    bmp.fill({0, 0, 0, 0});
    return bmp;
}

// 中心付近に1px の目印(不透明)を置いた透明アートワークを作る
core::Bitmap makeMarker(int w, int h, int mx, int my, core::Bitmap::Pixel color) {
    core::Bitmap bmp = makeTransparent(w, h);
    bmp.setPixel(mx, my, color);
    return bmp;
}

// 「白からどれだけ離れているか(=赤み)」を重みとして使い、画像全体の重心x座標を求める。
// 単一の目印(赤)がバイリニア補間やレンズボケでピクセルにまたがって出ても位置を検出できる
double weightedCentroidX(const core::Bitmap& img) {
    double sumW = 0.0;
    double sumWX = 0.0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const core::Bitmap::Pixel p = img.pixel(x, y);
            const double w = 255.0 - p.g;  // 背景は白(g=255)、赤い目印はg=0
            sumW += w;
            sumWX += w * x;
        }
    }
    REQUIRE(sumW > 0.0);
    return sumWX / sumW;
}

// (x,y)の重み(赤み)を返す
double weightAt(const core::Bitmap& img, int x, int y) {
    if (x < 0 || y < 0 || x >= img.width() || y >= img.height()) return 0.0;
    return 255.0 - img.pixel(x, y).g;
}

// 画像内で最も重み(赤み)が強いピクセルを探す
std::pair<int, int> findPeak(const core::Bitmap& img) {
    int bestX = 0, bestY = 0;
    double best = -1.0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const double w = weightAt(img, x, y);
            if (w > best) {
                best = w;
                bestX = x;
                bestY = y;
            }
        }
    }
    return {bestX, bestY};
}

// ピンホール投影の期待画素位置(spec通りの式)
double expectedPinholePixel(double wx, double focal, double distanceMm, double sensorWidthMm, int width) {
    return width * (0.5 + wx * focal / (distanceMm * sensorWidthMm));
}

}  // namespace

TEST_CASE("renderMultiplane pinhole projects a centered marker to the image center", "[core][multiplane]") {
    core::Bitmap art = makeMarker(40, 40, 20, 20, {255, 0, 0, 255});  // ビットマップ中央の目印

    core::MultiplanePlane plane;
    plane.artwork = &art;
    plane.distanceMm = 500.0;
    plane.widthMm = 400.0;

    core::MultiplaneCamera camera;
    camera.focalLengthMm = 50.0;
    camera.sensorWidthMm = 36.0;
    camera.apertureFStop = 0.0;  // ピンホール
    camera.focusDistanceMm = 500.0;

    const core::Bitmap out = core::renderMultiplane({plane}, camera, 100, 100, 1, 1);

    const double cx = weightedCentroidX(out);
    REQUIRE(std::abs(cx - 50.0) <= 2.0);
}

TEST_CASE("renderMultiplane pinhole projects an offset marker to the expected pixel", "[core][multiplane]") {
    // ビットマップ中央からずれた位置(列30)に目印を置く。テクセル中心は u=mx+0.5 とみなす
    core::Bitmap art = makeMarker(40, 40, 30, 20, {255, 0, 0, 255});

    core::MultiplanePlane plane;
    plane.artwork = &art;
    plane.distanceMm = 500.0;
    plane.widthMm = 400.0;

    core::MultiplaneCamera camera;
    camera.focalLengthMm = 50.0;
    camera.sensorWidthMm = 36.0;
    camera.apertureFStop = 0.0;
    camera.focusDistanceMm = 500.0;

    const core::Bitmap out = core::renderMultiplane({plane}, camera, 100, 100, 1, 1);

    // 目印テクセル(列30)のワールドx座標(mm)
    const double u = 30.5;
    const double wx = ((u / art.width()) - 0.5) * plane.widthMm;
    const double expectedPx = expectedPinholePixel(wx, camera.focalLengthMm, plane.distanceMm, camera.sensorWidthMm, 100);

    const double cx = weightedCentroidX(out);
    REQUIRE(std::abs(cx - expectedPx) <= 2.0);
}

TEST_CASE("renderMultiplane draws a farther plane smaller (parallax/scale)", "[core][multiplane]") {
    // 縦バー(列16-23が不透明)を持つアートワークを、距離D と 2D に置いて描画幅を比較する
    core::Bitmap art = makeTransparent(40, 40);
    for (int y = 0; y < 40; ++y) {
        for (int x = 16; x < 24; ++x) art.setPixel(x, y, {255, 0, 0, 255});
    }

    core::MultiplaneCamera camera;
    camera.focalLengthMm = 50.0;
    camera.sensorWidthMm = 36.0;
    camera.apertureFStop = 0.0;
    camera.focusDistanceMm = 500.0;

    const auto barWidthAt = [&](double distanceMm) -> int {
        core::MultiplanePlane plane;
        plane.artwork = &art;
        plane.distanceMm = distanceMm;
        plane.widthMm = 400.0;
        const core::Bitmap out = core::renderMultiplane({plane}, camera, 100, 100, 1, 1);

        const int row = 50;
        double maxW = 0.0;
        for (int x = 0; x < out.width(); ++x) maxW = std::max(maxW, weightAt(out, x, row));
        REQUIRE(maxW > 0.0);
        int count = 0;
        for (int x = 0; x < out.width(); ++x) {
            if (weightAt(out, x, row) > maxW * 0.5) ++count;
        }
        return count;
    };

    const int widthNear = barWidthAt(500.0);
    const int widthFar = barWidthAt(1000.0);

    // 2倍遠い平面は描画幅がおよそ半分になる
    REQUIRE(widthFar > 0);
    REQUIRE(std::abs(widthFar - widthNear / 2) <= 2);
}

TEST_CASE("renderMultiplane depth of field: focus plane is sharp, other planes blur", "[core][multiplane]") {
    core::Bitmap art = makeMarker(40, 40, 20, 20, {255, 0, 0, 255});  // 中央の目印

    core::MultiplaneCamera camera;
    camera.focalLengthMm = 50.0;
    camera.sensorWidthMm = 36.0;
    camera.apertureFStop = 2.0;    // 開放
    camera.focusDistanceMm = 500.0;

    core::MultiplanePlane focusPlane;
    focusPlane.artwork = &art;
    focusPlane.distanceMm = 500.0;  // ピントが合う距離
    focusPlane.widthMm = 400.0;

    core::MultiplanePlane defocusPlane;
    defocusPlane.artwork = &art;
    defocusPlane.distanceMm = 250.0;  // ピントが合わない距離
    defocusPlane.widthMm = 400.0;

    const core::Bitmap outFocus = core::renderMultiplane({focusPlane}, camera, 100, 100, 64, 1);
    const core::Bitmap outDefocus = core::renderMultiplane({defocusPlane}, camera, 100, 100, 64, 1);

    // ピークピクセル周辺(半径2px)にどれだけエネルギーが集中しているか
    const auto concentrationRatio = [](const core::Bitmap& img) {
        const auto [px, py] = findPeak(img);
        double total = 0.0;
        double nearPeak = 0.0;
        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                const double w = weightAt(img, x, y);
                total += w;
                if (std::abs(x - px) <= 2 && std::abs(y - py) <= 2) nearPeak += w;
            }
        }
        REQUIRE(total > 0.0);
        return nearPeak / total;
    };

    const double focusRatio = concentrationRatio(outFocus);
    const double defocusRatio = concentrationRatio(outDefocus);

    REQUIRE(focusRatio > 0.9);    // シャープ: ほぼ全エネルギーがピーク近傍に集中
    REQUIRE(defocusRatio < 0.6);  // ボケ: 周囲に広く拡散

    // 決定論性: 同じseedで2回描画するとバイト同一になる
    const core::Bitmap outDefocus2 = core::renderMultiplane({defocusPlane}, camera, 100, 100, 64, 1);
    REQUIRE(outDefocus.width() == outDefocus2.width());
    REQUIRE(outDefocus.height() == outDefocus2.height());
    REQUIRE(std::memcmp(outDefocus.data(), outDefocus2.data(), outDefocus.byteSize()) == 0);
}

TEST_CASE("renderMultiplane composites planes front-to-back with straight-alpha over", "[core][multiplane]") {
    // 手前: 左半分(列0-19)が不透明緑、右半分(列20-39)は透明
    core::Bitmap front = makeTransparent(40, 40);
    for (int y = 0; y < 40; ++y) {
        for (int x = 0; x < 20; ++x) front.setPixel(x, y, {0, 180, 0, 255});
    }

    // 奥: 全面不透明赤、ただし右下1/4(列20-39, 行20-39)は透明
    core::Bitmap back = makeTransparent(40, 40);
    for (int y = 0; y < 40; ++y) {
        for (int x = 0; x < 40; ++x) {
            if (x >= 20 && y >= 20) continue;  // 右下は透明のまま
            back.setPixel(x, y, {255, 0, 0, 255});
        }
    }

    core::MultiplanePlane frontPlane;
    frontPlane.artwork = &front;
    frontPlane.distanceMm = 300.0;
    frontPlane.widthMm = 300.0;  // 視野(216mm)より広く、フレーム全体を覆う

    core::MultiplanePlane backPlane;
    backPlane.artwork = &back;
    backPlane.distanceMm = 600.0;
    backPlane.widthMm = 500.0;  // 視野(432mm)より広く、フレーム全体を覆う

    core::MultiplaneCamera camera;
    camera.focalLengthMm = 50.0;
    camera.sensorWidthMm = 36.0;
    camera.apertureFStop = 0.0;  // ピンホール
    camera.focusDistanceMm = 500.0;

    const core::Bitmap out = core::renderMultiplane({frontPlane, backPlane}, camera, 100, 100, 1, 1);

    // 左半分: 手前の緑が奥を隠す
    const core::Bitmap::Pixel left = out.pixel(20, 50);
    REQUIRE(left.g > left.r);
    REQUIRE(left.g > 100);

    // 右上: 手前は透明、奥の赤が見える
    const core::Bitmap::Pixel rightTop = out.pixel(80, 20);
    REQUIRE(rightTop.r > rightTop.g);
    REQUIRE(rightTop.r > 100);

    // 右下: 手前・奥とも透明 → 紙(白)
    const core::Bitmap::Pixel rightBottom = out.pixel(80, 80);
    REQUIRE(rightBottom.r > 240);
    REQUIRE(rightBottom.g > 240);
    REQUIRE(rightBottom.b > 240);
}

TEST_CASE("renderMultiplane pinhole is nearly identical with samples=1 and samples=16", "[core][multiplane]") {
    core::Bitmap art = makeMarker(40, 40, 20, 20, {255, 0, 0, 255});

    core::MultiplanePlane plane;
    plane.artwork = &art;
    plane.distanceMm = 500.0;
    plane.widthMm = 400.0;

    core::MultiplaneCamera camera;
    camera.focalLengthMm = 50.0;
    camera.sensorWidthMm = 36.0;
    camera.apertureFStop = 0.0;  // ピンホール(ジッタのみがサンプル間の差)
    camera.focusDistanceMm = 500.0;

    const core::Bitmap out1 = core::renderMultiplane({plane}, camera, 100, 100, 1, 1);
    const core::Bitmap out16 = core::renderMultiplane({plane}, camera, 100, 100, 16, 1);

    const double cx1 = weightedCentroidX(out1);
    const double cx16 = weightedCentroidX(out16);
    REQUIRE(std::abs(cx1 - cx16) <= 2.0);
    REQUIRE(std::abs(cx1 - 50.0) <= 2.0);
    REQUIRE(std::abs(cx16 - 50.0) <= 2.0);
}

// --- ここから: Cut/Compositor統合(Phase 17クラシック撮影) ---

TEST_CASE("renderCutFrame with multiplane disabled matches legacy digital compositing", "[core][compositor][multiplane]") {
    core::Cut cut("Cut 1");
    core::Cel& celA = cut.addCel("A");
    core::Layer& lineA = celA.addLayer("線画");
    core::Bitmap bmpA(8, 8);
    bmpA.fill({0, 0, 0, 0});
    bmpA.setPixel(2, 2, {0, 0, 0, 255});
    lineA.addFrame().bitmap() = bmpA;

    core::Cel& celB = cut.addCel("B");
    core::Layer& lineB = celB.addLayer("線画");
    core::Bitmap bmpB(8, 8);
    bmpB.fill({0, 0, 0, 0});
    bmpB.setPixel(5, 5, {255, 0, 0, 255});
    lineB.addFrame().bitmap() = bmpB;

    cut.setFrameCount(1);
    celA.setExposure(0, 0);
    celB.setExposure(0, 0);

    // 既定はデジタル合成(従来経路)。マルチプレーンを持たせても無効なら影響しない
    REQUIRE_FALSE(cut.multiplane().enabled);
    cut.multiplane().planes.push_back({0, 500.0, 400.0});  // 無効時は割付が残っていても無視される

    const core::Bitmap out = core::renderCutFrame(cut, 0, 8, 8);

    REQUIRE(out.pixel(0, 0).r == 255);  // 紙は白
    REQUIRE(out.pixel(0, 0).a == 255);
    REQUIRE(out.pixel(2, 2).r == 0);    // セルAの黒点
    REQUIRE(out.pixel(5, 5).r == 255);  // セルBの赤点
    REQUIRE(out.pixel(5, 5).g == 0);
}

TEST_CASE("renderCutFrame classic multiplane draws a farther plane's marker closer to center (parallax)",
          "[core][compositor][multiplane]") {
    // 同じ目印位置(セル内テクセル位置は同じ)を持つ2つのカットを、距離違いの単一段として撮影し、
    // 画面上の目印位置(中心からの距離)を比較する。ピンホール投影では距離が遠いほど中心寄りになる
    const auto renderAtDistance = [](double distanceMm) {
        core::Cut cut("Cut");
        core::Cel& cel = cut.addCel("A");
        core::Layer& layer = cel.addLayer("線画");
        core::Bitmap art(40, 40);
        art.fill({0, 0, 0, 0});
        art.setPixel(30, 20, {255, 0, 0, 255});  // 中心からずれた目印
        layer.addFrame().bitmap() = art;
        cut.setFrameCount(1);
        cel.setExposure(0, 0);

        core::MultiplaneSetup& mp = cut.multiplane();
        mp.enabled = true;
        mp.camera.focalLengthMm = 50.0;
        mp.camera.sensorWidthMm = 36.0;
        mp.camera.apertureFStop = 0.0;  // ピンホール(決定論的)
        mp.camera.focusDistanceMm = 500.0;
        mp.samplesPerPixel = 1;
        mp.planes.push_back({0, distanceMm, 400.0});

        return core::renderCutFrame(cut, 0, 100, 100);
    };

    const core::Bitmap outNear = renderAtDistance(500.0);
    const core::Bitmap outFar = renderAtDistance(1000.0);

    const double cxNear = weightedCentroidX(outNear);
    const double cxFar = weightedCentroidX(outFar);

    // 中心(50px)からの距離: 遠い段のほうが小さい(中央寄り=縮小して写る)
    REQUIRE(std::abs(cxFar - 50.0) < std::abs(cxNear - 50.0));
}

// --- 透過光(T光、バックライトの二重露光) ---

TEST_CASE("renderMultiplane backlight shines through a hole in an opaque black plane", "[core][multiplane][backlight]") {
    // 黒く塗り潰した平面の中央に「穴」(透明部分)を開ける。バックライトONで穴だけ明るく光り、
    // 塗った部分は遮光されて黒いまま=実物の透過光マスクと同じ挙動
    core::Bitmap art(40, 40);
    art.fill({0, 0, 0, 255});  // 不透明な黒(完全遮光)
    for (int y = 18; y <= 21; ++y)
        for (int x = 18; x <= 21; ++x) art.setPixel(x, y, {0, 0, 0, 0});  // 穴

    core::MultiplanePlane plane;
    plane.artwork = &art;
    plane.distanceMm = 500.0;
    plane.widthMm = 400.0;

    core::MultiplaneCamera camera;  // 既定: focal50/sensor36/ピンホール/focus500

    core::MultiplaneBacklight backlight;
    backlight.enabled = true;
    backlight.intensity = 4.0;
    backlight.colorR = backlight.colorG = backlight.colorB = 1.0;
    backlight.bloomStrength = 0.0;  // まずハレーション無しで透過そのものを検証

    const core::Bitmap out = core::renderMultiplane({plane}, camera, 100, 100, 1, 1, &backlight);

    // 穴の投影位置=画像中央: 紙白1.0+透過4.0でクランプ→真っ白
    REQUIRE(out.pixel(50, 50).r == 255);
    // 黒塗り部分(中央から離れた場所): 反射=黒、透過=0 → 暗いまま
    REQUIRE(out.pixel(10, 10).r < 30);
}

TEST_CASE("renderMultiplane backlight bloom spreads light beyond the hole (halation)", "[core][multiplane][backlight]") {
    core::Bitmap art(40, 40);
    art.fill({0, 0, 0, 255});
    for (int y = 18; y <= 21; ++y)
        for (int x = 18; x <= 21; ++x) art.setPixel(x, y, {0, 0, 0, 0});

    core::MultiplanePlane plane;
    plane.artwork = &art;
    plane.distanceMm = 500.0;
    plane.widthMm = 400.0;
    core::MultiplaneCamera camera;

    core::MultiplaneBacklight noBloom;
    noBloom.enabled = true;
    noBloom.bloomStrength = 0.0;
    core::MultiplaneBacklight withBloom = noBloom;
    withBloom.bloomStrength = 1.0;
    withBloom.bloomRadiusPx = 8.0;

    const core::Bitmap outNo = core::renderMultiplane({plane}, camera, 100, 100, 1, 1, &noBloom);
    const core::Bitmap outBloom = core::renderMultiplane({plane}, camera, 100, 100, 1, 1, &withBloom);

    // 穴から少し離れた黒塗り部分: ブルーム無しでは暗く、ブルーム有りではにじみで明るくなる
    const int sampleX = 50 + 10;  // 穴(中央、投影幅約10px)の外側
    REQUIRE(outBloom.pixel(sampleX, 50).r > outNo.pixel(sampleX, 50).r);
}

TEST_CASE("renderMultiplane backlight through a colored gel transmits its color", "[core][multiplane][backlight]") {
    // 赤いゲル(不透明な赤、透過率τ=1.0)を全面に敷く: 透過光は赤くなる(G,Bは遮断)
    core::Bitmap art(40, 40);
    art.fill({255, 0, 0, 255});

    core::MultiplanePlane plane;
    plane.artwork = &art;
    plane.distanceMm = 500.0;
    plane.widthMm = 2000.0;  // 画面全体を確実に覆う
    core::MultiplaneCamera camera;

    core::MultiplaneBacklight backlight;
    backlight.enabled = true;
    backlight.intensity = 2.0;
    backlight.colorR = backlight.colorG = backlight.colorB = 1.0;
    backlight.paintTransmittance = 1.0;  // 完全なカラーゲル
    backlight.bloomStrength = 0.0;

    const core::Bitmap out = core::renderMultiplane({plane}, camera, 60, 60, 1, 1, &backlight);
    const core::Bitmap::Pixel center = out.pixel(30, 30);
    // 反射光: 赤ゲル(255,0,0)そのまま。透過光: 赤チャンネルのみ2.0通過 → 赤が緑/青より明確に強い
    REQUIRE(center.r > 200);
    REQUIRE(center.g < 60);
    REQUIRE(center.b < 60);
}

TEST_CASE("renderMultiplane backlight disabled matches no-backlight output byte for byte", "[core][multiplane][backlight]") {
    core::Bitmap art(40, 40);
    art.fill({0, 0, 0, 0});
    art.setPixel(20, 20, {200, 30, 30, 255});
    core::MultiplanePlane plane;
    plane.artwork = &art;
    plane.distanceMm = 500.0;
    plane.widthMm = 400.0;
    core::MultiplaneCamera camera;
    camera.apertureFStop = 2.0;  // DoF+モンテカルロ経路も含めて比較する

    core::MultiplaneBacklight disabled;  // enabled=false
    const core::Bitmap a = core::renderMultiplane({plane}, camera, 80, 45, 8, 7, nullptr);
    const core::Bitmap b = core::renderMultiplane({plane}, camera, 80, 45, 8, 7, &disabled);
    REQUIRE(a.byteSize() == b.byteSize());
    REQUIRE(std::memcmp(a.data(), b.data(), a.byteSize()) == 0);
}

TEST_CASE("renderMultiplane is deterministic across runs (parallel rows)", "[core][multiplane][parallel]") {
    // 行並列化後も同じ入力・シードでバイト単位に同一の結果になること
    core::Bitmap art(40, 40);
    art.fill({0, 0, 0, 0});
    for (int i = 5; i < 35; ++i) art.setPixel(i, i, {180, 40, 220, 255});
    core::MultiplanePlane plane;
    plane.artwork = &art;
    plane.distanceMm = 400.0;
    plane.widthMm = 400.0;
    core::MultiplaneCamera camera;
    camera.apertureFStop = 1.4;
    camera.focusDistanceMm = 300.0;

    core::MultiplaneBacklight backlight;
    backlight.enabled = true;

    const core::Bitmap a = core::renderMultiplane({plane}, camera, 160, 90, 8, 3, &backlight);
    const core::Bitmap b = core::renderMultiplane({plane}, camera, 160, 90, 8, 3, &backlight);
    REQUIRE(std::memcmp(a.data(), b.data(), a.byteSize()) == 0);
}

// --- T光マスク(ペンで絞る/セル・レイヤーを光源形状として使う) ---

TEST_CASE("renderMultiplane backlight mask restricts light to the pen-drawn shape",
          "[core][multiplane][backlight][mask]") {
    // 黒塗り(不透明・完全遮光)の中に、左寄りと右寄りの2つの穴を開ける
    core::Bitmap art(40, 40);
    art.fill({0, 0, 0, 255});
    for (int y = 18; y <= 21; ++y) {
        for (int x = 8; x <= 11; ++x) art.setPixel(x, y, {0, 0, 0, 0});    // 左穴
        for (int x = 28; x <= 31; ++x) art.setPixel(x, y, {0, 0, 0, 0});   // 右穴
    }

    core::MultiplanePlane plane;
    plane.artwork = &art;
    plane.distanceMm = 500.0;
    plane.widthMm = 400.0;
    core::MultiplaneCamera camera;  // 既定: focal50/sensor36/ピンホール/focus500

    // マスク無し(比較用の対照): 両穴ともブルームで周辺の黒塗りを明るくする
    core::MultiplaneBacklight noMask;
    noMask.enabled = true;
    noMask.intensity = 4.0;
    noMask.colorR = noMask.colorG = noMask.colorB = 1.0;
    noMask.bloomStrength = 1.0;
    noMask.bloomRadiusPx = 10.0;

    // マスク有り: 出力(100x100)の左半分だけalpha255、右半分は0(=右穴は光源として絞られ消える)
    core::Bitmap mask(100, 100);
    for (int y = 0; y < 100; ++y) {
        for (int x = 0; x < 100; ++x) {
            mask.setPixel(x, y, {255, 255, 255, static_cast<uint8_t>(x < 50 ? 255 : 0)});
        }
    }
    core::MultiplaneBacklight withMask = noMask;
    withMask.mask = mask;

    const core::Bitmap outNoMask = core::renderMultiplane({plane}, camera, 100, 100, 1, 1, &noMask);
    const core::Bitmap outWithMask = core::renderMultiplane({plane}, camera, 100, 100, 1, 1, &withMask);

    // 穴そのものの投影位置は反射(紙白フォールバック)で常に白飽和するため、
    // 穴のすぐ外側(黒塗りのまま)のブルームによるにじみで判定する(halationテストと同じ手法)。
    // 左穴(投影px≈22)のすぐ外側・右穴(投影px≈78)のすぐ外側を見る
    const int leftAdjacentX = 30;   // 左穴のすぐ外側(ブルームが届く)
    const int rightAdjacentX = 86;  // 右穴のすぐ外側(ブルームが届く)

    // マスク無し: 両穴ともブルームで黒塗りのすぐ外側まで明るくなる(対照実験)
    REQUIRE(outNoMask.pixel(rightAdjacentX, 50).r > 5);

    // マスク有り: 右穴はマスクで絞られて透過光ゼロになる(ブルーム元が無い)ため、すぐ外側も暗いまま
    REQUIRE(outWithMask.pixel(rightAdjacentX, 50).r < outNoMask.pixel(rightAdjacentX, 50).r);
    REQUIRE(outWithMask.pixel(rightAdjacentX, 50).r < 3);

    // 左穴はマスクの内側(alpha255)なので、マスク無しと同じ明るさのまま(乗算1.0=無変化)
    REQUIRE(outWithMask.pixel(leftAdjacentX, 50).r == outNoMask.pixel(leftAdjacentX, 50).r);
    REQUIRE(outWithMask.pixel(leftAdjacentX, 50).r > 5);
}

// --- Cut/Compositor統合: キーフレーム・セル/レイヤーマスク ---

TEST_CASE("MultiplaneSetup::valueAt interpolates keyframes with clamping", "[core][multiplane][keys]") {
    const std::map<size_t, double> empty;
    REQUIRE(core::MultiplaneSetup::valueAt(empty, 5, 42.0) == 42.0);  // キー無し→基本値をそのまま

    const std::map<size_t, double> oneKey{{3, 7.0}};
    REQUIRE(core::MultiplaneSetup::valueAt(oneKey, 0, 42.0) == 7.0);   // 1キーのみ→定数
    REQUIRE(core::MultiplaneSetup::valueAt(oneKey, 3, 42.0) == 7.0);
    REQUIRE(core::MultiplaneSetup::valueAt(oneKey, 10, 42.0) == 7.0);

    const std::map<size_t, double> twoKeys{{2, 10.0}, {6, 20.0}};
    REQUIRE(core::MultiplaneSetup::valueAt(twoKeys, 0, 0.0) == 10.0);   // 最初のキーより前→クランプ
    REQUIRE(core::MultiplaneSetup::valueAt(twoKeys, 2, 0.0) == 10.0);   // キー上
    REQUIRE(core::MultiplaneSetup::valueAt(twoKeys, 4, 0.0) == 15.0);   // 中間=線形補間
    REQUIRE(core::MultiplaneSetup::valueAt(twoKeys, 6, 0.0) == 20.0);   // キー上
    REQUIRE(core::MultiplaneSetup::valueAt(twoKeys, 100, 0.0) == 20.0);  // 最後のキーより後→クランプ
}

TEST_CASE("renderCutFrame classic multiplane blinks via intensityKeys", "[core][compositor][multiplane][keys]") {
    // 点滅(押井守作品風)の再現確認: 透過光の強度キーframe0=0/frame2=8で、
    // frame0はほぼ消灯・frame2は明るく光る
    core::Cut cut("Cut");
    core::Cel& cel = cut.addCel("A");
    core::Layer& layer = cel.addLayer("線画");
    core::Bitmap art(100, 100);
    art.fill({60, 60, 60, 255});  // 不透明な暗いグレー(反射のベースライン、透過は色c=60/255で少量通す)
    layer.addFrame().bitmap() = art;
    cut.setFrameCount(3);
    // 動画は1枚(drawing 0)のみ。全コマで同じ絵を表示する(露出0固定、コマごとの絵変化は無関係にする)
    cel.setExposure(0, 0);
    cel.setExposure(1, 0);
    cel.setExposure(2, 0);

    core::MultiplaneSetup& mp = cut.multiplane();
    mp.enabled = true;
    mp.camera.apertureFStop = 0.0;  // ピンホール(決定論的)
    mp.samplesPerPixel = 1;
    mp.planes.push_back({0, 500.0, 400.0});  // 400mm幅はFOV(360mm)より広く、全面を覆う
    mp.backlight.enabled = true;
    mp.backlight.colorR = mp.backlight.colorG = mp.backlight.colorB = 1.0;
    mp.backlight.paintTransmittance = 0.3;
    mp.backlight.bloomStrength = 0.0;
    mp.intensityKeys = {{0, 0.0}, {2, 8.0}};

    const core::Bitmap out0 = core::renderCutFrame(cut, 0, 100, 100);
    const core::Bitmap out2 = core::renderCutFrame(cut, 2, 100, 100);

    // frame0: 透過光0→反射(暗いグレー)のみ。frame2: 透過光8→大きく明るくなる
    const int r0 = out0.pixel(50, 50).r;
    const int r2 = out2.pixel(50, 50).r;
    INFO("r0=" << r0 << " r2=" << r2);
    REQUIRE(r2 > r0 + 50);
    REQUIRE(r0 < 90);   // ほぼベースラインのまま
    REQUIRE(r2 < 255);  // 飽和はしていない(差が見える範囲)
}

TEST_CASE("renderCutFrame classic multiplane backlight cel/layer mask lights only that shape",
          "[core][compositor][multiplane][mask]") {
    // セルA=不透明な暗いグレー全面(遮光気味・わずかに透過)をマルチプレーンの唯一の段として撮影し、
    // セルB=中央に円(planesには割付けない、光源マスクとしてのみ使う)を指定する。
    // 円の内側だけ透過光が強く出て、外側はベースライン(セルAの反射+わずかな一様透過)のままになる
    core::Cut cut("Cut");
    core::Cel& celA = cut.addCel("A");
    core::Layer& layerA = celA.addLayer("線画");
    core::Bitmap artA(100, 100);
    artA.fill({60, 60, 60, 255});
    layerA.addFrame().bitmap() = artA;

    core::Cel& celB = cut.addCel("B");
    core::Layer& layerB = celB.addLayer("線画");
    core::Bitmap artB(100, 100);
    artB.fill({0, 0, 0, 0});  // 全面透明
    for (int y = 0; y < 100; ++y) {
        for (int x = 0; x < 100; ++x) {
            const double dx = x - 50.0;
            const double dy = y - 50.0;
            if (dx * dx + dy * dy <= 15.0 * 15.0) artB.setPixel(x, y, {255, 255, 255, 255});  // 中央の円
        }
    }
    layerB.addFrame().bitmap() = artB;

    cut.setFrameCount(1);
    celA.setExposure(0, 0);
    celB.setExposure(0, 0);

    core::MultiplaneSetup& mp = cut.multiplane();
    mp.enabled = true;
    mp.camera.apertureFStop = 0.0;
    mp.samplesPerPixel = 1;
    mp.planes.push_back({0, 500.0, 400.0});  // セルAのみ撮影台に割り付ける(セルBは光源マスク専用)
    mp.backlight.enabled = true;
    mp.backlight.intensity = 6.0;
    mp.backlight.colorR = mp.backlight.colorG = mp.backlight.colorB = 1.0;
    mp.backlight.paintTransmittance = 0.3;
    mp.backlight.bloomStrength = 0.0;
    mp.backlight.maskCelIndex = 1;    // セルB
    mp.backlight.maskLayerIndex = -1;  // セル全体(可視レイヤー合成)

    const core::Bitmap out = core::renderCutFrame(cut, 0, 100, 100);

    // 円の内側(中央): ベースライン反射+透過光の乗算マスクで明るく光る
    REQUIRE(out.pixel(50, 50).r > 100);
    // 円の外側: セルAのベースライン反射のみ(マスクで透過光が絞られてほぼ0)
    REQUIRE(out.pixel(10, 10).r < 80);
    REQUIRE(out.pixel(90, 90).r < 80);

    // 対照実験: マスク指定を外す(maskCelIndex=-1)と全面が一様に明るくなり、円の外側も明るくなる
    mp.backlight.maskCelIndex = -1;
    const core::Bitmap outNoMask = core::renderCutFrame(cut, 0, 100, 100);
    REQUIRE(outNoMask.pixel(10, 10).r > out.pixel(10, 10).r);
}
