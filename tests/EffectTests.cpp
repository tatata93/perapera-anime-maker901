#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <filesystem>

#include "core/Compositor.h"
#include "core/EffectProcessor.h"
#include "core/ProjectIO.h"

namespace {

core::Bitmap makeTransparentDot(int w, int h, int x, int y, core::Bitmap::Pixel color) {
    core::Bitmap bmp(w, h);
    bmp.fill({0, 0, 0, 0});
    bmp.setPixel(x, y, color);
    return bmp;
}

long sumAlpha(const core::Bitmap& bmp) {
    long total = 0;
    for (int y = 0; y < bmp.height(); ++y) {
        for (int x = 0; x < bmp.width(); ++x) total += bmp.pixel(x, y).a;
    }
    return total;
}

// 指定色の1点だけを持つ透明セルを動画として持つレイヤーを作る(CompositorTestsと同じ流儀)
void addDrawingWithDot(core::Layer& layer, int x, int y, core::Bitmap::Pixel color, int w, int h) {
    core::Bitmap bitmap(w, h);
    bitmap.fill({0, 0, 0, 0});
    bitmap.setPixel(x, y, color);
    layer.addFrame().bitmap() = std::move(bitmap);
}

}  // namespace

// --- Blur ---

TEST_CASE("applyBlur spreads a marker pixel and roughly preserves total alpha", "[core][effect][blur]") {
    // 十分広いキャンバス(端のクランプの影響を避ける)に単一の目印ピクセルを置く
    core::Bitmap bmp = makeTransparentDot(41, 41, 20, 20, {200, 50, 50, 255});
    const long alphaBefore = sumAlpha(bmp);
    REQUIRE(alphaBefore == 255);

    core::Effect blur;
    blur.type = core::EffectType::Blur;
    blur.params = core::effectDefaultParams(core::EffectType::Blur);  // radius=4

    core::applyEffect(bmp, blur, 0);

    // 中心は薄まるが完全には無くならず、周囲(半径内)へ広がる
    REQUIRE(bmp.pixel(20, 20).a > 0);
    REQUIRE(bmp.pixel(20, 20).a < 255);
    REQUIRE(bmp.pixel(21, 20).a > 0);  // 隣接ピクセルへ広がっている
    REQUIRE(bmp.pixel(0, 0).a == 0);   // 十分遠い場所には広がらない

    // アルファの総和はおおむね保存される(8bit量子化による多少の目減りは許容)
    const long alphaAfter = sumAlpha(bmp);
    REQUIRE(alphaAfter > 0);
    REQUIRE(alphaAfter <= alphaBefore);
}

TEST_CASE("applyBlur does not darken colors at a transparent edge (alpha weighted)", "[core][effect][blur]") {
    // 不透明な黄色ブロックが透明な背景に接している状況で、縁の色が黒ずまないことを確認する
    core::Bitmap bmp(21, 21);
    bmp.fill({0, 0, 0, 0});
    for (int y = 8; y <= 12; ++y) {
        for (int x = 8; x <= 12; ++x) bmp.setPixel(x, y, {255, 255, 0, 255});
    }

    core::Effect blur;
    blur.type = core::EffectType::Blur;
    blur.params = {{"radius", 2.0}};
    core::applyEffect(bmp, blur, 0);

    const auto edge = bmp.pixel(8, 10);
    REQUIRE(edge.a > 0);
    REQUIRE(edge.a < 255);
    // アルファ加重平均のため、透明部分(色0)に引っ張られて黒ずむことはない
    REQUIRE(edge.r > 200);
    REQUIRE(edge.g > 200);
    REQUIRE(edge.b < 50);
}

// --- Glow ---

TEST_CASE("applyGlow leaves the image unchanged when nothing exceeds the threshold", "[core][effect][glow]") {
    core::Bitmap bmp(16, 16);
    bmp.fill({100, 100, 100, 255});  // 輝度100 < 既定threshold(200)

    core::Bitmap before = bmp;

    core::Effect glow;
    glow.type = core::EffectType::Glow;
    glow.params = core::effectDefaultParams(core::EffectType::Glow);
    core::applyEffect(bmp, glow, 0);

    for (int y = 0; y < bmp.height(); ++y) {
        for (int x = 0; x < bmp.width(); ++x) {
            const auto a = bmp.pixel(x, y);
            const auto b = before.pixel(x, y);
            REQUIRE(a.r == b.r);
            REQUIRE(a.g == b.g);
            REQUIRE(a.b == b.b);
            REQUIRE(a.a == b.a);
        }
    }
}

TEST_CASE("applyGlow brightens pixels around a bright area", "[core][effect][glow]") {
    // 単一ピクセルだと拡散(半径8)で薄まりすぎて8bit量子化に埋もれるため、ある程度の面積を持つ
    // 明部(7x7の白ブロック)で確認する(実際のハイライトも通常は面を持つ)
    core::Bitmap bmp(41, 41);
    bmp.fill({80, 80, 80, 255});  // 暗めの不透明背景(輝度80 < threshold)
    for (int y = 17; y <= 23; ++y) {
        for (int x = 17; x <= 23; ++x) bmp.setPixel(x, y, {255, 255, 255, 255});  // 明るいブロック(輝度255 >= threshold)
    }

    core::Effect glow;
    glow.type = core::EffectType::Glow;
    glow.params = core::effectDefaultParams(core::EffectType::Glow);  // threshold=200,radius=8,strength=0.6
    core::applyEffect(bmp, glow, 0);

    // ブロックのすぐ外側のピクセルは元の暗い色より明るくなっているはず
    const auto neighbor = bmp.pixel(24, 20);
    REQUIRE(neighbor.r > 80);
    REQUIRE(neighbor.g > 80);
    REQUIRE(neighbor.b > 80);
}

// --- Para ---

