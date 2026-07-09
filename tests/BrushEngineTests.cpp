#include <catch2/catch_test_macros.hpp>

#include "core/BrushEngine.h"

namespace {

core::Bitmap makeWhiteBitmap(int w, int h) {
    core::Bitmap bitmap(w, h);
    bitmap.fill({255, 255, 255, 255});
    return bitmap;
}

}  // namespace

TEST_CASE("DirtyRect unite and clamp", "[core][brush]") {
    core::DirtyRect a{2, 3, 10, 12};
    core::DirtyRect b{8, 1, 15, 5};
    a.unite(b);
    REQUIRE(a.x0 == 2);
    REQUIRE(a.y0 == 1);
    REQUIRE(a.x1 == 15);
    REQUIRE(a.y1 == 12);

    a.clampTo(12, 10);
    REQUIRE(a.x1 == 12);
    REQUIRE(a.y1 == 10);

    core::DirtyRect empty{};
    REQUIRE(empty.isEmpty());
    empty.unite(a);
    REQUIRE(empty.x0 == a.x0);
}

TEST_CASE("BrushEngine stamps a dot at stroke start", "[core][brush]") {
    auto bitmap = makeWhiteBitmap(64, 64);
    core::BrushEngine engine;
    engine.settings().radius = 5.0f;
    engine.settings().color = {0, 0, 0, 255};

    const auto dirty = engine.beginStroke(bitmap, 32.0f, 32.0f, 1.0f);
    engine.endStroke();

    REQUIRE_FALSE(dirty.isEmpty());
    // 中心は黒くなる
    const auto center = bitmap.pixel(32, 32);
    REQUIRE(center.r < 30);
    // 半径の外(中心から10px)は白のまま
    const auto outside = bitmap.pixel(32, 45);
    REQUIRE(outside.r == 255);
}

TEST_CASE("BrushEngine draws a continuous line between points", "[core][brush]") {
    auto bitmap = makeWhiteBitmap(64, 64);
    core::BrushEngine engine;
    engine.settings().radius = 3.0f;
    engine.settings().color = {0, 0, 0, 255};

    engine.beginStroke(bitmap, 10.0f, 32.0f, 1.0f);
    engine.continueStroke(bitmap, 54.0f, 32.0f, 1.0f);
    engine.endStroke();

    // 線上の複数点が暗くなっている(スタンプ間隔よりも細かく確認)
    for (int x = 12; x <= 52; x += 4) {
        const auto px = bitmap.pixel(x, 32);
        INFO("x=" << x);
        REQUIRE(px.r < 100);
    }
}

TEST_CASE("Pressure affects stamp radius", "[core][brush]") {
    auto low = makeWhiteBitmap(64, 64);
    auto high = makeWhiteBitmap(64, 64);
    core::BrushEngine engine;
    engine.settings().radius = 10.0f;

    engine.beginStroke(low, 32.0f, 32.0f, 0.2f);
    engine.endStroke();
    engine.beginStroke(high, 32.0f, 32.0f, 1.0f);
    engine.endStroke();

    // 筆圧1.0では半径8px地点まで黒いが、筆圧0.2(半径2px)では白のまま
    REQUIRE(high.pixel(32 + 8, 32).r < 100);
    REQUIRE(low.pixel(32 + 8, 32).r == 255);
}

TEST_CASE("Painting on a transparent cel keeps the brush color", "[core][brush]") {
    core::Bitmap bitmap(64, 64);
    bitmap.fill({0, 0, 0, 0});  // 透明セル

    core::BrushEngine engine;
    engine.settings().radius = 8.0f;
    engine.settings().color = {200, 50, 50, 255};  // 赤系
    engine.beginStroke(bitmap, 32.0f, 32.0f, 1.0f);
    engine.endStroke();

    // straight-alphaのsrc-over: 透明部に描いても色が黒ずまない
    const auto px = bitmap.pixel(32, 32);
    REQUIRE(static_cast<int>(px.r) >= 195);
    REQUIRE(px.a > 250);
}

TEST_CASE("Erase mode restores transparency", "[core][brush]") {
    core::Bitmap bitmap(64, 64);
    bitmap.fill({0, 0, 0, 0});
    core::BrushEngine engine;

    // まず黒で塗る
    engine.settings().radius = 8.0f;
    engine.settings().color = {0, 0, 0, 255};
    engine.beginStroke(bitmap, 32.0f, 32.0f, 1.0f);
    engine.endStroke();
    REQUIRE(bitmap.pixel(32, 32).a > 250);

    // Eraseモードで透明に戻る
    engine.settings().mode = core::BrushMode::Erase;
    engine.settings().pressureAffectsRadius = false;
    engine.beginStroke(bitmap, 32.0f, 32.0f, 1.0f);
    engine.endStroke();
    REQUIRE(bitmap.pixel(32, 32).a < 10);
}
