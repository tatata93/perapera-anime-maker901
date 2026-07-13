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
    const std::vector<core::MultiplaneBacklight> backlights{backlight};

    const core::Bitmap out = core::renderMultiplane({plane}, camera, 100, 100, 1, 1, &backlights);

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
    const std::vector<core::MultiplaneBacklight> noBloomLights{noBloom};
    const std::vector<core::MultiplaneBacklight> withBloomLights{withBloom};

    const core::Bitmap outNo = core::renderMultiplane({plane}, camera, 100, 100, 1, 1, &noBloomLights);
    const core::Bitmap outBloom = core::renderMultiplane({plane}, camera, 100, 100, 1, 1, &withBloomLights);

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
    const std::vector<core::MultiplaneBacklight> backlights{backlight};

    const core::Bitmap out = core::renderMultiplane({plane}, camera, 60, 60, 1, 1, &backlights);
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
    const std::vector<core::MultiplaneBacklight> disabledLights{disabled};
    const core::Bitmap a = core::renderMultiplane({plane}, camera, 80, 45, 8, 7, nullptr);
    const core::Bitmap b = core::renderMultiplane({plane}, camera, 80, 45, 8, 7, &disabledLights);
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
    const std::vector<core::MultiplaneBacklight> backlights{backlight};

    const core::Bitmap a = core::renderMultiplane({plane}, camera, 160, 90, 8, 3, &backlights);
    const core::Bitmap b = core::renderMultiplane({plane}, camera, 160, 90, 8, 3, &backlights);
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
    const std::vector<core::MultiplaneBacklight> noMaskLights{noMask};
    const std::vector<core::MultiplaneBacklight> withMaskLights{withMask};

    const core::Bitmap outNoMask = core::renderMultiplane({plane}, camera, 100, 100, 1, 1, &noMaskLights);
    const core::Bitmap outWithMask = core::renderMultiplane({plane}, camera, 100, 100, 1, 1, &withMaskLights);

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

TEST_CASE("renderCutFrame classic multiplane blinks per-light via its own intensityKeys",
          "[core][compositor][multiplane][keys]") {
    // 灯ごとの点滅(蛍光灯と液晶が別タイミングで明滅する等)の確認: 灯1はintensityKeys
    // (コマ0=0/コマ1=4)で点滅、灯2は点滅せず常時点灯(強度2)。コマ0は灯2のみ・コマ1は両方光る
    core::Cut cut("Cut");
    core::Cel& cel = cut.addCel("A");
    core::Layer& layer = cel.addLayer("線画");
    core::Bitmap art(100, 100);
    art.fill({60, 60, 60, 255});  // 不透明な暗いグレー(反射のベースライン、透過は色c=60/255で少量通す)
    layer.addFrame().bitmap() = art;
    cut.setFrameCount(2);
    // 動画は1枚(drawing 0)のみ。全コマで同じ絵を表示する(露出0固定、コマごとの絵変化は無関係にする)
    cel.setExposure(0, 0);
    cel.setExposure(1, 0);

    core::MultiplaneSetup& mp = cut.multiplane();
    mp.enabled = true;
    mp.camera.apertureFStop = 0.0;  // ピンホール(決定論的)
    mp.samplesPerPixel = 1;
    mp.planes.push_back({0, 500.0, 400.0});  // 400mm幅はFOV(360mm)より広く、全面を覆う

    core::MultiplaneBacklight blinking;
    blinking.name = "灯1(点滅)";
    blinking.enabled = true;
    blinking.colorR = blinking.colorG = blinking.colorB = 1.0;
    blinking.paintTransmittance = 0.3;  // 先頭の有効灯なので、共有される∏Tのτはこれが使われる
    blinking.bloomStrength = 0.0;
    blinking.intensityKeys = {{0, 0.0}, {1, 4.0}};
    mp.backlights.push_back(blinking);

    core::MultiplaneBacklight steady;
    steady.name = "灯2(常時点灯)";
    steady.enabled = true;
    steady.intensity = 2.0;  // 点滅しない(キー無し)
    steady.colorR = steady.colorG = steady.colorB = 1.0;
    steady.bloomStrength = 0.0;
    mp.backlights.push_back(steady);

    const core::Bitmap out0 = core::renderCutFrame(cut, 0, 100, 100);
    const core::Bitmap out1 = core::renderCutFrame(cut, 1, 100, 100);

    // コマ0: 灯1消灯・灯2のみ点灯→ベースラインよりやや明るい。コマ1: 両灯点灯→さらに明るい
    const int r0 = out0.pixel(50, 50).r;
    const int r1 = out1.pixel(50, 50).r;
    INFO("r0=" << r0 << " r1=" << r1);
    REQUIRE(r0 > 60);   // 灯2の寄与でベースライン(60)より明るい
    REQUIRE(r1 > r0 + 30);
    REQUIRE(r1 < 255);  // 飽和していない(差が見える範囲)
}

