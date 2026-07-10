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

// カメラフレーム(画面に写る範囲)でsrcをクロップ+バイリニア補間でリサンプルする。
// 出力は同じwidth×height。切り出し矩形はキャンバス外にはみ出すことがあり、
// その場合は紙(白)として扱う
Bitmap applyCameraFrame(const Bitmap& src, const CameraFrameState& cam, int width, int height) {
    const double cropW = std::max(1.0, width * cam.scale);
    const double cropH = std::max(1.0, height * cam.scale);
    const double cropX0 = cam.center.x - cropW * 0.5;
    const double cropY0 = cam.center.y - cropH * 0.5;

    // キャンバス外を参照する場合は白(紙)として扱う
    const auto samplePaper = [&](int x, int y) -> Bitmap::Pixel {
        if (x < 0 || y < 0 || x >= src.width() || y >= src.height()) return {255, 255, 255, 255};
        return src.pixel(x, y);
    };
    const auto lerpChannel = [](double c00, double c10, double c01, double c11, double fx, double fy) {
        const double top = c00 + (c10 - c00) * fx;
        const double bottom = c01 + (c11 - c01) * fx;
        return static_cast<uint8_t>(std::lround(std::clamp(top + (bottom - top) * fy, 0.0, 255.0)));
    };

    Bitmap out(width, height);
    for (int oy = 0; oy < height; ++oy) {
        for (int ox = 0; ox < width; ++ox) {
            // 出力ピクセル中心を切り出し矩形→元画像の座標へ写像する
            const double u = (ox + 0.5) / width;
            const double v = (oy + 0.5) / height;
            const double sx = cropX0 + u * cropW - 0.5;
            const double sy = cropY0 + v * cropH - 0.5;

            const int x0 = static_cast<int>(std::floor(sx));
            const int y0 = static_cast<int>(std::floor(sy));
            const double fx = sx - x0;
            const double fy = sy - y0;

            const Bitmap::Pixel p00 = samplePaper(x0, y0);
            const Bitmap::Pixel p10 = samplePaper(x0 + 1, y0);
            const Bitmap::Pixel p01 = samplePaper(x0, y0 + 1);
            const Bitmap::Pixel p11 = samplePaper(x0 + 1, y0 + 1);

            Bitmap::Pixel result;
            result.r = lerpChannel(p00.r, p10.r, p01.r, p11.r, fx, fy);
            result.g = lerpChannel(p00.g, p10.g, p01.g, p11.g, fx, fy);
            result.b = lerpChannel(p00.b, p10.b, p01.b, p11.b, fx, fy);
            result.a = lerpChannel(p00.a, p10.a, p01.a, p11.a, fx, fy);
            out.setPixel(ox, oy, result);
        }
    }
    return out;
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

    // カメラフレーム(画面に写る範囲)が指定されていればクロップ+リサンプルする。
    // キーが無い場合は完全に既存動作のまま(バイト単位で同一)
    if (const auto cam = cut.cameraFrameAt(frame)) {
        out = applyCameraFrame(out, *cam, width, height);
    }
    return out;
}

}  // namespace core
