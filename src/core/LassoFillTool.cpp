#include "LassoFillTool.h"

#include <algorithm>
#include <cmath>

namespace core {

DirtyRect fillLasso(Bitmap& target, const std::vector<LassoPoint>& points, Bitmap::Pixel color) {
    if (target.isEmpty() || points.size() < 3) return {};

    float minY = points.front().y;
    float maxY = points.front().y;
    for (const LassoPoint& point : points) {
        minY = std::min(minY, point.y);
        maxY = std::max(maxY, point.y);
    }

    const int y0 = std::clamp(static_cast<int>(std::floor(minY)), 0, target.height());
    const int y1 = std::clamp(static_cast<int>(std::ceil(maxY)), 0, target.height());
    DirtyRect dirty;
    bool changed = false;
    std::vector<float> intersections;
    intersections.reserve(points.size());

    for (int y = y0; y < y1; ++y) {
        const float scanY = static_cast<float>(y) + 0.5f;
        intersections.clear();

        for (size_t i = 0, previous = points.size() - 1; i < points.size(); previous = i++) {
            const LassoPoint& a = points[previous];
            const LassoPoint& b = points[i];
            const bool crosses = (a.y <= scanY && b.y > scanY) || (b.y <= scanY && a.y > scanY);
            if (!crosses) continue;
            const float t = (scanY - a.y) / (b.y - a.y);
            intersections.push_back(a.x + (b.x - a.x) * t);
        }

        std::sort(intersections.begin(), intersections.end());
        for (size_t i = 0; i + 1 < intersections.size(); i += 2) {
            const int x0 = std::clamp(static_cast<int>(std::ceil(intersections[i] - 0.5f)), 0, target.width());
            const int x1 =
                std::clamp(static_cast<int>(std::ceil(intersections[i + 1] - 0.5f)), 0, target.width());
            for (int x = x0; x < x1; ++x) {
                const Bitmap::Pixel old = target.pixel(x, y);
                if (old.r == color.r && old.g == color.g && old.b == color.b && old.a == color.a) continue;
                target.setPixel(x, y, color);
                if (!changed) {
                    dirty = {x, y, x + 1, y + 1};
                    changed = true;
                } else {
                    dirty.x0 = std::min(dirty.x0, x);
                    dirty.y0 = std::min(dirty.y0, y);
                    dirty.x1 = std::max(dirty.x1, x + 1);
                    dirty.y1 = std::max(dirty.y1, y + 1);
                }
            }
        }
    }

    return changed ? dirty : DirtyRect{};
}

}  // namespace core