TEST_CASE("renderCutFrame classic multiplane backlight cel/layer mask lights only that shape",
          "[core][compositor][multiplane][mask]") {
    // セルA=不透明な暗いグレー全面(遮光気味・わずかに透過)をマルチプレーンの唯一の段として撮影し、
    // セルB=中央に円(planesには割付けない、光源マスクとしてのみ使う)を指定する。
    // マスクセルが段に割り付いていないケース(フォールバック=キャンバス1:1)の確認:
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

    core::MultiplaneBacklight bl;
    bl.enabled = true;
    bl.intensity = 6.0;
    bl.colorR = bl.colorG = bl.colorB = 1.0;
    bl.paintTransmittance = 0.3;
    bl.bloomStrength = 0.0;
    bl.maskCelIndex = 1;    // セルB(段には割り付いていない→フォールバック)
    bl.maskLayerIndex = -1;  // セル全体(可視レイヤー合成)
    mp.backlights.push_back(bl);

    const core::Bitmap out = core::renderCutFrame(cut, 0, 100, 100);

    // 円の内側(中央): ベースライン反射+透過光の乗算マスクで明るく光る
    REQUIRE(out.pixel(50, 50).r > 100);
    // 円の外側: セルAのベースライン反射のみ(マスクで透過光が絞られてほぼ0)
    REQUIRE(out.pixel(10, 10).r < 80);
    REQUIRE(out.pixel(90, 90).r < 80);

    // 対照実験: マスク指定を外す(maskCelIndex=-1)と全面が一様に明るくなり、円の外側も明るくなる
    mp.backlights[0].maskCelIndex = -1;
    const core::Bitmap outNoMask = core::renderCutFrame(cut, 0, 100, 100);
    REQUIRE(outNoMask.pixel(10, 10).r > out.pixel(10, 10).r);
}

// --- ユーザー要望(a): 透過光を複数灯設定できる(灯ごとに色/強度/マスク/点滅) ---

