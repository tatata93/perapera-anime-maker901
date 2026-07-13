#include "Multiplane.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "BoxBlur.h"
#include "Parallel.h"

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

// レンダリング用に前計算した平面情報(サンプルごとの再計算を避ける)
struct PlaneContext {
    const Bitmap* artwork = nullptr;
    double halfWidthMm = 0.0;   // 物理幅/2
    double halfHeightMm = 0.0;  // 物理高さ/2
    double pxPerMmX = 0.0;      // mm→アートワークpx変換
    double pxPerMmY = 0.0;
    double offsetXMm = 0.0;
    double offsetYMm = 0.0;
    double distanceMm = 0.0;
    double pinholeScale = 0.0;  // ピンホール時: ワールド座標 = センサー座標 * (D / focal)
    double lensT = 0.0;         // 薄レンズ時: 交点 = L + (P - L) * (D / F)
};

// 平面のワールド交点(wx, wy)でアートワークをサンプルする
inline FloatPixel samplePlane(const PlaneContext& ctx, double wx, double wy) {
    const double u = (wx - ctx.offsetXMm + ctx.halfWidthMm) * ctx.pxPerMmX;
    const double v = (wy - ctx.offsetYMm + ctx.halfHeightMm) * ctx.pxPerMmY;
    return sampleArtworkBilinear(*ctx.artwork, u, v);
}

// 光源マスクの出力ピクセル(ox, oy)におけるアルファ(0〜1)を返す。マスクが出力と同サイズなら
// そのまま参照し、サイズが異なる場合(呼び出し側の解像度計算が丸め誤差でズレた場合の保険)は
// バイリニアで(outW/outH比)スケールしてサンプルする
double sampleMaskAlpha01(const Bitmap& mask, int ox, int oy, int outW, int outH) {
    if (mask.width() == outW && mask.height() == outH) {
        return mask.pixel(ox, oy).a / 255.0;
    }
    const double mx = (ox + 0.5) * mask.width() / outW - 0.5;
    const double my = (oy + 0.5) * mask.height() / outH - 0.5;
    const int x0 = static_cast<int>(std::floor(mx));
    const int y0 = static_cast<int>(std::floor(my));
    const double fx = mx - x0;
    const double fy = my - y0;
    const auto tap = [&](int x, int y) -> double {
        x = std::clamp(x, 0, mask.width() - 1);
        y = std::clamp(y, 0, mask.height() - 1);
        return mask.pixel(x, y).a / 255.0;
    };
    const double top = tap(x0, y0) + (tap(x0 + 1, y0) - tap(x0, y0)) * fx;
    const double bottom = tap(x0, y0 + 1) + (tap(x0 + 1, y0 + 1) - tap(x0, y0 + 1)) * fx;
    return top + (bottom - top) * fy;
}

}  // namespace

