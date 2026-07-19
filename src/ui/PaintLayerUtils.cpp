#include "PaintLayerUtils.h"

#include <algorithm>
#include <cmath>

namespace perapera::ui {
namespace {

core::Bitmap transparentBitmap(int width, int height) {
    core::Bitmap bitmap(width, height);
    bitmap.fill({0, 0, 0, 0});
    return bitmap;
}

core::PaintLayer makeLayer(const std::string& name, core::LayerRole role, int width, int height) {
    core::PaintLayer layer;
    layer.name = name;
    layer.role = role;
    layer.bitmap = transparentBitmap(width, height);
    return layer;
}

core::Bitmap resizeBitmapCenteredCopy(const core::Bitmap& bitmap, int width, int height) {
    core::Bitmap resized = transparentBitmap(width, height);
    if (bitmap.isEmpty()) return resized;

    const int copyW = std::min(bitmap.width(), width);
    const int copyH = std::min(bitmap.height(), height);
    const int srcX = std::max(0, (bitmap.width() - width) / 2);
    const int srcY = std::max(0, (bitmap.height() - height) / 2);
    const int dstX = std::max(0, (width - bitmap.width()) / 2);
    const int dstY = std::max(0, (height - bitmap.height()) / 2);
    for (int y = 0; y < copyH; ++y) {
        for (int x = 0; x < copyW; ++x) {
            resized.setPixel(dstX + x, dstY + y, bitmap.pixel(srcX + x, srcY + y));
        }
    }
    return resized;
}

void blendOver(core::Bitmap& dst, const core::Bitmap& src, double opacity) {
    if (dst.isEmpty() || src.isEmpty() || opacity <= 0.0) return;
    opacity = std::clamp(opacity, 0.0, 1.0);
    const int w = std::min(dst.width(), src.width());
    const int h = std::min(dst.height(), src.height());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const core::Bitmap::Pixel s = src.pixel(x, y);
            const float srcA = static_cast<float>((s.a / 255.0) * opacity);
            if (srcA <= 0.0f) continue;

            core::Bitmap::Pixel d = dst.pixel(x, y);
            if (s.a == 255 && opacity >= 0.999 && d.a == 0) {
                dst.setPixel(x, y, s);
                continue;
            }

            const float dstA = d.a / 255.0f;
            const float outA = srcA + dstA * (1.0f - srcA);
            if (outA > 0.0f) {
                const auto blend = [srcA, dstA, outA](uint8_t srcChannel, uint8_t dstChannel) {
                    return static_cast<uint8_t>(
                        std::lround((srcChannel * srcA + dstChannel * dstA * (1.0f - srcA)) / outA));
                };
                d.r = blend(s.r, d.r);
                d.g = blend(s.g, d.g);
                d.b = blend(s.b, d.b);
            }
            d.a = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(outA * 255.0f)), 0, 255));
            dst.setPixel(x, y, d);
        }
    }
}

}  // namespace

QSize paintLayerCanvasSize(const std::vector<core::PaintLayer>& layers, const core::Bitmap& legacy, int defaultWidth,
                           int defaultHeight) {
    for (const core::PaintLayer& layer : layers) {
        if (!layer.bitmap.isEmpty()) return QSize(layer.bitmap.width(), layer.bitmap.height());
    }
    if (!legacy.isEmpty()) return QSize(legacy.width(), legacy.height());
    return QSize(defaultWidth, defaultHeight);
}

void ensurePaintLayers(std::vector<core::PaintLayer>& layers, size_t& activeLayer, const core::Bitmap& legacy,
                       int defaultWidth, int defaultHeight, bool includePaintAndTrace) {
    const QSize size = paintLayerCanvasSize(layers, legacy, defaultWidth, defaultHeight);
    const int width = std::max(1, size.width());
    const int height = std::max(1, size.height());

    if (layers.empty()) {
        if (includePaintAndTrace) layers.push_back(makeLayer("塗り", core::LayerRole::Normal, width, height));

        core::PaintLayer lineLayer = makeLayer("線画", core::LayerRole::Normal, width, height);
        if (!legacy.isEmpty()) lineLayer.bitmap = legacy;
        layers.push_back(std::move(lineLayer));

        if (includePaintAndTrace) layers.push_back(makeLayer("塗分け線", core::LayerRole::ColorTrace, width, height));
        activeLayer = includePaintAndTrace ? 1 : 0;
    }

    for (core::PaintLayer& layer : layers) {
        if (layer.name.empty()) layer.name = "レイヤー";
        if (layer.bitmap.isEmpty()) {
            layer.bitmap = transparentBitmap(width, height);
        } else if (layer.bitmap.width() != width || layer.bitmap.height() != height) {
            layer.bitmap = resizeBitmapCenteredCopy(layer.bitmap, width, height);
        }
        layer.opacity = std::clamp(layer.opacity, 0.0, 1.0);
    }
    if (layers.empty()) return;
    activeLayer = std::min(activeLayer, layers.size() - 1);
}

void resizePaintLayersCentered(std::vector<core::PaintLayer>& layers, int width, int height) {
    width = std::max(1, width);
    height = std::max(1, height);
    for (core::PaintLayer& layer : layers) layer.bitmap = resizeBitmapCenteredCopy(layer.bitmap, width, height);
}

core::Bitmap compositePaintLayers(const std::vector<core::PaintLayer>& layers, int width, int height) {
    core::Bitmap out = transparentBitmap(std::max(1, width), std::max(1, height));
    for (const core::PaintLayer& layer : layers) {
        if (!layer.visible) continue;
        blendOver(out, layer.bitmap, layer.opacity);
    }
    return out;
}

QImage bitmapToImageCopy(const core::Bitmap& bitmap) {
    if (bitmap.isEmpty()) return QImage();
    return QImage(bitmap.data(), bitmap.width(), bitmap.height(), QImage::Format_RGBA8888).copy();
}

}  // namespace perapera::ui