TEST_CASE("renderMultiplane multiple backlights each keep their own color/intensity/mask",
          "[core][multiplane][backlight][multi]") {
    // 灯ごとに色/強度/マスクを持てることの確認: 一様な暗いグレー(反射のベースライン)の平面越しに
    // 赤い灯(左半分のみ)と青い灯(右半分のみ)を同時に灯す。∏T(共有の透過率)は灰色(無彩色)なので
    // どの灯でも同じ値になり、色の違いは各灯のcolor/maskだけに由来する
    core::Bitmap art(40, 40);
    art.fill({60, 60, 60, 255});

    core::MultiplanePlane plane;
    plane.artwork = &art;
    plane.distanceMm = 500.0;
    plane.widthMm = 400.0;  // FOV(360mm)より広く、全面を覆う
    core::MultiplaneCamera camera;  // 既定: focal50/sensor36/ピンホール/focus500

    core::Bitmap leftMask(100, 100);
    core::Bitmap rightMask(100, 100);
    for (int y = 0; y < 100; ++y) {
        for (int x = 0; x < 100; ++x) {
            leftMask.setPixel(x, y, {255, 255, 255, static_cast<uint8_t>(x < 50 ? 255 : 0)});
            rightMask.setPixel(x, y, {255, 255, 255, static_cast<uint8_t>(x >= 50 ? 255 : 0)});
        }
    }

    core::MultiplaneBacklight red;
    red.name = "赤(左半分)";
    red.enabled = true;
    red.intensity = 8.0;
    red.colorR = 1.0;
    red.colorG = 0.0;
    red.colorB = 0.0;
    red.paintTransmittance = 0.3;
    red.bloomStrength = 0.0;
    red.mask = leftMask;

    core::MultiplaneBacklight blue;
    blue.name = "青(右半分)";
    blue.enabled = true;
    blue.intensity = 8.0;
    blue.colorR = 0.0;
    blue.colorG = 0.0;
    blue.colorB = 1.0;
    blue.paintTransmittance = 0.3;  // 光路共有のため実際に使われるのは先頭(赤)のτだが揃えておく
    blue.bloomStrength = 0.0;
    blue.mask = rightMask;

    const std::vector<core::MultiplaneBacklight> lights{red, blue};
    const core::Bitmap out = core::renderMultiplane({plane}, camera, 100, 100, 1, 1, &lights);

    const core::Bitmap::Pixel left = out.pixel(25, 50);
    const core::Bitmap::Pixel right = out.pixel(75, 50);
    INFO("left=(" << int(left.r) << "," << int(left.g) << "," << int(left.b) << ") right=(" << int(right.r) << ","
                   << int(right.g) << "," << int(right.b) << ")");

    // 左半分: 赤い灯だけが効く(青は右半分マスクで絞られてゼロ)
    REQUIRE(left.r > left.g + 40);
    REQUIRE(left.r > left.b + 40);
    // 右半分: 青い灯だけが効く(赤は左半分マスクで絞られてゼロ)
    REQUIRE(right.b > right.r + 40);
    REQUIRE(right.b > right.g + 40);
}

// --- バグ修正: マスクセルが撮影台の段に割り付いている場合の投影座標系 ---

