#pragma once

#include <algorithm>
#include <string>

#include "Bitmap.h"
#include "Layer.h"

namespace core {

struct PaintLayer {
    std::string name;
    Bitmap bitmap;
    bool visible = true;
    double opacity = 1.0;
    LayerRole role = LayerRole::Normal;

    void setOpacity(double value) { opacity = std::clamp(value, 0.0, 1.0); }
};

}  // namespace core