TEST_CASE("applyPara darkens the top edge and leaves the bottom edge unchanged (defaults)", "[core][effect][para]") {
    core::Bitmap bmp(8, 20);
    bmp.fill({255, 255, 255, 255});  // 白紙想定(不透明)

    core::Effect para;
    para.type = core::EffectType::Para;
    para.params = core::effectDefaultParams(core::EffectType::Para);  // top=0.25, bottom=0.0, r=g=b=0
    core::applyEffect(bmp, para, 0);

    const auto topPixel = bmp.pixel(4, 0);
    // 白(255)からtop濃度0.25で黒(0)へ寄る = 255*0.75 ≈ 191
    REQUIRE(topPixel.r < 255);
    REQUIRE(topPixel.r > 150);

    const auto bottomPixel = bmp.pixel(4, 19);
    REQUIRE(bottomPixel.r == 255);  // 下端(bottom=0.0)は変化なし
    REQUIRE(bottomPixel.g == 255);
    REQUIRE(bottomPixel.b == 255);
}

TEST_CASE("applyPara does not tint fully transparent pixels", "[core][effect][para]") {
    core::Bitmap bmp(4, 4);
    bmp.fill({0, 0, 0, 0});

    core::Effect para;
    para.type = core::EffectType::Para;
    para.params = core::effectDefaultParams(core::EffectType::Para);
    core::applyEffect(bmp, para, 0);

    REQUIRE(bmp.pixel(0, 0).a == 0);
    REQUIRE(bmp.pixel(0, 0).r == 0);
}

// --- Shake ---

TEST_CASE("applyShake is deterministic for the same frame and seed", "[core][effect][shake]") {
    core::Bitmap bmpA(16, 16);
    bmpA.fill({0, 0, 0, 0});
    bmpA.setPixel(8, 8, {10, 20, 30, 255});
    core::Bitmap bmpB = bmpA;

    core::Effect shake;
    shake.type = core::EffectType::Shake;
    shake.params = core::effectDefaultParams(core::EffectType::Shake);

    core::applyEffect(bmpA, shake, 5);
    core::applyEffect(bmpB, shake, 5);

    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            REQUIRE(bmpA.pixel(x, y).r == bmpB.pixel(x, y).r);
            REQUIRE(bmpA.pixel(x, y).g == bmpB.pixel(x, y).g);
            REQUIRE(bmpA.pixel(x, y).b == bmpB.pixel(x, y).b);
            REQUIRE(bmpA.pixel(x, y).a == bmpB.pixel(x, y).a);
        }
    }
}

TEST_CASE("applyShake produces different offsets for different frames", "[core][effect][shake]") {
    core::Effect shake;
    shake.type = core::EffectType::Shake;
    shake.params = core::effectDefaultParams(core::EffectType::Shake);

    bool anyDifferent = false;
    core::Bitmap base(16, 16);
    base.fill({0, 0, 0, 0});
    base.setPixel(8, 8, {10, 20, 30, 255});

    core::Bitmap ref = base;
    core::applyEffect(ref, shake, 0);

    for (size_t frame = 1; frame < 6; ++frame) {
        core::Bitmap variant = base;
        core::applyEffect(variant, shake, frame);
        bool different = false;
        for (int y = 0; y < 16 && !different; ++y) {
            for (int x = 0; x < 16 && !different; ++x) {
                if (variant.pixel(x, y).r != ref.pixel(x, y).r || variant.pixel(x, y).a != ref.pixel(x, y).a) {
                    different = true;
                }
            }
        }
        if (different) anyDifferent = true;
    }
    REQUIRE(anyDifferent);
}

// --- ColorCorrect(色調補正) ---

TEST_CASE("applyColorCorrect brightens with positive brightness", "[core][effect][colorcorrect]") {
    core::Bitmap bmp(4, 4);
    bmp.fill({100, 100, 100, 255});

    core::Effect cc;
    cc.type = core::EffectType::ColorCorrect;
    cc.params = core::effectDefaultParams(core::EffectType::ColorCorrect);
    cc.params["brightness"] = 50.0;
    core::applyEffect(bmp, cc, 0);

    const auto p = bmp.pixel(0, 0);
    REQUIRE(p.r == 150);
    REQUIRE(p.g == 150);
    REQUIRE(p.b == 150);
    REQUIRE(p.a == 255);
}

TEST_CASE("applyColorCorrect with saturation 0 turns a color pixel gray", "[core][effect][colorcorrect]") {
    core::Bitmap bmp(4, 4);
    bmp.fill({200, 50, 50, 255});

    core::Effect cc;
    cc.type = core::EffectType::ColorCorrect;
    cc.params = core::effectDefaultParams(core::EffectType::ColorCorrect);
    cc.params["saturation"] = 0.0;
    core::applyEffect(bmp, cc, 0);

    const auto p = bmp.pixel(0, 0);
    REQUIRE(p.r == p.g);
    REQUIRE(p.g == p.b);
}

TEST_CASE("applyColorCorrect skips fully transparent pixels", "[core][effect][colorcorrect]") {
    core::Bitmap bmp(4, 4);
    bmp.fill({0, 0, 0, 0});

    core::Effect cc;
    cc.type = core::EffectType::ColorCorrect;
    cc.params = core::effectDefaultParams(core::EffectType::ColorCorrect);
    cc.params["brightness"] = 100.0;
    core::applyEffect(bmp, cc, 0);

    REQUIRE(bmp.pixel(0, 0).a == 0);
    REQUIRE(bmp.pixel(0, 0).r == 0);
}

// --- Diffusion(ディフュージョン) ---

TEST_CASE("applyDiffusion spreads a bright area into transparent surroundings", "[core][effect][diffusion]") {
    core::Bitmap bmp(41, 41);
    bmp.fill({0, 0, 0, 0});
    for (int y = 17; y <= 23; ++y) {
        for (int x = 17; x <= 23; ++x) bmp.setPixel(x, y, {255, 255, 255, 255});
    }

    core::Effect diff;
    diff.type = core::EffectType::Diffusion;
    diff.params = core::effectDefaultParams(core::EffectType::Diffusion);  // radius=12, strength=0.5
    core::applyEffect(bmp, diff, 0);

    // 明部のすぐ外側(元は完全透明)がにじみ出て不透明度を持つはず
    const auto neighbor = bmp.pixel(25, 20);
    REQUIRE(neighbor.a > 0);
}

