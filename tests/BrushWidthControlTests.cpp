#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "ui/BrushWidthControl.h"

TEST_CASE("Brush width controls preserve fine numeric values", "[ui][brush-width]") {
    using namespace perapera::ui;

    REQUIRE(brushRadiusFromWidth(0.25) == Catch::Approx(0.125f));
    REQUIRE(brushRadiusFromWidth(1.37) == Catch::Approx(0.685f));
    REQUIRE(brushWidthFromRadius(0.685f) == Catch::Approx(1.37));
    REQUIRE(brushRadiusFromSliderValue(137) == Catch::Approx(0.685f));
    REQUIRE(sliderValueFromBrushRadius(0.685f) == 137);
}

TEST_CASE("Brush width controls clamp values to the supported range", "[ui][brush-width]") {
    using namespace perapera::ui;

    REQUIRE(brushRadiusFromWidth(0.0) == Catch::Approx(0.125f));
    REQUIRE(brushRadiusFromWidth(999.0) == Catch::Approx(64.0f));
    REQUIRE(sliderValueFromBrushRadius(0.0f) == kBrushWidthSliderMin);
    REQUIRE(sliderValueFromBrushRadius(999.0f) == kBrushWidthSliderMax);
}
