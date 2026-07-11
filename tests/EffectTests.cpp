#include <catch2/catch_test_macros.hpp>
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

    const auto path = std::filesystem::temp_directory_path() / "ppam_effect_test.ppam";
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

    std::filesystem::remove(path);
}

TEST_CASE("Cut with no effects omits the effects field and round trips as empty", "[core][io][effect]") {
    core::Project project("P");
    project.addScene("S").addCut("Cut A").addCel("A").addLayer("L").addFrame();

    const auto path = std::filesystem::temp_directory_path() / "ppam_effect_empty_test.ppam";
    std::string error;
    REQUIRE(core::ProjectIO::save(project, path, &error));
    const auto loaded = core::ProjectIO::load(path, &error);
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->scene(0).cut(0).effects().empty());

    std::filesystem::remove(path);
}