TEST_CASE("applyDiffusion is a no-op when strength is 0", "[core][effect][diffusion]") {
    core::Bitmap bmp(16, 16);
    bmp.fill({120, 60, 200, 255});
    core::Bitmap before = bmp;

    core::Effect diff;
    diff.type = core::EffectType::Diffusion;
    diff.params = {{"radius", 12.0}, {"strength", 0.0}};
    core::applyEffect(bmp, diff, 0);

    for (int y = 0; y < bmp.height(); ++y) {
        for (int x = 0; x < bmp.width(); ++x) {
            REQUIRE(bmp.pixel(x, y).r == before.pixel(x, y).r);
            REQUIRE(bmp.pixel(x, y).a == before.pixel(x, y).a);
        }
    }
}

// --- RadialBlur(放射ブラー) ---

TEST_CASE("applyRadialBlur leaves the exact center pixel unchanged", "[core][effect][radialblur]") {
    core::Bitmap bmp(41, 41);
    bmp.fill({0, 0, 0, 0});
    for (int y = 0; y < 41; ++y) {
        for (int x = 0; x < 41; ++x) bmp.setPixel(x, y, {static_cast<uint8_t>(x * 6 % 256), 40, 200, 255});
    }
    const auto centerBefore = bmp.pixel(20, 20);

    core::Effect rb;
    rb.type = core::EffectType::RadialBlur;
    rb.params = core::effectDefaultParams(core::EffectType::RadialBlur);  // centerX/Y=0.5
    rb.params["amount"] = 0.15;
    core::applyEffect(bmp, rb, 0);

    const auto centerAfter = bmp.pixel(20, 20);
    REQUIRE(centerAfter.r == centerBefore.r);
    REQUIRE(centerAfter.g == centerBefore.g);
    REQUIRE(centerAfter.b == centerBefore.b);
}

TEST_CASE("applyRadialBlur blurs pixels away from the center", "[core][effect][radialblur]") {
    core::Bitmap bmp(41, 41);
    bmp.fill({0, 0, 0, 255});
    bmp.setPixel(35, 20, {255, 255, 255, 255});  // 中心から離れた1点の目印
    const auto before = bmp.pixel(35, 20);

    core::Effect rb;
    rb.type = core::EffectType::RadialBlur;
    rb.params = core::effectDefaultParams(core::EffectType::RadialBlur);
    rb.params["amount"] = 0.2;
    core::applyEffect(bmp, rb, 0);

    const auto after = bmp.pixel(35, 20);
    REQUIRE(after.r < before.r);  // 縮小サンプリングで周辺の暗い色と混ざり薄まる
}

// --- Vignette(ビネット) ---

TEST_CASE("applyVignette darkens the corners but leaves the exact center unchanged", "[core][effect][vignette]") {
    core::Bitmap bmp(41, 41);
    bmp.fill({200, 200, 200, 255});

    core::Effect vg;
    vg.type = core::EffectType::Vignette;
    vg.params = core::effectDefaultParams(core::EffectType::Vignette);  // amount=0.4, softness=0.5
    core::applyEffect(bmp, vg, 0);

    const auto center = bmp.pixel(20, 20);
    REQUIRE(center.r == 200);  // 中心(距離0)は不変

    const auto corner = bmp.pixel(0, 0);
    REQUIRE(corner.r < 200);  // 四隅は暗くなる
}

// --- Grain(グレイン) ---

TEST_CASE("applyGrain is deterministic for the same frame and unchanged when amount is 0", "[core][effect][grain]") {
    core::Bitmap bmpA(16, 16);
    bmpA.fill({128, 128, 128, 255});
    core::Bitmap bmpB = bmpA;
    core::Bitmap bmpZero = bmpA;

    core::Effect grain;
    grain.type = core::EffectType::Grain;
    grain.params = core::effectDefaultParams(core::EffectType::Grain);  // amount=0.15

    core::applyEffect(bmpA, grain, 3);
    core::applyEffect(bmpB, grain, 3);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            REQUIRE(bmpA.pixel(x, y).r == bmpB.pixel(x, y).r);
            REQUIRE(bmpA.pixel(x, y).g == bmpB.pixel(x, y).g);
            REQUIRE(bmpA.pixel(x, y).b == bmpB.pixel(x, y).b);
        }
    }

    // 決定論的ノイズなので、既定パラメータでは輝度が変化しているはず(全ピクセル完全一致ではない)
    bool anyDifferent = false;
    for (int y = 0; y < 16 && !anyDifferent; ++y) {
        for (int x = 0; x < 16 && !anyDifferent; ++x) {
            if (bmpA.pixel(x, y).r != 128) anyDifferent = true;
        }
    }
    REQUIRE(anyDifferent);

    core::Effect grainZero;
    grainZero.type = core::EffectType::Grain;
    grainZero.params = {{"amount", 0.0}, {"size", 1.0}};
    core::applyEffect(bmpZero, grainZero, 3);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) REQUIRE(bmpZero.pixel(x, y).r == 128);  // amount=0は不変
    }
}

TEST_CASE("applyGrain produces a different pattern for a different frame", "[core][effect][grain]") {
    core::Bitmap base(16, 16);
    base.fill({128, 128, 128, 255});

    core::Effect grain;
    grain.type = core::EffectType::Grain;
    grain.params = core::effectDefaultParams(core::EffectType::Grain);  // amount=0.15

    core::Bitmap atFrame0 = base;
    core::applyEffect(atFrame0, grain, 0);
    core::Bitmap atFrame1 = base;
    core::applyEffect(atFrame1, grain, 1);

    bool anyDifferent = false;
    for (int y = 0; y < 16 && !anyDifferent; ++y) {
        for (int x = 0; x < 16 && !anyDifferent; ++x) {
            if (atFrame0.pixel(x, y).r != atFrame1.pixel(x, y).r) anyDifferent = true;
        }
    }
    REQUIRE(anyDifferent);
}