Bitmap renderMultiplane(const std::vector<MultiplanePlane>& planes, const MultiplaneCamera& camera, int width,
                         int height, int samplesPerPixel, uint32_t seed,
                         const std::vector<MultiplaneBacklight>* backlights) {
    Bitmap out(width, height);
    if (width <= 0 || height <= 0) return out;

    // 手前(距離が近い)→奥の順にソートしてfront-to-back合成する。
    // サンプルごとに再計算しない量(物理幅/高さ、mm→px変換、投影係数)を前計算しておく
    std::vector<const MultiplanePlane*> sorted;
    sorted.reserve(planes.size());
    for (const MultiplanePlane& p : planes) sorted.push_back(&p);
    std::sort(sorted.begin(), sorted.end(),
              [](const MultiplanePlane* a, const MultiplanePlane* b) { return a->distanceMm < b->distanceMm; });

    const double focusDistanceMm = camera.focusDistanceMm;
    std::vector<PlaneContext> contexts;
    contexts.reserve(sorted.size());
    for (const MultiplanePlane* p : sorted) {
        if (p->artwork == nullptr || p->artwork->isEmpty() || p->widthMm <= 0.0) continue;
        PlaneContext ctx;
        ctx.artwork = p->artwork;
        const double heightMm = p->widthMm * p->artwork->height() / p->artwork->width();
        ctx.halfWidthMm = p->widthMm / 2.0;
        ctx.halfHeightMm = heightMm / 2.0;
        ctx.pxPerMmX = p->artwork->width() / p->widthMm;
        ctx.pxPerMmY = p->artwork->height() / heightMm;
        ctx.offsetXMm = p->offsetXMm;
        ctx.offsetYMm = p->offsetYMm;
        ctx.distanceMm = p->distanceMm;
        ctx.pinholeScale = p->distanceMm / camera.focalLengthMm;
        ctx.lensT = focusDistanceMm > 0.0 ? p->distanceMm / focusDistanceMm : 1.0;
        contexts.push_back(ctx);
    }

    const double sensorHeightMm = camera.sensorWidthMm * height / width;
    const bool pinhole = camera.apertureFStop <= 0.0;
    const int samples = std::max(1, samplesPerPixel);

    const bool useBacklight =
        backlights != nullptr &&
        std::any_of(backlights->begin(), backlights->end(), [](const MultiplaneBacklight& bl) { return bl.enabled; });
    // paintTransmittance(τ)は光路を共有するため灯ごとに変えられない(同一の光線に沿って
    // ∏T=per-channel透過率の積を1回だけ計算するため)。先頭の有効灯のτを全体で使う
    double blTau = 0.0;
    if (useBacklight) {
        for (const MultiplaneBacklight& bl : *backlights) {
            if (bl.enabled) {
                blTau = std::clamp(bl.paintTransmittance, 0.0, 1.0);
                break;
            }
        }
    }

    // 透過率(∏T、per-channel)の蓄積バッファ。全灯で共有する生の透過率(intensity/colorは未適用)。
    // 反射光と分けて持ち、後段で灯ごとにintensity×color×マスクを掛けてハレーション(ブルーム)を
    // かけてから加算する(実際の透過光撮影の二重露光に相当)
    const size_t pixelCount = static_cast<size_t>(width) * height;
    std::vector<float> transR, transG, transB;
    if (useBacklight) {
        transR.assign(pixelCount, 0.0f);
        transG.assign(pixelCount, 0.0f);
        transB.assign(pixelCount, 0.0f);
    }

    // 反射光(通常露光)の平均値もfloatで持ち、最後に透過光と合算してから量子化する
    std::vector<float> reflR(pixelCount), reflG(pixelCount), reflB(pixelCount);

    parallelForRows(0, height, [&](int rowBegin, int rowEnd) {
        for (int py = rowBegin; py < rowEnd; ++py) {
            for (int px = 0; px < width; ++px) {
                // ピクセルごとに決定論的なシード(並列化しても結果はシリアルと同一)
                std::minstd_rand rng(seed + static_cast<uint32_t>(py) * static_cast<uint32_t>(width) +
                                      static_cast<uint32_t>(px));
                std::uniform_real_distribution<double> dist01(0.0, 1.0);

                double sumR = 0.0, sumG = 0.0, sumB = 0.0;
                double sumTr = 0.0, sumTg = 0.0, sumTb = 0.0;  // 透過光成分

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
                    double focusX = 0.0, focusY = 0.0;
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

                    // 平面を手前から奥へ順に合成(front-to-back)。同じ光線に沿って
                    // 透過光のper-channel透過率の積も同時に蓄積する(同一の光路=物理的に正しい)
                    double accumR = 0.0, accumG = 0.0, accumB = 0.0, accumA = 0.0;
                    double trR = 1.0, trG = 1.0, trB = 1.0;  // 透過率の積
                    for (const PlaneContext& ctx : contexts) {
                        double wx, wy;
                        if (pinhole) {
                            wx = sx * ctx.pinholeScale;
                            wy = sy * ctx.pinholeScale;
                        } else {
                            wx = lx + (focusX - lx) * ctx.lensT;
                            wy = ly + (focusY - ly) * ctx.lensT;
                        }
                        const FloatPixel sample = samplePlane(ctx, wx, wy);
                        if (sample.a <= 0.0) continue;
                        const double remain = 1.0 - accumA;
                        accumR += remain * sample.a * sample.r;
                        accumG += remain * sample.a * sample.g;
                        accumB += remain * sample.a * sample.b;
                        accumA += remain * sample.a;
                        if (useBacklight) {
                            // 塗られた部分は色フィルターとして透過する: T = (1-a) + a*c*τ
                            trR *= (1.0 - sample.a) + sample.a * sample.r * blTau;
                            trG *= (1.0 - sample.a) + sample.a * sample.g * blTau;
                            trB *= (1.0 - sample.a) + sample.a * sample.b * blTau;
                        }
                    }

                    // 最後に白(紙)を敷いて不透明化する(通常露光=トップライトの反射)
                    const double remain = 1.0 - accumA;
                    sumR += accumR + remain;
                    sumG += accumG + remain;
                    sumB += accumB + remain;

                    if (useBacklight) {
                        // ここではintensity/colorを掛けない(灯ごとに異なるため、後段で灯ごとに適用する)。
                        // ∏Tの生値だけを蓄積する
                        sumTr += trR;
                        sumTg += trG;
                        sumTb += trB;
                    }
                }

                const size_t idx = static_cast<size_t>(py) * width + px;
                reflR[idx] = static_cast<float>(sumR / samples);
                reflG[idx] = static_cast<float>(sumG / samples);
                reflB[idx] = static_cast<float>(sumB / samples);
                if (useBacklight) {
                    transR[idx] = static_cast<float>(sumTr / samples);
                    transG[idx] = static_cast<float>(sumTg / samples);
                    transB[idx] = static_cast<float>(sumTb / samples);
                }
            }
        }
    });

    // 灯ごとに: intensity×color×∏T(全灯共有の生の透過率)→光源マスク→ブルームをかけて、
    // 全灯ぶんを合算する(灯ごとに異なる色/強度/マスク/にじみを持てるようにするため、
    // ここから先だけ灯ごとにループする。∏Tの計算自体は上のサンプルループで1回だけ行い共有する)
    std::vector<float> totalTransR, totalTransG, totalTransB;
    if (useBacklight) {
        totalTransR.assign(pixelCount, 0.0f);
        totalTransG.assign(pixelCount, 0.0f);
        totalTransB.assign(pixelCount, 0.0f);

        for (const MultiplaneBacklight& bl : *backlights) {
            if (!bl.enabled) continue;

            // この灯のintensity×colorを∏Tへ掛け、光源マスクがあればアルファで絞る
            std::vector<float> lightR(pixelCount), lightG(pixelCount), lightB(pixelCount);
            parallelForRows(0, height, [&](int rowBegin, int rowEnd) {
                for (int py = rowBegin; py < rowEnd; ++py) {
                    for (int px = 0; px < width; ++px) {
                        const size_t idx = static_cast<size_t>(py) * width + px;
                        double r = bl.intensity * bl.colorR * transR[idx];
                        double g = bl.intensity * bl.colorG * transG[idx];
                        double b = bl.intensity * bl.colorB * transB[idx];
                        if (!bl.mask.isEmpty()) {
                            const double a = sampleMaskAlpha01(bl.mask, px, py, width, height);
                            r *= a;
                            g *= a;
                            b *= a;
                        }
                        lightR[idx] = static_cast<float>(r);
                        lightG[idx] = static_cast<float>(g);
                        lightB[idx] = static_cast<float>(b);
                    }
                }
            });

            // ハレーション(ブルーム): この灯の(マスク後の)光成分だけをぼかして加算する
            // (フィルムの光のにじみの近似。絞った光がにじむ=物理的に正しい)
            const bool hasBloom = bl.bloomStrength > 0.0 && bl.bloomRadiusPx >= 1.0;
            std::vector<float> bloomR, bloomG, bloomB;
            if (hasBloom) {
                const int radius = std::max(1, static_cast<int>(std::lround(bl.bloomRadiusPx)));
                bloomR = lightR;
                bloomG = lightG;
                bloomB = lightB;
                tripleBoxBlur(bloomR, width, height, radius);
                tripleBoxBlur(bloomG, width, height, radius);
                tripleBoxBlur(bloomB, width, height, radius);
            }

            for (size_t idx = 0; idx < pixelCount; ++idx) {
                totalTransR[idx] += lightR[idx];
                totalTransG[idx] += lightG[idx];
                totalTransB[idx] += lightB[idx];
                if (hasBloom) {
                    totalTransR[idx] += static_cast<float>(bl.bloomStrength * bloomR[idx]);
                    totalTransG[idx] += static_cast<float>(bl.bloomStrength * bloomG[idx]);
                    totalTransB[idx] += static_cast<float>(bl.bloomStrength * bloomB[idx]);
                }
            }
        }
    }

    // 反射光+透過光(全灯合算)を合算し、量子化する(二重露光の加算)
    parallelForRows(0, height, [&](int rowBegin, int rowEnd) {
        for (int py = rowBegin; py < rowEnd; ++py) {
            for (int px = 0; px < width; ++px) {
                const size_t idx = static_cast<size_t>(py) * width + px;
                double r = reflR[idx];
                double g = reflG[idx];
                double b = reflB[idx];
                if (useBacklight) {
                    r += totalTransR[idx];
                    g += totalTransG[idx];
                    b += totalTransB[idx];
                }
                const auto quantize = [](double v) {
                    return static_cast<uint8_t>(std::lround(std::clamp(v * 255.0, 0.0, 255.0)));
                };
                out.setPixel(px, py, {quantize(r), quantize(g), quantize(b), 255});
            }
        }
    });

    return out;
}

}  // namespace core
