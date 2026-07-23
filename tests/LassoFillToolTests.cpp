#include <catch2/catch_test_macros.hpp>

#include "core/LassoFillTool.h"

TEST_CASE("Lasso fill colors only the enclosed polygon", "[core][lasso]") {
    core::Bitmap bitmap(12, 12);
    bitmap.fill({0, 0, 0, 0});
    const std::vector<core::LassoPoint> points{{2.0f, 2.0f}, {10.0f, 2.0f}, {10.0f, 10.0f}, {2.0f, 10.0f}};

    const core::DirtyRect dirty = core::fillLasso(bitmap, points, {30, 120, 220, 255});

    REQUIRE_FALSE(dirty.isEmpty());
    CHECK(bitmap.pixel(5, 5).a == 255);
    CHECK(bitmap.pixel(5, 5).b == 220);
    CHECK(bitmap.pixel(1, 5).a == 0);
    CHECK(bitmap.pixel(10, 5).a == 0);
}

TEST_CASE("Lasso fill supports concave outlines", "[core][lasso]") {
    core::Bitmap bitmap(10, 10);
    bitmap.fill({0, 0, 0, 0});
    const std::vector<core::LassoPoint> points{
        {1.0f, 1.0f}, {8.0f, 1.0f}, {8.0f, 3.0f}, {3.0f, 3.0f}, {3.0f, 8.0f}, {1.0f, 8.0f}};

    core::fillLasso(bitmap, points, {200, 40, 60, 255});

    CHECK(bitmap.pixel(2, 6).a == 255);
    CHECK(bitmap.pixel(6, 2).a == 255);
    CHECK(bitmap.pixel(6, 6).a == 0);
}

TEST_CASE("Lasso fill ignores incomplete outlines", "[core][lasso]") {
    core::Bitmap bitmap(8, 8);
    bitmap.fill({0, 0, 0, 0});

    const core::DirtyRect dirty =
        core::fillLasso(bitmap, {{1.0f, 1.0f}, {6.0f, 6.0f}}, {255, 0, 0, 255});

    CHECK(dirty.isEmpty());
    CHECK(bitmap.pixel(3, 3).a == 0);
}