TEST_CASE("applyGrain frame-to-frame noise is uncorrelated across blocks (not a uniform shift)",
          "[core][effect][grain]") {
    // バグ修正前(minstd_randへシードを線形に渡すだけ)は、フレームを1つ進めたときの
    // ノイズ変化がブロック間でほぼ一様なシフトになってしまっていた。ハッシュノイズへの
    // 置き換え後は、離れた2ブロックでの「フレーム0→1の変化量」が一致しない(無相関)ことを確認する
    core::Bitmap base(32, 32);
    base.fill({128, 128, 128, 255});

    core::Effect grain;
    grain.type = core::EffectType::Grain;
    grain.params = {{"amount", 0.5}, {"size", 1.0}};  // 各ピクセルが独立ブロック(size=1)

    core::Bitmap atFrame0 = base;
    core::applyEffect(atFrame0, grain, 100);
    core::Bitmap atFrame1 = base;
    core::applyEffect(atFrame1, grain, 101);

    // 十分離れた2ブロック(A, B)について、フレーム0→1での輝度変化量を比較する
    const int deltaA = static_cast<int>(atFrame1.pixel(3, 5).r) - static_cast<int>(atFrame0.pixel(3, 5).r);
    const int deltaB = static_cast<int>(atFrame1.pixel(27, 21).r) - static_cast<int>(atFrame0.pixel(27, 21).r);
    REQUIRE(deltaA != deltaB);  // 一様ずれ(全ブロックで同じdelta)であれば一致してしまうはず
}

// --- Film(フィルム) ---

TEST_CASE("applyFilm is byte-identical when all parameters are identity", "[core][effect][film]") {
    core::Bitmap bmp(6, 6);
    bmp.fill({0, 0, 0, 0});
    for (int y = 0; y < 6; ++y) {
        for (int x = 0; x < 6; ++x) {
            bmp.setPixel(x, y, {static_cast<uint8_t>(x * 40), static_cast<uint8_t>(y * 40), 90, 255});
        }
    }
    core::Bitmap before = bmp;

    core::Effect film;
    film.type = core::EffectType::Film;
    film.params = {{"exposure", 0.0}, {"contrast", 0.0}, {"fade", 0.0}, {"warmth", 0.0},
                    {"crosstalk", 0.0}, {"grain", 0.0},    {"grainSize", 1.0}};
    core::applyEffect(bmp, film, 0);

    for (int y = 0; y < 6; ++y) {
        for (int x = 0; x < 6; ++x) {
            REQUIRE(bmp.pixel(x, y).r == before.pixel(x, y).r);
            REQUIRE(bmp.pixel(x, y).g == before.pixel(x, y).g);
            REQUIRE(bmp.pixel(x, y).b == before.pixel(x, y).b);
            REQUIRE(bmp.pixel(x, y).a == before.pixel(x, y).a);
        }
    }
}

TEST_CASE("applyFilm crosstalk mixes pure red toward green/blue (row-normalized)", "[core][effect][film]") {
    core::Bitmap bmp(2, 2);
    bmp.fill({255, 0, 0, 255});

    core::Effect film;
    film.type = core::EffectType::Film;
    film.params = {{"exposure", 0.0}, {"contrast", 0.0}, {"fade", 0.0}, {"warmth", 0.0},
                    {"crosstalk", 0.2}, {"grain", 0.0},    {"grainSize", 1.0}};
    core::applyEffect(bmp, film, 0);

    const auto p = bmp.pixel(0, 0);
    REQUIRE(p.r < 255);  // Rは減る
    REQUIRE(p.g > 0);    // Gは増える
    REQUIRE(p.b > 0);    // Bは増える
}

TEST_CASE("applyFilm characteristic curve applies an S-curve and fade lifts black", "[core][effect][film]") {
    auto runGray = [](uint8_t level, double contrast, double fade) {
        core::Bitmap bmp(2, 2);
        bmp.fill({level, level, level, 255});
        core::Effect film;
        film.type = core::EffectType::Film;
        film.params = {{"exposure", 0.0}, {"contrast", contrast}, {"fade", fade}, {"warmth", 0.0},
                        {"crosstalk", 0.0}, {"grain", 0.0},         {"grainSize", 1.0}};
        core::applyEffect(bmp, film, 0);
        return bmp.pixel(0, 0).r;
    };

    // contrast=1でS字: 中間調(128)はほぼ不変、暗部(32)はより暗く、明部(224)はより明るくなる
    const uint8_t mid = runGray(128, 1.0, 0.0);
    REQUIRE(std::abs(static_cast<int>(mid) - 128) <= 2);
    REQUIRE(runGray(32, 1.0, 0.0) < 32);
    REQUIRE(runGray(224, 1.0, 0.0) > 224);

    // fade=0.1で黒(0)が持ち上がる
    REQUIRE(runGray(0, 0.0, 0.1) > 0);
}

