#pragma once

#include <QString>

#include <algorithm>
#include <cmath>

namespace perapera::ui {

// The UI displays the actual line width while BrushEngine stores a radius.
// Four slider steps per pixel allow 0.25 px lines without making the control
// needlessly sensitive at ordinary widths.
constexpr int kBrushWidthStepsPerPixel = 4;
constexpr int kBrushWidthSliderMin = 1;
constexpr int kBrushWidthSliderMax = 128 * kBrushWidthStepsPerPixel;

inline float brushRadiusFromSliderValue(int value) {
    const float width = static_cast<float>(std::clamp(value, kBrushWidthSliderMin,
                                                       kBrushWidthSliderMax)) /
                        kBrushWidthStepsPerPixel;
    return width * 0.5f;
}

inline int sliderValueFromBrushRadius(float radius) {
    const int value = static_cast<int>(std::lround(std::max(0.0f, radius) * 2.0f *
                                                   kBrushWidthStepsPerPixel));
    return std::clamp(value, kBrushWidthSliderMin, kBrushWidthSliderMax);
}

inline QString brushWidthLabel(float radius) {
    QString text = QString::number(std::max(0.0f, radius) * 2.0f, 'f', 2);
    while (text.endsWith(QLatin1Char('0'))) text.chop(1);
    if (text.endsWith(QLatin1Char('.'))) text.chop(1);
    return text;
}

}  // namespace perapera::ui