TEST_CASE("renderCutFrame classic multiplane cel mask projects using the assigned stage's physical layout",
          "[core][compositor][multiplane][mask][bugfix]") {
    // マスクセルのバグ修正の検証: マスクセルが撮影台の段(距離500mm/幅400mm)に割り付いている場合、
    // マスクは「出力px→キャンバスpx 1:1」ではなく、その段の物理配置でピンホール投影してサンプル
    // されなければならない。マスクセルのビットマップ解像度(200x200)を出力解像度(100x100)と
    // 意図的に変えることで、1:1参照(旧コードのバグ)なら出力範囲(0-99,0-99)からは絶対に
    // 届かない位置(147-152,97-102)にマークを置き、正しい投影位置(ピンホール投影式で計算)
    // だけが光ることを確認する
    core::Cut cut("Cut");

    // 物理的な光の透過媒体(段0): 一様な暗いグレー(反射のベースライン、わずかに透過もする)
    core::Cel& stage = cut.addCel("Stage");
    core::Layer& stageLayer = stage.addLayer("線画");
    core::Bitmap stageArt(100, 100);
    stageArt.fill({60, 60, 60, 255});
    stageLayer.addFrame().bitmap() = stageArt;

    // マスク光源の形(段1、同じ距離500mm/幅400mmに割り付ける): 200x200(出力の100x100と
    // わざと解像度を変える)、中央からずれた位置(147-152,97-102)に不透明マークを置く。
    // それ以外は全面透明
    core::Cel& markCel = cut.addCel("MarkMask");
    core::Layer& markLayer = markCel.addLayer("線画");
    core::Bitmap markArt(200, 200);
    markArt.fill({0, 0, 0, 0});
    for (int y = 97; y <= 102; ++y)
        for (int x = 147; x <= 152; ++x) markArt.setPixel(x, y, {255, 255, 255, 255});
    markLayer.addFrame().bitmap() = markArt;

    cut.setFrameCount(1);
    stage.setExposure(0, 0);
    markCel.setExposure(0, 0);

    core::MultiplaneSetup& mp = cut.multiplane();
    mp.enabled = true;
    mp.camera.apertureFStop = 0.0;  // ピンホール(決定論的)
    mp.samplesPerPixel = 1;
    mp.planes.push_back({0, 500.0, 400.0});  // Stage
    mp.planes.push_back({1, 500.0, 400.0});  // MarkMask(マスクセルも同じ段に割り付ける)

    core::MultiplaneBacklight bl;
    bl.enabled = true;
    bl.intensity = 8.0;
    bl.colorR = bl.colorG = bl.colorB = 1.0;
    bl.paintTransmittance = 0.3;
    bl.bloomStrength = 0.0;
    bl.maskCelIndex = 1;  // MarkMask
    bl.maskLayerIndex = -1;
    mp.backlights.push_back(bl);

    const core::Bitmap out = core::renderCutFrame(cut, 0, 100, 100);

    // 期待位置(ピンホール投影式): マークの中心(150,100)のテクセル座標からワールドmmへ変換し、
    // renderMultiplaneのsamplePlane/PlaneContextと同じ式で出力pxへ投影する
    const double u = 150.5, v = 100.5;  // マーク(6x6ブロック)の中心+テクセル中心オフセット
    const double wx = (u / 200.0 - 0.5) * 400.0;
    const double wy = (v / 200.0 - 0.5) * 400.0;
    const double focal = mp.camera.focalLengthMm;
    const double sensorW = mp.camera.sensorWidthMm;  // framingLock適用後もfocal50/sensor36は変わらない
    const int expectedX = static_cast<int>(std::lround(100.0 * (0.5 + wx * focal / (500.0 * sensorW))));
    const int expectedY = static_cast<int>(std::lround(100.0 * (0.5 + wy * focal / (500.0 * sensorW))));
    INFO("expectedX=" << expectedX << " expectedY=" << expectedY);
    REQUIRE(expectedX >= 0);
    REQUIRE(expectedX < 100);
    REQUIRE(expectedY >= 0);
    REQUIRE(expectedY < 100);

    // 期待位置はベースライン(60)より明るい(マスクの投影が正しく効いている)
    REQUIRE(out.pixel(expectedX, expectedY).r > 90);

    // マークから離れた位置は透過光がベースラインのまま暗い(マスクが正しく絞られている=
    // 旧バグのように全面に漏れていない)
    REQUIRE(out.pixel(10, 10).r < 80);
    REQUIRE(out.pixel(90, 90).r < 80);
}

// --- ユーザー要望(c): フレーミング固定(焦点距離を変えても基準距離の構図を維持する) ---