TEST_CASE("applyFilm grain leaves black/white unchanged and varies midtones, independently per channel",
          "[core][effect][film]") {
    core::Effect film;
    film.type = core::EffectType::Film;
    film.params = {{"exposure", 0.0}, {"contrast", 0.0}, {"fade", 0.0}, {"warmth", 0.0},
                    {"crosstalk", 0.0}, {"grain", 1.0},    {"grainSize", 1.0}};

    // 黒(0)・白(255)は中間調重み(4y(1-y))が0になるため不変
    core::Bitmap black(4, 4);
    black.fill({0, 0, 0, 255});
    core::applyEffect(black, film, 7);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) REQUIRE(black.pixel(x, y).r == 0);

    core::Bitmap white(4, 4);
    white.fill({255, 255, 255, 255});
    core::applyEffect(white, film, 7);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) REQUIRE(white.pixel(x, y).r == 255);

    // 中間調(128)は変化する
    core::Bitmap mid(4, 4);
    mid.fill({128, 128, 128, 255});
    core::Bitmap midBefore = mid;
    core::applyEffect(mid, film, 7);
    bool midChanged = false;
    for (int y = 0; y < 4 && !midChanged; ++y) {
        for (int x = 0; x < 4 && !midChanged; ++x) {
            if (mid.pixel(x, y).r != midBefore.pixel(x, y).r) midChanged = true;
        }
    }
    REQUIRE(midChanged);

    // RGB独立: 十分広い中間調の面で、あるピクセルはRとGのノイズ符号が異なるはず
    core::Bitmap grid(24, 24);
    grid.fill({128, 128, 128, 255});
    core::applyEffect(grid, film, 7);
    bool foundDivergentSign = false;
    for (int y = 0; y < 24 && !foundDivergentSign; ++y) {
        for (int x = 0; x < 24 && !foundDivergentSign; ++x) {
            const auto p = grid.pixel(x, y);
            const int dr = static_cast<int>(p.r) - 128;
            const int dg = static_cast<int>(p.g) - 128;
            if ((dr > 0 && dg < 0) || (dr < 0 && dg > 0)) foundDivergentSign = true;
        }
    }
    REQUIRE(foundDivergentSign);

    // フレーム0と1で粒パターンが異なる
    core::Bitmap gridFrame0(16, 16);
    gridFrame0.fill({128, 128, 128, 255});
    core::Bitmap gridFrame1 = gridFrame0;
    core::applyEffect(gridFrame0, film, 0);
    core::applyEffect(gridFrame1, film, 1);
    bool frameDifferent = false;
    for (int y = 0; y < 16 && !frameDifferent; ++y) {
        for (int x = 0; x < 16 && !frameDifferent; ++x) {
            if (gridFrame0.pixel(x, y).r != gridFrame1.pixel(x, y).r) frameDifferent = true;
        }
    }
    REQUIRE(frameDifferent);
}

// --- ChromAb(色収差) ---

TEST_CASE("applyChromAb leaves the exact center pixel unchanged", "[core][effect][chromab]") {
    core::Bitmap bmp(41, 41);
    for (int y = 0; y < 41; ++y) {
        for (int x = 0; x < 41; ++x) {
            bmp.setPixel(x, y, {static_cast<uint8_t>(x * 6 % 256), 128, static_cast<uint8_t>(y * 6 % 256), 255});
        }
    }
    const auto centerBefore = bmp.pixel(20, 20);

    core::Effect ca;
    ca.type = core::EffectType::ChromAb;
    ca.params = {{"amount", 8.0}};
    core::applyEffect(bmp, ca, 0);

    const auto centerAfter = bmp.pixel(20, 20);
    REQUIRE(centerAfter.r == centerBefore.r);
    REQUIRE(centerAfter.g == centerBefore.g);
    REQUIRE(centerAfter.b == centerBefore.b);
}

TEST_CASE("applyChromAb shifts the R/B channels away from the center", "[core][effect][chromab]") {
    // 横方向にR/Bが変化する縞模様(Gは一定)にすることで、周辺でのチャンネルずれを検出しやすくする
    core::Bitmap bmp(41, 21);
    for (int y = 0; y < 21; ++y) {
        for (int x = 0; x < 41; ++x) {
            const uint8_t stripeR = (x % 4 < 2) ? 255 : 0;
            const uint8_t stripeB = (x % 4 < 2) ? 0 : 255;
            bmp.setPixel(x, y, {stripeR, 128, stripeB, 255});
        }
    }
    core::Bitmap before = bmp;

    core::Effect ca;
    ca.type = core::EffectType::ChromAb;
    ca.params = {{"amount", 10.0}};
    core::applyEffect(bmp, ca, 0);

    // 右端付近(中心から離れた場所)でR/Bのいずれかが元の縞模様からずれているはず
    bool anyDifferent = false;
    for (int x = 35; x < 40 && !anyDifferent; ++x) {
        const auto a = bmp.pixel(x, 10);
        const auto b = before.pixel(x, 10);
        if (a.r != b.r || a.b != b.b) anyDifferent = true;
    }
    REQUIRE(anyDifferent);

    // Gチャンネルはどのピクセルでもそのまま(シフトされない)
    for (int x = 0; x < 41; ++x) REQUIRE(bmp.pixel(x, 10).g == 128);
}

// --- Compositor統合 ---

TEST_CASE("renderCutFrame is byte-identical when effects are empty", "[core][effect][compositor]") {
    core::Cut cut("Cut 1");
    core::Cel& cel = cut.addCel("A");
    core::Layer& layer = cel.addLayer("線画");
    addDrawingWithDot(layer, 2, 2, {0, 0, 0, 255}, 8, 8);
    cut.setFrameCount(1);
    cel.setExposure(0, 0);

    REQUIRE(cut.effects().empty());
    const auto out = core::renderCutFrame(cut, 0, 8, 8);
    REQUIRE(out.pixel(0, 0).r == 255);  // 紙は白のまま
    REQUIRE(out.pixel(2, 2).r == 0);    // 目印はそのまま
}

TEST_CASE("renderCutFrame applies a whole-screen effect (targetCel=-1)", "[core][effect][compositor]") {
    core::Cut cut("Cut 1");
    core::Cel& cel = cut.addCel("A");
    core::Layer& layer = cel.addLayer("線画");
    addDrawingWithDot(layer, 20, 20, {0, 0, 0, 255}, 41, 41);
    cut.setFrameCount(1);
    cel.setExposure(0, 0);

    core::Effect blur;
    blur.type = core::EffectType::Blur;
    blur.targetCel = -1;
    blur.params = {{"radius", 3.0}};
    cut.effects().push_back(blur);

    const auto out = core::renderCutFrame(cut, 0, 41, 41);
    // 全体ブラーにより、黒点周辺(紙の上)がわずかに暗くなっているはず
    REQUIRE(out.pixel(21, 20).r < 255);
    REQUIRE(out.pixel(21, 20).r > 0);
}

