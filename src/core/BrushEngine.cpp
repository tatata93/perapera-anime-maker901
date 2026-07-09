#include "BrushEngine.h"

#include <algorithm>
#include <cmath>

namespace core {

void DirtyRect::unite(const DirtyRect& other) {
    if (other.isEmpty()) return;
    if (isEmpty()) {
        *this = other;
        return;
    }
    x0 = std::min(x0, other.x0);
    y0 = std::min(y0, other.y0);
    x1 = std::max(x1, other.x1);
    y1 = std::max(y1, other.y1);
}

void DirtyRect::clampTo(int width, int height) {
    x0 = std::clamp(x0, 0, width);
    y0 = std::clamp(y0, 0, height);
    x1 = std::clamp(x1, 0, width);
    y1 = std::clamp(y1, 0, height);
}

float BrushEngine::stampRadius(float pressure) const {
    if (!m_settings.pressureAffectsRadius) return m_settings.radius;
    return m_settings.radius * std::max(0.05f, std::min(pressure, 1.0f));
}

DirtyRect BrushEngine::stamp(Bitmap& bitmap, float cx, float cy, float pressure) const {
    const float radius = stampRadius(pressure);

    DirtyRect rect{
        static_cast<int>(std::floor(cx - radius - 1.0f)),
        static_cast<int>(std::floor(cy - radius - 1.0f)),
        static_cast<int>(std::ceil(cx + radius + 1.0f)) + 1,
        static_cast<int>(std::ceil(cy + radius + 1.0f)) + 1,
    };
    rect.clampTo(bitmap.width(), bitmap.height());
    if (rect.isEmpty()) return DirtyRect{};

    const Bitmap::Pixel& color = m_settings.color;
    const float baseAlpha = color.a / 255.0f;

    for (int y = rect.y0; y < rect.y1; ++y) {
        for (int x = rect.x0; x < rect.x1; ++x) {
            const float dx = (x + 0.5f) - cx;
            const float dy = (y + 0.5f) - cy;
            const float dist = std::sqrt(dx * dx + dy * dy);
            // 円境界±0.5pxでカバレッジを線形補間(アンチエイリアス)
            const float coverage = std::clamp(radius - dist + 0.5f, 0.0f, 1.0f);
            if (coverage <= 0.0f) continue;

            Bitmap::Pixel dst = bitmap.pixel(x, y);

            if (m_settings.mode == BrushMode::Erase) {
                // 透明に戻す: アルファをカバレッジ分削る(色は保持し、合成時はアルファで消える)
                dst.a = static_cast<uint8_t>(std::lround(dst.a * (1.0f - coverage)));
                bitmap.setPixel(x, y, dst);
                continue;
            }

            // straight-alphaのsrc-over合成。透明なdstに描いても色が黒ずまないよう、
            // dstの寄与はdst.aで重み付けし、結果をoutAで正規化する
            const float srcA = baseAlpha * coverage;
            const float dstA = dst.a / 255.0f;
            const float outA = srcA + dstA * (1.0f - srcA);
            if (outA > 0.0f) {
                const auto blend = [srcA, dstA, outA](uint8_t src, uint8_t d) {
                    return static_cast<uint8_t>(std::lround((src * srcA + d * dstA * (1.0f - srcA)) / outA));
                };
                dst.r = blend(color.r, dst.r);
                dst.g = blend(color.g, dst.g);
                dst.b = blend(color.b, dst.b);
            }
            dst.a = static_cast<uint8_t>(std::lround(outA * 255.0f));
            bitmap.setPixel(x, y, dst);
        }
    }
    return rect;
}

DirtyRect BrushEngine::beginStroke(Bitmap& bitmap, float x, float y, float pressure) {
    m_active = true;
    m_lastX = x;
    m_lastY = y;
    m_lastPressure = pressure;
    m_residual = 0.0f;
    return stamp(bitmap, x, y, pressure);
}

DirtyRect BrushEngine::continueStroke(Bitmap& bitmap, float x, float y, float pressure) {
    if (!m_active) return beginStroke(bitmap, x, y, pressure);

    const float dx = x - m_lastX;
    const float dy = y - m_lastY;
    const float dist = std::sqrt(dx * dx + dy * dy);

    DirtyRect dirty{};
    if (dist > 0.0f) {
        // 前回位置からの距離に沿って等間隔にスタンプを打つ(筆圧は線形補間)
        const float avgRadius = std::max(0.5f, stampRadius((m_lastPressure + pressure) * 0.5f));
        const float step = std::max(0.5f, avgRadius * m_settings.spacingRatio * 2.0f);

        float t = m_residual > 0.0f ? m_residual : step;
        while (t <= dist) {
            const float ratio = t / dist;
            const float px = m_lastX + dx * ratio;
            const float py = m_lastY + dy * ratio;
            const float pp = m_lastPressure + (pressure - m_lastPressure) * ratio;
            dirty.unite(stamp(bitmap, px, py, pp));
            t += step;
        }
        m_residual = t - dist;
    }

    m_lastX = x;
    m_lastY = y;
    m_lastPressure = pressure;
    return dirty;
}

void BrushEngine::endStroke() {
    m_active = false;
    m_residual = 0.0f;
}

}  // namespace core
