#include "Compositor.h"

#include <algorithm>
#include <cmath>

namespace core {

namespace {

// srcをdst(不透明前提)へ、(offsetX, offsetY)だけずらしてsrc-over合成する。
// はみ出す部分はクリップされる(タップ/ペグ移動・引きセル対応)
void blendOver(Bitmap& dst, const Bitmap& src, int offsetX, int offsetY) {
    const int x0 = std::max(0, offsetX);
    const int y0 = std::max(0, offsetY);
    const int x1 = std::min(dst.width(), src.width() + offsetX);
    const int y1 = std::min(dst.height(), src.height() + offsetY);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const Bitmap::Pixel s = src.pixel(x - offsetX, y - offsetY);
            if (s.a == 0) continue;
            if (s.a == 255) {
                dst.setPixel(x, y, {s.r, s.g, s.b, 255});
                continue;
            }
            const float a = s.a / 255.0f;
            Bitmap::Pixel d = dst.pixel(x, y);
            d.r = static_cast<uint8_t>(std::lround(s.r * a + d.r * (1.0f - a)));
            d.g = static_cast<uint8_t>(std::lround(s.g * a + d.g * (1.0f - a)));
            d.b = static_cast<uint8_t>(std::lround(s.b * a + d.b * (1.0f - a)));
            d.a = 255;
            dst.setPixel(x, y, d);
        }
    }
}

}  // namespace

Bitmap renderCutFrame(const Cut& cut, size_t frame, int width, int height, const RenderOptions& options) {
    Bitmap out(width, height);
    out.fill({255, 255, 255, 255});  // 紙(白)

    for (size_t ci = 0; ci < cut.celCount(); ++ci) {
        if (options.onlyCel >= 0 && static_cast<size_t>(options.onlyCel) != ci) continue;
        const Cel& cel = cut.cel(ci);
        if (!cel.visible()) continue;
        const int drawing = cel.exposure(frame);
        if (drawing < 0) continue;  // このコマにセルなし

        // タップ/ペグ移動: このコマでのセル位置(キー間は線形補間)
        const Vec2 position = cel.positionAt(frame);
        const int offsetX = static_cast<int>(std::lround(position.x));
        const int offsetY = static_cast<int>(std::lround(position.y));

        for (size_t li = 0; li < cel.layerCount(); ++li) {
            const Layer& layer = cel.layer(li);
            if (!layer.visible()) continue;
            if (layer.role() == LayerRole::ColorTrace && !options.includeColorTrace) continue;
            if (layer.role() == LayerRole::Correction && !options.includeCorrection) continue;
            if (static_cast<size_t>(drawing) >= layer.frameCount()) continue;

            const Bitmap& src = layer.frame(static_cast<size_t>(drawing)).bitmap();
            if (src.isEmpty()) continue;
            blendOver(out, src, offsetX, offsetY);
        }
    }
    return out;
}

}  // namespace core