TEST_CASE("renderCutFrame applies an effect only to its targetCel", "[core][effect][compositor]") {
    core::Cut cut("Cut 1");
    core::Cel& celA = cut.addCel("A");
    addDrawingWithDot(celA.addLayer("L"), 20, 20, {0, 0, 0, 255}, 41, 41);
    core::Cel& celB = cut.addCel("B");
    addDrawingWithDot(celB.addLayer("L"), 5, 5, {255, 0, 0, 255}, 41, 41);
    cut.setFrameCount(1);
    celA.setExposure(0, 0);
    celB.setExposure(0, 0);

    core::Effect blur;
    blur.type = core::EffectType::Blur;
    blur.targetCel = 0;  // セルAのみ
    blur.params = {{"radius", 3.0}};
    cut.effects().push_back(blur);

    const auto out = core::renderCutFrame(cut, 0, 41, 41);
    // セルA周辺は滲んでいる
    REQUIRE(out.pixel(21, 20).r < 255);
    // セルBの赤点はブラーの影響を受けず、そのまま鮮明
    REQUIRE(out.pixel(5, 5).r == 255);
    REQUIRE(out.pixel(5, 5).g == 0);
    REQUIRE(out.pixel(4, 5).g == 255);  // セルBの隣接ピクセルは滲んでいない(紙の白のまま)
}

// --- ProjectIO往復 ---

TEST_CASE("Cut effects round trip through ppam", "[core][io][effect]") {
    core::Project project("P");
    core::Scene& scene = project.addScene("S");
    core::Cut& cut = scene.addCut("Cut A");
    cut.addCel("A").addLayer("L").addFrame();
    cut.addCel("B").addLayer("L").addFrame();

    core::Effect blur;
    blur.type = core::EffectType::Blur;
    blur.enabled = true;
    blur.targetCel = -1;
    blur.params = {{"radius", 5.5}};
    cut.effects().push_back(blur);

    core::Effect shake;
    shake.type = core::EffectType::Shake;
    shake.enabled = false;
    shake.targetCel = 1;
    shake.params = {{"amplitudeX", 12.0}, {"amplitudeY", 3.0}, {"seed", 42.0}};
    cut.effects().push_back(shake);

    const auto path = std::filesystem::temp_directory_path() / "ppam_effect_test.ppproj";
    std::string error;
    REQUIRE(core::ProjectIO::save(project, path, &error));
    const auto loaded = core::ProjectIO::load(path, &error);
    REQUIRE(loaded != nullptr);

    const auto& effects = loaded->scene(0).cut(0).effects();
    REQUIRE(effects.size() == 2);

    REQUIRE(effects[0].type == core::EffectType::Blur);
    REQUIRE(effects[0].enabled == true);
    REQUIRE(effects[0].targetCel == -1);
    REQUIRE(effects[0].params.at("radius") == 5.5);

    REQUIRE(effects[1].type == core::EffectType::Shake);
    REQUIRE(effects[1].enabled == false);
    REQUIRE(effects[1].targetCel == 1);
    REQUIRE(effects[1].params.at("amplitudeX") == 12.0);
    REQUIRE(effects[1].params.at("amplitudeY") == 3.0);
    REQUIRE(effects[1].params.at("seed") == 42.0);

    std::filesystem::remove_all(path);
}

TEST_CASE("Cut with no effects omits the effects field and round trips as empty", "[core][io][effect]") {
    core::Project project("P");
    project.addScene("S").addCut("Cut A").addCel("A").addLayer("L").addFrame();

    const auto path = std::filesystem::temp_directory_path() / "ppam_effect_empty_test.ppproj";
    std::string error;
    REQUIRE(core::ProjectIO::save(project, path, &error));
    const auto loaded = core::ProjectIO::load(path, &error);
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->scene(0).cut(0).effects().empty());

    std::filesystem::remove_all(path);
}

// --- プロパティ単位のキーフレーム曲線(AE式、コマ補間) ---

TEST_CASE("Effect::paramsAt interpolates per-property keyframes", "[core][effect][keys]") {
    core::Effect blur;
    blur.type = core::EffectType::Blur;
    blur.params = {{"radius", 4.0}};

    SECTION("キーが無ければ基本値を返す") {
        REQUIRE(blur.paramsAt(0).at("radius") == 4.0);
        REQUIRE(blur.paramsAt(100).at("radius") == 4.0);
        REQUIRE_FALSE(blur.hasCurve("radius"));
    }

    SECTION("1キーなら常にそのキー値") {
        blur.setKey("radius", 10, 8.0);
        REQUIRE(blur.hasCurve("radius"));
        REQUIRE(blur.paramsAt(0).at("radius") == 8.0);
        REQUIRE(blur.paramsAt(10).at("radius") == 8.0);
        REQUIRE(blur.paramsAt(99).at("radius") == 8.0);
    }

    SECTION("2キー間は線形補間、範囲外はクランプ") {
        blur.setKey("radius", 0, 0.0);
        blur.setKey("radius", 10, 10.0);
        REQUIRE(blur.valueAt("radius", 5) == 5.0);
        REQUIRE(blur.valueAt("radius", 0) == 0.0);
        REQUIRE(blur.valueAt("radius", 10) == 10.0);
        REQUIRE(blur.valueAt("radius", 50) == 10.0);  // 最後のキー以後はクランプ
    }

    SECTION("キーを打ったパラメータだけが変化し、他は基本値のまま") {
        core::Effect glow;
        glow.type = core::EffectType::Glow;
        glow.params = {{"threshold", 200.0}, {"radius", 8.0}, {"strength", 0.6}};
        glow.setKey("strength", 0, 0.0);
        glow.setKey("strength", 10, 1.0);
        const auto mid = glow.paramsAt(5);
        REQUIRE(mid.at("strength") == 0.5);
        REQUIRE(mid.at("threshold") == 200.0);  // キーの無いパラメータは基本値
        REQUIRE(mid.at("radius") == 8.0);
        REQUIRE(glow.hasCurve("strength"));
        REQUIRE_FALSE(glow.hasCurve("radius"));
    }

    SECTION("setKey/removeKey/hasKeyAt") {
        blur.setKey("radius", 5, 3.0);
        blur.setKey("radius", 15, 7.0);
        REQUIRE(blur.hasKeyAt("radius", 5));
        REQUIRE(blur.hasKeyAt("radius", 15));
        REQUIRE_FALSE(blur.hasKeyAt("radius", 10));
        blur.removeKey("radius", 5);
        REQUIRE_FALSE(blur.hasKeyAt("radius", 5));
        REQUIRE(blur.hasCurve("radius"));  // まだ1個残る
        blur.removeKey("radius", 15);
        REQUIRE_FALSE(blur.hasCurve("radius"));  // 最後の1個を消したら曲線ごと消える
        REQUIRE(blur.paramsAt(0).at("radius") == 4.0);  // 基本値に戻る
    }
}

