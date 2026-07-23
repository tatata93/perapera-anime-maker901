#pragma once

#include <vector>

#include "BrushEngine.h"

namespace core {

struct LassoPoint {
    float x = 0.0f;
    float y = 0.0f;
};

// Fills the polygon using the even-odd rule. Pixel centers determine inclusion.
DirtyRect fillLasso(Bitmap& target, const std::vector<LassoPoint>& points, Bitmap::Pixel color);

}  // namespace core