TEST_CASE("renderCutFrame classic multiplane framing lock keeps composition when focal length changes",
          "[core][compositor][multiplane][framing]") {
    // フレーミング固定(framingLock=true、既定)の確認: 基準距離(500mm、既定のframingRefDistanceMm)の
    // 平面上のバーの見かけの幅・位置は、焦点距離を50mm→100mmへ変えても変わらない
    // (望遠にしても大ボケでも奥のセルが小さくならず構図が変わらない)。
    // framingLock=falseなら通常のカメラどおり焦点距離に比例して拡大される
    const auto renderAt = [](double focalMm, bool framingLock) {
        core::Cut cut("Cut");
        core::Cel& cel = cut.addCel("A");
        core::Layer& layer = cel.addLayer("線画");
        core::Bitmap art(100, 100);
        art.fill({0, 0, 0, 0});
        for (int y = 0; y < 100; ++y) {
            for (int x = 40; x < 60; ++x) art.setPixel(x, y, {255, 0, 0, 255});  // 中央の縦バー
        }
        layer.addFrame().bitmap() = art;
        cut.setFrameCount(1);
        cel.setExposure(0, 0);

        core::MultiplaneSetup& mp = cut.multiplane();
        mp.enabled = true;
        mp.camera.apertureFStop = 0.0;  // ピンホール(決定論的)
        mp.camera.focalLengthMm = focalMm;
        mp.samplesPerPixel = 1;
        mp.framingLock = framingLock;
        mp.planes.push_back({0, 500.0, 400.0});  // 基準距離(500mm)ちょうどに置く

        return core::renderCutFrame(cut, 0, 100, 100);
    };

    // (幅[px above half-max], 重心x)を測る
    const auto measureBar = [](const core::Bitmap& img) {
        const int row = 50;
        double maxW = 0.0;
        for (int x = 0; x < img.width(); ++x) maxW = std::max(maxW, 255.0 - img.pixel(x, row).g);
        REQUIRE(maxW > 0.0);
        int count = 0;
        double sumW = 0.0, sumWX = 0.0;
        for (int x = 0; x < img.width(); ++x) {
            const double w = 255.0 - img.pixel(x, row).g;
            if (w > maxW * 0.5) ++count;
            sumW += w;
            sumWX += w * x;
        }
        REQUIRE(sumW > 0.0);
        return std::make_pair(count, sumWX / sumW);
    };

    // フレーミング固定あり: 焦点距離50→100でも幅・中心位置がほぼ同じ(構図維持)
    const auto lock50 = measureBar(renderAt(50.0, true));
    const auto lock100 = measureBar(renderAt(100.0, true));
    INFO("lock50 width=" << lock50.first << " center=" << lock50.second << " lock100 width=" << lock100.first
                          << " center=" << lock100.second);
    REQUIRE(std::abs(lock50.second - lock100.second) <= 2.0);
    REQUIRE(std::abs(lock50.first - lock100.first) <= 2);

    // フレーミング固定なし: 焦点距離100は50よりはっきり大きく写る(通常のカメラの挙動)
    const auto free50 = measureBar(renderAt(50.0, false));
    const auto free100 = measureBar(renderAt(100.0, false));
    INFO("free50 width=" << free50.first << " free100 width=" << free100.first);
    REQUIRE(static_cast<double>(free100.first) > static_cast<double>(free50.first) * 1.5);
}

// --- ユーザー要望(b): 絞りF値もコマキーを打てる ---