TEST_CASE("applyEffect uses interpolated keyframe values (Blur radius 0 -> 4)", "[core][effect][keys]") {
    // 半径キーはコマ0=0(ボケなし)→コマ10=4。半径を大きくしすぎると1点のアルファが
    // 8bit量子化で0に潰れるため、既定と同じ半径4で検証する
    core::Effect blur;
    blur.type = core::EffectType::Blur;
    blur.params = core::effectDefaultParams(core::EffectType::Blur);
    blur.setKey("radius", 0, 0.0);
    blur.setKey("radius", 10, 4.0);

    // コマ0: 半径0なのでボケない(目印ピクセルがそのまま)
    core::Bitmap at0 = makeTransparentDot(41, 41, 20, 20, {200, 50, 50, 255});
    core::applyEffect(at0, blur, 0);
    REQUIRE(at0.pixel(20, 20).a == 255);
    REQUIRE(at0.pixel(21, 20).a == 0);

    // コマ10: 半径4でボケる(目印が薄まり周囲へ広がる)
    core::Bitmap at10 = makeTransparentDot(41, 41, 20, 20, {200, 50, 50, 255});
    core::applyEffect(at10, blur, 10);
    REQUIRE(at10.pixel(20, 20).a < 255);
    REQUIRE(at10.pixel(21, 20).a > 0);
}

TEST_CASE("Effect mask limits a whole-screen effect to the painted region", "[core][effect][mask]") {
    // 全面に均一の色を塗ったセルを1枚だけ持つカット。左半分だけマスクした黒パラ(上濃度も
    // 一定になるよう top=bottom=0.5)をかけると、左半分だけ暗くなり右半分は元のままになる
    core::Cut cut("C");
    core::Cel& cel = cut.addCel("A");
    core::Layer& layer = cel.addLayer("L");
    core::Bitmap art(40, 20);
    art.fill({200, 200, 200, 255});  // 不透明グレー(パラは透明部分に効かないため不透明にする)
    layer.addFrame().bitmap() = std::move(art);
    cel.setExposure(0, 0);
    cut.setFrameCount(1);

    core::Effect para;
    para.type = core::EffectType::Para;
    para.targetCel = -1;  // 全体
    para.params = {{"top", 0.5}, {"bottom", 0.5}, {"r", 0.0}, {"g", 0.0}, {"b", 0.0}};  // 一定濃度の黒
    // マスク: 画面(40x20)の左半分だけ不透明
    core::Bitmap mask(40, 20);
    mask.fill({0, 0, 0, 0});
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 20; ++x) mask.setPixel(x, y, {255, 255, 255, 255});
    para.mask = std::move(mask);
    cut.effects().push_back(std::move(para));

    const core::Bitmap out = core::renderCutFrame(cut, 0, 40, 20);
    // 左半分(マスク内): 200 → 100付近(黒と0.5でブレンド)
    REQUIRE(out.pixel(5, 10).r < 150);
    // 右半分(マスク外): 元の200のまま
    REQUIRE(out.pixel(35, 10).r == 200);
}

TEST_CASE("Effect mask round trips through ppam", "[core][io][effect][mask]") {
    core::Project project("P");
    core::Scene& scene = project.addScene("S");
    core::Cut& cut = scene.addCut("Cut A");
    cut.addCel("A").addLayer("L").addFrame();

    core::Effect blur;
    blur.type = core::EffectType::Blur;
    blur.params = {{"radius", 4.0}};
    core::Bitmap mask(8, 6);
    mask.fill({0, 0, 0, 0});
    mask.setPixel(3, 2, {255, 255, 255, 200});
    blur.mask = std::move(mask);
    cut.effects().push_back(blur);

    const auto path = std::filesystem::temp_directory_path() / "ppam_effect_mask_test.ppproj";
    std::string error;
    REQUIRE(core::ProjectIO::save(project, path, &error));
    const auto loaded = core::ProjectIO::load(path, &error);
    REQUIRE(loaded != nullptr);

    const auto& effect = loaded->scene(0).cut(0).effects().at(0);
    REQUIRE_FALSE(effect.mask.isEmpty());
    REQUIRE(effect.mask.width() == 8);
    REQUIRE(effect.mask.height() == 6);
    REQUIRE(effect.mask.pixel(3, 2).a == 200);
    REQUIRE(effect.mask.pixel(0, 0).a == 0);

    std::filesystem::remove_all(path);
}

