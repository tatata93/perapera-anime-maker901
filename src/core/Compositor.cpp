#include "Compositor.h"

#include <algorithm>
#include <cmath>

namespace core {

namespace {

// srcをdst(不透明前提)へsrc-over合成する。サイズが異なる場合は左上を合わせて重なる範囲のみ
void blendOver(Bitmap& dst, const Bitmap& src) {
    const int w = std::min(dst.width(), src.width());
    const int h = std::min(dst.height(), src.height());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const Bitmap::Pixel s = src.pixel(x, y);
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

        for (size_t li = 0; li < cel.layerCount(); ++li) {
            const Layer& layer = cel.layer(li);
            if (!layer.visible()) continue;
            if (layer.role() == LayerRole::ColorTrace && !options.includeColorTrace) continue;
            if (layer.role() == LayerRole::Correction && !options.includeCorrection) continue;
            if (static_cast<size_t>(drawing) >= layer.frameCount()) continue;

            const Bitmap& src = layer.frame(static_cast<size_t>(drawing)).bitmap();
            if (src.isEmpty()) continue;
            blendOver(out, src);
        }
    }
    return out;
}

}  // namespace core
