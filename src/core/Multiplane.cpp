#include "Multiplane.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace core {

namespace {

constexpr double kPi = 3.14159265358979323846;

// RGBAを[0,1]のfloatとして保持する作業用ピクセル(straight-alpha)
struct FloatPixel {
    double r = 0.0, g = 0.0, b = 0.0, a = 0.0;
};

// アートワーク上の連続座標(u,v)をバイリニア補間でサンプルする。範囲外は透明(0,0,0,0)。
// Compositor.cppのapplyCameraFrameと同じ流儀(タップごとに範囲外判定、ピクセル中心基準)
FloatPixel sampleArtworkBilinear(const Bitmap& artwork, double u, double v) {
    const auto tap = [&](int x, int y) -> FloatPixel {
        if (x < 0 || y < 0 || x >= artwork.width() || y >= artwork.height()) return {0.0, 0.0, 0.0, 0.0};
        const Bitmap::Pixel p = artwork.pixel(x, y);
        return {p.r / 255.0, p.g / 255.0, p.b / 255.0, p.a / 255.0};
    };

    const double su = u - 0.5;
    const double sv = v - 0.5;
    const int x0 = static_cast<int>(std::floor(su));
    const int y0 = static_cast<int>(std::floor(sv));
    const double fx = su - x0;
    const double fy = sv - y0;

    const FloatPixel p00 = tap(x0, y0);
    const FloatPixel p10 = tap(x0 + 1, y0);
    const FloatPixel p01 = tap(x0, y0 + 1);
    const FloatPixel p11 = tap(x0 + 1, y0 + 1);

    const auto lerp = [&](double c00, double c10, double c01, double c11) {
        const double top = c00 + (c10 - c00) * fx;
        const double bottom = c01 + (c11 - c01) * fx;
        return top + (bottom - top) * fy;
    };

    FloatPixel out;
    out.r = lerp(p00.r, p10.r, p01.r, p11.r);
    out.g = lerp(p00.g, p10.g, p01.g, p11.g);
    out.b = lerp(p00.b, p10.b, p01.b, p11.b);
    out.a = lerp(p00.a, p10.a, p01.a, p11.a);
    return out;
}

// 平面の距離Dにおけるワールド交点(xy)でアートワークをサンプルする。平面外(または未設定)は透明
FloatPixel samplePlaneAt(const MultiplanePlane& plane, double wx, double wy) {
    if (plane.artwork == nullptr || plane.artwork->isEmpty()) return {0.0, 0.0, 0.0, 0.0};
    const double widthMm = plane.widthMm;
    if (widthMm <= 0.0) return {0.0, 0.0, 0.0, 0.0};
    const double heightMm = widthMm * plane.artwork->height() / plane.artwork->width();

    const double u = (wx - plane.offsetXMm + widthMm / 2.0) / widthMm * plane.artwork->width();
    const double v = (wy - plane.offsetYMm + heightMm / 2.0) / heightMm * plane.artwork->height();
    return sampleArtworkBilinear(*plane.artwork, u, v);
}

}  // namespace

Bitmap renderMultiplane(const std::vector<MultiplanePlane>& planes, const MultiplaneCamera& camera, int width,
                         int height, int samplesPerPixel, uint32_t seed) {
    Bitmap out(width, height);
    if (width <= 0 || height <= 0) return out;

    // 手前(距離が近い)→奥の順にソートしてfront-to-back合成する
    std::vector<const MultiplanePlane*> sorted;
    sorted.reserve(planes.size());
    for (const MultiplanePlane& p : planes) sorted.push_back(&p);
    std::sort(sorted.begin(), sorted.end(),
              [](const MultiplanePlane* a, const MultiplanePlane* b) { return a->distanceMm < b->distanceMm; });

    const double sensorHeightMm = camera.sensorWidthMm * height / width;
    const bool pinhole = camera.apertureFStop <= 0.0;
    const int samples = std::max(1, samplesPerPixel);

    for (int py = 0; py < height; ++py) {
        for (int px = 0; px < width; ++px) {
            std::minstd_rand rng(seed + static_cast<uint32_t>(py) * static_cast<uint32_t>(width) +
                                  static_cast<uint32_t>(px));
            std::uniform_real_distribution<double> dist01(0.0, 1.0);

            double sumR = 0.0, sumG = 0.0, sumB = 0.0;

            for (int s = 0; s < samples; ++s) {
                double jx = 0.5, jy = 0.5;
                if (samples > 1) {
                    jx = dist01(rng);
                    jy = dist01(rng);
                }
                const double sx = ((px + jx) / width - 0.5) * camera.sensorWidthMm;
                const double sy = ((py + jy) / height - 0.5) * sensorHeightMm;

                // このサンプルのレンズサンプル点(ピンホールでは原点固定)とフォーカス面上の共役点
                double lx = 0.0, ly = 0.0;
                double focusX = 0.0, focusY = 0.0;  // フォーカス面上の共役点P(xy)
                const double focusDistanceMm = camera.focusDistanceMm;
                if (!pinhole) {
                    focusX = sx * focusDistanceMm / camera.focalLengthMm;
                    focusY = sy * focusDistanceMm / camera.focalLengthMm;
                    const double radius = (camera.focalLengthMm / camera.apertureFStop) / 2.0;
                    const double u1 = dist01(rng);
                    const double u2 = dist01(rng);
                    const double r = radius * std::sqrt(u1);
                    const double theta = 2.0 * kPi * u2;
                    lx = r * std::cos(theta);
                    ly = r * std::sin(theta);
                }

                // 平面を手前から奥へ順に合成(front-to-back)
                double accumR = 0.0, accumG = 0.0, accumB = 0.0, accumA = 0.0;
                for (const MultiplanePlane* plane : sorted) {
                    const double d = plane->distanceMm;
                    double wx, wy;
                    if (pinhole) {
                        wx = sx * d / camera.focalLengthMm;
                        wy = sy * d / camera.focalLengthMm;
                    } else {
                        const double t = d / focusDistanceMm;
                        wx = lx + (focusX - lx) * t;
                        wy = ly + (focusY - ly) * t;
                    }
                    const FloatPixel sample = samplePlaneAt(*plane, wx, wy);
                    if (sample.a <= 0.0) continue;
                    const double remain = 1.0 - accumA;
                    accumR += remain * sample.a * sample.r;
                    accumG += remain * sample.a * sample.g;
                    accumB += remain * sample.a * sample.b;
                    accumA += remain * sample.a;
                }

                // 最後に白(紙)を敷いて不透明化する
                const double remain = 1.0 - accumA;
                const double finalR = accumR + remain * 1.0;
                const double finalG = accumG + remain * 1.0;
                const double finalB = accumB + remain * 1.0;

                sumR += finalR;
                sumG += finalG;
                sumB += finalB;
            }

            const auto quantize = [&](double sum) {
                const double avg = sum / samples;
                return static_cast<uint8_t>(std::lround(std::clamp(avg * 255.0, 0.0, 255.0)));
            };
            out.setPixel(px, py, {quantize(sumR), quantize(sumG), quantize(sumB), 255});
        }
    }

    return out;
}

}  // namespace core