// --- 適用範囲のコマ(in/out点) ---

TEST_CASE("Effect::activeAt honors default and explicit in/out points", "[core][effect][range]") {
    core::Effect effect;
    effect.type = core::EffectType::Blur;

    SECTION("既定(0,-1)は常にtrue") {
        REQUIRE(effect.activeAt(0));
        REQUIRE(effect.activeAt(50));
        REQUIRE(effect.activeAt(9999));
    }

    SECTION("範囲指定: in点より前とout点より後はfalse") {
        effect.startFrame = 5;
        effect.endFrame = 10;
        REQUIRE_FALSE(effect.activeAt(4));
        REQUIRE(effect.activeAt(5));
        REQUIRE(effect.activeAt(7));
        REQUIRE(effect.activeAt(10));
        REQUIRE_FALSE(effect.activeAt(11));
    }

    SECTION("out点=-1のみ指定: in点以降は末尾まで常にtrue") {
        effect.startFrame = 3;
        effect.endFrame = -1;
        REQUIRE_FALSE(effect.activeAt(2));
        REQUIRE(effect.activeAt(3));
        REQUIRE(effect.activeAt(9999));
    }
}

TEST_CASE("renderCutFrame respects an effect's in/out points", "[core][effect][range][compositor]") {
    core::Cut cut("Cut 1");
    core::Cel& cel = cut.addCel("A");
    core::Layer& layer = cel.addLayer("線画");
    addDrawingWithDot(layer, 20, 20, {0, 0, 0, 255}, 41, 41);
    cut.setFrameCount(12);
    for (size_t f = 0; f < 12; ++f) cel.setExposure(f, 0);  // 全コマ同じ動画をホールド

    core::Effect blur;
    blur.type = core::EffectType::Blur;
    blur.targetCel = -1;  // 全体
    blur.params = {{"radius", 3.0}};
    blur.startFrame = 5;
    blur.endFrame = 10;
    cut.effects().push_back(blur);

    // コマ4(in点より前): エフェクト無しとバイト同一(比較用に別カットで生成)
    core::Cut noEffectCut("Cut 1 (no effect)");
    core::Cel& noEffectCel = noEffectCut.addCel("A");
    addDrawingWithDot(noEffectCel.addLayer("線画"), 20, 20, {0, 0, 0, 255}, 41, 41);
    noEffectCut.setFrameCount(12);
    for (size_t f = 0; f < 12; ++f) noEffectCel.setExposure(f, 0);
    const auto refAt4 = core::renderCutFrame(noEffectCut, 4, 41, 41);
    const auto outAt4 = core::renderCutFrame(cut, 4, 41, 41);
    for (int y = 0; y < 41; ++y) {
        for (int x = 0; x < 41; ++x) {
            REQUIRE(outAt4.pixel(x, y).r == refAt4.pixel(x, y).r);
            REQUIRE(outAt4.pixel(x, y).a == refAt4.pixel(x, y).a);
        }
    }

    // コマ7(範囲内): ブラーが効いて黒点周辺が滲む
    const auto outAt7 = core::renderCutFrame(cut, 7, 41, 41);
    REQUIRE(outAt7.pixel(21, 20).r < 255);
    REQUIRE(outAt7.pixel(21, 20).r > 0);
}

TEST_CASE("Effect startFrame/endFrame round trip through ppam", "[core][io][effect][range]") {
    core::Project project("P");
    core::Scene& scene = project.addScene("S");
    core::Cut& cut = scene.addCut("Cut A");
    cut.addCel("A").addLayer("L").addFrame();

    core::Effect blur;
    blur.type = core::EffectType::Blur;
    blur.params = {{"radius", 4.0}};
    blur.startFrame = 5;
    blur.endFrame = 20;
    cut.effects().push_back(blur);

    core::Effect defaultRange;
    defaultRange.type = core::EffectType::Shake;
    defaultRange.params = core::effectDefaultParams(core::EffectType::Shake);
    cut.effects().push_back(defaultRange);  // 既定(0,-1)のまま

    const auto path = std::filesystem::temp_directory_path() / "ppam_effect_range_test.ppproj";
    std::string error;
    REQUIRE(core::ProjectIO::save(project, path, &error));
    const auto loaded = core::ProjectIO::load(path, &error);
    REQUIRE(loaded != nullptr);

    const auto& effects = loaded->scene(0).cut(0).effects();
    REQUIRE(effects.size() == 2);
    REQUIRE(effects[0].startFrame == 5);
    REQUIRE(effects[0].endFrame == 20);
    REQUIRE(effects[1].startFrame == 0);
    REQUIRE(effects[1].endFrame == -1);

    std::filesystem::remove_all(path);
}

TEST_CASE("Effect paramCurves round trip through ppam", "[core][io][effect][keys]") {
    core::Project project("P");
    core::Scene& scene = project.addScene("S");
    core::Cut& cut = scene.addCut("Cut A");
    cut.addCel("A").addLayer("L").addFrame();

    core::Effect blur;
    blur.type = core::EffectType::Blur;
    blur.params = {{"radius", 4.0}};
    blur.setKey("radius", 0, 0.0);
    blur.setKey("radius", 23, 10.0);
    cut.effects().push_back(blur);

    const auto path = std::filesystem::temp_directory_path() / "ppam_effect_curves_test.ppproj";
    std::string error;
    REQUIRE(core::ProjectIO::save(project, path, &error));
    const auto loaded = core::ProjectIO::load(path, &error);
    REQUIRE(loaded != nullptr);

    const auto& effect = loaded->scene(0).cut(0).effects().at(0);
    REQUIRE(effect.paramCurves.size() == 1);
    REQUIRE(effect.paramCurves.at("radius").size() == 2);
    REQUIRE(effect.paramCurves.at("radius").at(0) == 0.0);
    REQUIRE(effect.paramCurves.at("radius").at(23) == 10.0);

    std::filesystem::remove_all(path);
}
