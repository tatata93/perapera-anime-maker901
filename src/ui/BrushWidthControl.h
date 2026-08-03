#pragma once

#include <algorithm>
#include <cmath>

namespace perapera::ui {

// The UI displays the actual line width while BrushEngine stores a radius.
// The numeric control accepts hundredths while its arrow buttons move in
// practical 0.25 px increments. Keeping the slider on the same scale prevents
// direct numeric input from being rounded when the controls synchronize.
constexpr int kBrushWidthStepsPerPixel = 100;
constexpr int kBrushWidthSliderMin = 25;
constexpr int kBrushWidthSliderMax = 128 * kBrushWidthStepsPerPixel;
constexpr double kBrushWidthMin = 0.25;
constexpr double kBrushWidthMax = 128.0;
constexpr double kBrushWidthStep = 0.25;

inline float brushRadiusFromWidth(double width) {
    return static_cast<float>(std::clamp(width, kBrushWidthMin, kBrushWidthMax) * 0.5);
}

inline double brushWidthFromRadius(float radius) {
    return std::clamp(static_cast<double>(std::max(0.0f, radius)) * 2.0,
                      kBrushWidthMin, kBrushWidthMax);
}

inline float brushRadiusFromSliderValue(int value) {
    const float width = static_cast<float>(std::clamp(value, kBrushWidthSliderMin,
                                                       kBrushWidthSliderMax)) /
                        kBrushWidthStepsPerPixel;
    return brushRadiusFromWidth(width);
}

inline int sliderValueFromBrushRadius(float radius) {
    const int value = static_cast<int>(std::lround(std::max(0.0f, radius) * 2.0f *
                                                   kBrushWidthStepsPerPixel));
    return std::clamp(value, kBrushWidthSliderMin, kBrushWidthSliderMax);
}

}  // namespace perapera::ui