TEST_CASE("renderCutFrame classic multiplane fstopKeys switch pan-focus to shallow depth of field",
          "[core][compositor][multiplane][keys]") {
    // 絞りのコマキー確認: コマ0はfstop=0(ピンホール=パンフォーカス)でシャープ、
    // コマ4はfstop=2.0(開放)でフォーカス外の段がボケる
    core::Cut cut("Cut");
    core::Cel& cel = cut.addCel("A");
    core::Layer& layer = cel.addLayer("線画");
    core::Bitmap art(40, 40);
    art.fill({0, 0, 0, 0});
    art.setPixel(20, 20, {255, 0, 0, 255});  // 中央の目印
    layer.addFrame().bitmap() = art;
    cut.setFrameCount(5);
    for (size_t t = 0; t < 5; ++t) cel.setExposure(t, 0);

    core::MultiplaneSetup& mp = cut.multiplane();
    mp.enabled = true;
    mp.camera.focusDistanceMm = 500.0;  // ピントは500mmに固定
    mp.samplesPerPixel = 64;  // ボケの検出にはサンプル数が必要
    // ピント面(500mm)より手前の段=開放時はボケの対象。widthMmは100mm(狭め)にして、
    // 近距離(250mm)による拡大率が大きくなりすぎて目印自体の投影サイズが±2px判定窓を
    // 超えないようにする(ピンホールでもシャープ判定できるようにするための調整、ボケ量自体には無関係)
    mp.planes.push_back({0, 250.0, 100.0});
    mp.fstopKeys = {{0, 0.0}, {4, 2.0}};

    const core::Bitmap out0 = core::renderCutFrame(cut, 0, 100, 100);
    const core::Bitmap out4 = core::renderCutFrame(cut, 4, 100, 100);

    const auto concentrationRatio = [](const core::Bitmap& img) {
        int bestX = 0, bestY = 0;
        double best = -1.0;
        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                const double w = 255.0 - img.pixel(x, y).g;
                if (w > best) {
                    best = w;
                    bestX = x;
                    bestY = y;
                }
            }
        }
        double total = 0.0, nearPeak = 0.0;
        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                const double w = 255.0 - img.pixel(x, y).g;
                total += w;
                if (std::abs(x - bestX) <= 2 && std::abs(y - bestY) <= 2) nearPeak += w;
            }
        }
        REQUIRE(total > 0.0);
        return nearPeak / total;
    };

    const double ratio0 = concentrationRatio(out0);
    const double ratio4 = concentrationRatio(out4);
    INFO("ratio0=" << ratio0 << " ratio4=" << ratio4);
    REQUIRE(ratio0 > 0.9);  // fstop=0(ピンホール)はシャープ
    REQUIRE(ratio4 < 0.6);  // fstop=2.0(開放)はボケる
}

// --- 書き出しサンプル数(なめらか化) ---

TEST_CASE("renderCutFrame useExportSamples uses exportSamplesPerPixel", "[core][compositor][multiplane][export]") {
    // 被写界深度でボケる状況を作り、書き出し(高サンプル)は作業(低サンプル)よりノイズが少ない
    // (=画素値の局所的なばらつきが小さい)ことを確認する
    core::Cut cut("C");
    core::Cel& cel = cut.addCel("A");
    core::Layer& layer = cel.addLayer("L");
    core::Bitmap art(60, 60);
    art.fill({0, 0, 0, 0});
    // 中央に不透明な白ブロック(ボケると縁にモンテカルロノイズが出る)
    for (int y = 20; y < 40; ++y)
        for (int x = 20; x < 40; ++x) art.setPixel(x, y, {255, 255, 255, 255});
    layer.addFrame().bitmap() = std::move(art);
    cel.setExposure(0, 0);
    cut.setFrameCount(1);

    core::MultiplaneSetup& mp = cut.multiplane();
    mp.enabled = true;
    mp.samplesPerPixel = 2;         // 作業=低サンプル(荒い)
    mp.exportSamplesPerPixel = 64;  // 書き出し=高サンプル(なめらか)
    mp.camera.apertureFStop = 1.4;  // 大きなボケ
    mp.camera.focusDistanceMm = 300.0;
    mp.planes.push_back({0, 700.0, 400.0});  // フォーカス面から外れてボケる

    core::RenderOptions work;  // useExportSamples=false → samplesPerPixel(2)
    core::RenderOptions exp;
    exp.useExportSamples = true;  // → exportSamplesPerPixel(64)

    const core::Bitmap workImg = core::renderCutFrame(cut, 0, 120, 120, work);
    const core::Bitmap expImg = core::renderCutFrame(cut, 0, 120, 120, exp);

    // 隣接ピクセル差の総和(ノイズが多いほど大きい)を、ボケの縁付近の帯で比較する
    const auto localVariation = [](const core::Bitmap& img) {
        long v = 0;
        for (int y = 40; y < 80; ++y) {
            for (int x = 1; x < img.width(); ++x) {
                v += std::abs(img.pixel(x, y).r - img.pixel(x - 1, y).r);
            }
        }
        return v;
    };
    // 高サンプルの方がざらつき(隣接差)が小さい
    REQUIRE(localVariation(expImg) < localVariation(workImg));
}
