#include "EffectProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

#include "BoxBlur.h"   // 分離型箱ぼかし(行/列並列)。Multiplaneの透過光ハレーションと共用
#include "Parallel.h"  // 行並列ユーティリティ(結果はシリアル実行とバイト同一)

namespace core {

namespace {

// パラメータをキーで取得する(無ければfallback)
double param(const Effect& effect, const std::string& key, double fallback) {
    const auto it = effect.params.find(key);
    return it != effect.params.end() ? it->second : fallback;
}

// (a, b, c, d)から無相関な擬似乱数[0,1)を作る決定論的ハッシュ(splitmix64系のアバランシェ)。
// std::minstd_rand等のLCGは初期シードに対して初回出力が線形になりやすく、
// シードを frame*A + x*B + y*C のように組み立てても「フレーム間でパターンが一様にずれるだけ」で
// 真に無相関なノイズにならない。ここでは入力をXOR+乗算で十分に混ぜてから上位ビットを取り出すことで、
// 入力が少しでも変わればビット単位で無関係な出力になるようにする(決定論的=同じ入力なら常に同じ結果)
inline double hashNoise01(uint64_t a, uint64_t b, uint64_t c, uint64_t d = 0) {
    uint64_t h = a * 0x9E3779B97F4A7C15ull ^ b * 0xBF58476D1CE4E5B9ull ^ c * 0x94D049BB133111EBull ^
                 d * 0xD6E8FEB86659FD93ull;
    h ^= h >> 30;
    h *= 0xBF58476D1CE4E5B9ull;
    h ^= h >> 27;
    h *= 0x94D049BB133111EBull;
    h ^= h >> 31;
    return (h >> 11) * (1.0 / 9007199254740992.0);  // 上位53bit→[0,1)
}

// ブラー: プリマルチプライドアルファ空間で箱ぼかしを3回かけてから戻す。
// 透明な部分は色に寄与しないため、不透明な縁が透明側へ滲んでも色が黒ずまない(アルファ加重平均)
void applyBlur(Bitmap& image, const Effect& effect) {
    const double radius = param(effect, "radius", 4.0);
    const int w = image.width();
    const int h = image.height();
    if (w <= 0 || h <= 0 || radius <= 0.0) return;
    const int boxRadius = std::max(1, static_cast<int>(std::lround(radius)));

    const size_t count = static_cast<size_t>(w) * h;
    std::vector<float> rp(count), gp(count), bp(count), ap(count);
    parallelForRows(0, h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            for (int x = 0; x < w; ++x) {
                const Bitmap::Pixel p = image.pixel(x, y);
                const size_t idx = static_cast<size_t>(y) * w + x;
                const float alphaFrac = p.a / 255.0f;
                rp[idx] = p.r * alphaFrac;
                gp[idx] = p.g * alphaFrac;
                bp[idx] = p.b * alphaFrac;
                ap[idx] = static_cast<float>(p.a);
            }
        }
    });

    tripleBoxBlur(rp, w, h, boxRadius);
    tripleBoxBlur(gp, w, h, boxRadius);
    tripleBoxBlur(bp, w, h, boxRadius);
    tripleBoxBlur(ap, w, h, boxRadius);

    parallelForRows(0, h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            for (int x = 0; x < w; ++x) {
                const size_t idx = static_cast<size_t>(y) * w + x;
                const float aOut = std::clamp(ap[idx], 0.0f, 255.0f);
                Bitmap::Pixel result{0, 0, 0, static_cast<uint8_t>(std::lround(aOut))};
                if (aOut > 0.5f) {
                    const float alphaFrac = aOut / 255.0f;
                    result.r = static_cast<uint8_t>(std::lround(std::clamp(rp[idx] / alphaFrac, 0.0f, 255.0f)));
                    result.g = static_cast<uint8_t>(std::lround(std::clamp(gp[idx] / alphaFrac, 0.0f, 255.0f)));
                    result.b = static_cast<uint8_t>(std::lround(std::clamp(bp[idx] / alphaFrac, 0.0f, 255.0f)));
                }
                image.setPixel(x, y, result);
            }
        }
    });
}

// グロー: 輝度がthreshold以上の部分をプリマルチプライド抽出→ブラー→元画像へ加算合成(strength倍)。
// 透明な領域にも光が滲み出す(アルファも加算される)ため、ディフュージョン系の柔らかい光になる
void applyGlow(Bitmap& image, const Effect& effect) {
    const double threshold = param(effect, "threshold", 200.0);
    const double radius = param(effect, "radius", 8.0);
    const double strength = param(effect, "strength", 0.6);
    const int w = image.width();
    const int h = image.height();
    if (w <= 0 || h <= 0) return;
    const int boxRadius = std::max(1, static_cast<int>(std::lround(radius)));

    const size_t count = static_cast<size_t>(w) * h;
    std::vector<float> exR(count, 0.0f), exG(count, 0.0f), exB(count, 0.0f), exA(count, 0.0f);
    parallelForRows(0, h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            for (int x = 0; x < w; ++x) {
                const Bitmap::Pixel p = image.pixel(x, y);
                if (p.a == 0) continue;
                const double luma = 0.299 * p.r + 0.587 * p.g + 0.114 * p.b;
                if (luma < threshold) continue;
                const size_t idx = static_cast<size_t>(y) * w + x;
                const float alphaFrac = p.a / 255.0f;
                exR[idx] = p.r * alphaFrac;
                exG[idx] = p.g * alphaFrac;
                exB[idx] = p.b * alphaFrac;
                exA[idx] = static_cast<float>(p.a);
            }
        }
    });

    tripleBoxBlur(exR, w, h, boxRadius);
    tripleBoxBlur(exG, w, h, boxRadius);
    tripleBoxBlur(exB, w, h, boxRadius);
    tripleBoxBlur(exA, w, h, boxRadius);

    parallelForRows(0, h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            for (int x = 0; x < w; ++x) {
                const size_t idx = static_cast<size_t>(y) * w + x;
                const Bitmap::Pixel p = image.pixel(x, y);
                const float alphaFrac = p.a / 255.0f;
                const float rp = p.r * alphaFrac + static_cast<float>(strength) * exR[idx];
                const float gp = p.g * alphaFrac + static_cast<float>(strength) * exG[idx];
                const float bp = p.b * alphaFrac + static_cast<float>(strength) * exB[idx];
                const float aOut =
                    std::clamp(static_cast<float>(p.a) + static_cast<float>(strength) * exA[idx], 0.0f, 255.0f);

                Bitmap::Pixel result{0, 0, 0, static_cast<uint8_t>(std::lround(aOut))};
                if (aOut > 0.5f) {
                    const float outAlphaFrac = aOut / 255.0f;
                    result.r = static_cast<uint8_t>(std::lround(std::clamp(rp / outAlphaFrac, 0.0f, 255.0f)));
                    result.g = static_cast<uint8_t>(std::lround(std::clamp(gp / outAlphaFrac, 0.0f, 255.0f)));
                    result.b = static_cast<uint8_t>(std::lround(std::clamp(bp / outAlphaFrac, 0.0f, 255.0f)));
                }
                image.setPixel(x, y, result);
            }
        }
    });
}

// パラ(パラフィン): 画面上端からtop濃度→下端bottom濃度への線形グラデーションで色(r,g,b)を乗せる。
// 透明なピクセル(a==0)には何も乗せない(セル対象時に余白が汚れないように)
void applyPara(Bitmap& image, const Effect& effect) {
    const double top = std::clamp(param(effect, "top", 0.25), 0.0, 1.0);
    const double bottom = std::clamp(param(effect, "bottom", 0.0), 0.0, 1.0);
    const double pr = std::clamp(param(effect, "r", 0.0), 0.0, 255.0);
    const double pg = std::clamp(param(effect, "g", 0.0), 0.0, 255.0);
    const double pb = std::clamp(param(effect, "b", 0.0), 0.0, 255.0);
    const int w = image.width();
    const int h = image.height();
    if (w <= 0 || h <= 0) return;

    parallelForRows(0, h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const double t = h > 1 ? static_cast<double>(y) / (h - 1) : 0.0;
            const double density = top + (bottom - top) * t;
            if (density <= 0.0) continue;
            for (int x = 0; x < w; ++x) {
                Bitmap::Pixel p = image.pixel(x, y);
                if (p.a == 0) continue;  // 透明部分は変化なし
                p.r = static_cast<uint8_t>(std::lround(std::clamp(p.r + (pr - p.r) * density, 0.0, 255.0)));
                p.g = static_cast<uint8_t>(std::lround(std::clamp(p.g + (pg - p.g) * density, 0.0, 255.0)));
                p.b = static_cast<uint8_t>(std::lround(std::clamp(p.b + (pb - p.b) * density, 0.0, 255.0)));
                image.setPixel(x, y, p);
            }
        }
    });
}

// シェイク: コマ番号とseedからstd::minstd_randを決定論的に初期化し、X/Yオフセットを生成して
// 画面全体をずらす。はみ出しは白(紙)として扱う
void applyShake(Bitmap& image, const Effect& effect, size_t frame) {
    const double amplitudeX = param(effect, "amplitudeX", 8.0);
    const double amplitudeY = param(effect, "amplitudeY", 8.0);
    const double seed = param(effect, "seed", 1.0);
    const int w = image.width();
    const int h = image.height();
    if (w <= 0 || h <= 0) return;

    uint32_t seedValue = static_cast<uint32_t>(frame) * 2654435761u +
                          static_cast<uint32_t>(std::llround(seed * 97.0)) + 1u;
    if (seedValue == 0) seedValue = 1;  // minstd_randは0シードだと縮退する
    std::minstd_rand rng(seedValue);
    const uint32_t r1 = static_cast<uint32_t>(rng());
    const uint32_t r2 = static_cast<uint32_t>(rng());
    const double nx = (static_cast<double>(r1 % 2000001u) / 1000000.0) - 1.0;  // -1.0〜1.0
    const double ny = (static_cast<double>(r2 % 2000001u) / 1000000.0) - 1.0;
    const int offsetX = static_cast<int>(std::lround(nx * amplitudeX));
    const int offsetY = static_cast<int>(std::lround(ny * amplitudeY));
    if (offsetX == 0 && offsetY == 0) return;

    Bitmap shifted(w, h);
    shifted.fill({255, 255, 255, 255});  // はみ出しは白(紙)
    const int x0 = std::max(0, offsetX);
    const int y0 = std::max(0, offsetY);
    const int x1 = std::min(w, w + offsetX);
    const int y1 = std::min(h, h + offsetY);
    parallelForRows(y0, y1, [&](int rowBegin, int rowEnd) {
        for (int y = rowBegin; y < rowEnd; ++y) {
            for (int x = x0; x < x1; ++x) {
                shifted.setPixel(x, y, image.pixel(x - offsetX, y - offsetY));
            }
        }
    });
    image = std::move(shifted);
}

// バイリニアサンプリング(straight-alphaのまま線形補間する簡易版)。
// RadialBlur/ChromAbのような「近傍を少しずらしてサンプルする」系のエフェクトで使う。
// Blur/Glowほど厳密なアルファ加重合成は行わないが、ずれ幅が小さいため縁の色にじみは目立たない
Bitmap::Pixel sampleBilinear(const Bitmap& image, double x, double y) {
    const int w = image.width();
    const int h = image.height();
    x = std::clamp(x, 0.0, static_cast<double>(w - 1));
    y = std::clamp(y, 0.0, static_cast<double>(h - 1));
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = std::min(x0 + 1, w - 1);
    const int y1 = std::min(y0 + 1, h - 1);
    const double fx = x - x0;
    const double fy = y - y0;
    const Bitmap::Pixel p00 = image.pixel(x0, y0);
    const Bitmap::Pixel p10 = image.pixel(x1, y0);
    const Bitmap::Pixel p01 = image.pixel(x0, y1);
    const Bitmap::Pixel p11 = image.pixel(x1, y1);
    auto lerp2 = [&](double a00, double a10, double a01, double a11) {
        const double top = a00 + (a10 - a00) * fx;
        const double bottom = a01 + (a11 - a01) * fx;
        return top + (bottom - top) * fy;
    };
    const double r = lerp2(p00.r, p10.r, p01.r, p11.r);
    const double g = lerp2(p00.g, p10.g, p01.g, p11.g);
    const double b = lerp2(p00.b, p10.b, p01.b, p11.b);
    const double a = lerp2(p00.a, p10.a, p01.a, p11.a);
    return {static_cast<uint8_t>(std::lround(std::clamp(r, 0.0, 255.0))),
            static_cast<uint8_t>(std::lround(std::clamp(g, 0.0, 255.0))),
            static_cast<uint8_t>(std::lround(std::clamp(b, 0.0, 255.0))),
            static_cast<uint8_t>(std::lround(std::clamp(a, 0.0, 255.0)))};
}

// 色調補正: 明るさ加算→コントラスト(128中心)→彩度(輝度基準スケール)→色相回転
// (CSS/SVGのhue-rotateフィルタと同じ回転行列。輝度をおおむね保ったままRGBを回転させる)の順に適用する。
// 透明ピクセル(a==0)はスキップして余白を汚さない
void applyColorCorrect(Bitmap& image, const Effect& effect) {
    const double brightness = param(effect, "brightness", 0.0);
    const double contrast = param(effect, "contrast", 1.0);
    const double saturation = param(effect, "saturation", 1.0);
    const double hueDeg = param(effect, "hue", 0.0);
    const int w = image.width();
    const int h = image.height();
    if (w <= 0 || h <= 0) return;

    const double hueRad = hueDeg * 3.14159265358979323846 / 180.0;
    const double cosA = std::cos(hueRad);
    const double sinA = std::sin(hueRad);
    const double m00 = 0.213 + cosA * 0.787 - sinA * 0.213;
    const double m01 = 0.715 - cosA * 0.715 - sinA * 0.715;
    const double m02 = 0.072 - cosA * 0.072 + sinA * 0.928;
    const double m10 = 0.213 - cosA * 0.213 + sinA * 0.143;
    const double m11 = 0.715 + cosA * 0.285 + sinA * 0.140;
    const double m12 = 0.072 - cosA * 0.072 - sinA * 0.283;
    const double m20 = 0.213 - cosA * 0.213 - sinA * 0.787;
    const double m21 = 0.715 - cosA * 0.715 + sinA * 0.715;
    const double m22 = 0.072 + cosA * 0.928 + sinA * 0.072;

    parallelForRows(0, h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            for (int x = 0; x < w; ++x) {
                Bitmap::Pixel p = image.pixel(x, y);
                if (p.a == 0) continue;
                double r = std::clamp(p.r + brightness, 0.0, 255.0);
                double g = std::clamp(p.g + brightness, 0.0, 255.0);
                double b = std::clamp(p.b + brightness, 0.0, 255.0);

                r = std::clamp((r - 128.0) * contrast + 128.0, 0.0, 255.0);
                g = std::clamp((g - 128.0) * contrast + 128.0, 0.0, 255.0);
                b = std::clamp((b - 128.0) * contrast + 128.0, 0.0, 255.0);

                const double luma = 0.299 * r + 0.587 * g + 0.114 * b;
                r = std::clamp(luma + (r - luma) * saturation, 0.0, 255.0);
                g = std::clamp(luma + (g - luma) * saturation, 0.0, 255.0);
                b = std::clamp(luma + (b - luma) * saturation, 0.0, 255.0);

                const double nr = std::clamp(m00 * r + m01 * g + m02 * b, 0.0, 255.0);
                const double ng = std::clamp(m10 * r + m11 * g + m12 * b, 0.0, 255.0);
                const double nb = std::clamp(m20 * r + m21 * g + m22 * b, 0.0, 255.0);

                p.r = static_cast<uint8_t>(std::lround(nr));
                p.g = static_cast<uint8_t>(std::lround(ng));
                p.b = static_cast<uint8_t>(std::lround(nb));
                image.setPixel(x, y, p);
            }
        }
    });
}

// ディフュージョン: 画像全体をプリマルチプライドでブラーし、そのぼかしコピーを元画像へ
// screen合成(1-(1-a)(1-b))でstrengthブレンドする。しきい値が無い点がグローとの違いで、
// 明部だけでなく画像全体がふわっと柔らかくにじむ。アルファも同時にブレンドするため、
// 透明な縁の外側にもわずかに光が滲み出す(明部が周囲へ広がる)
void applyDiffusion(Bitmap& image, const Effect& effect) {
    const double radius = param(effect, "radius", 12.0);
    const double strength = std::clamp(param(effect, "strength", 0.5), 0.0, 1.0);
    const int w = image.width();
    const int h = image.height();
    if (w <= 0 || h <= 0 || radius <= 0.0 || strength <= 0.0) return;
    const int boxRadius = std::max(1, static_cast<int>(std::lround(radius)));

    const size_t count = static_cast<size_t>(w) * h;
    std::vector<float> rp(count), gp(count), bp(count), ap(count);
    parallelForRows(0, h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            for (int x = 0; x < w; ++x) {
                const Bitmap::Pixel p = image.pixel(x, y);
                const size_t idx = static_cast<size_t>(y) * w + x;
                const float alphaFrac = p.a / 255.0f;
                rp[idx] = p.r * alphaFrac;
                gp[idx] = p.g * alphaFrac;
                bp[idx] = p.b * alphaFrac;
                ap[idx] = static_cast<float>(p.a);
            }
        }
    });

    tripleBoxBlur(rp, w, h, boxRadius);
    tripleBoxBlur(gp, w, h, boxRadius);
    tripleBoxBlur(bp, w, h, boxRadius);
    tripleBoxBlur(ap, w, h, boxRadius);

    parallelForRows(0, h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            for (int x = 0; x < w; ++x) {
                const size_t idx = static_cast<size_t>(y) * w + x;
                const Bitmap::Pixel p = image.pixel(x, y);
                const float blurA = std::clamp(ap[idx], 0.0f, 255.0f);
                float blurR = 0.0f, blurG = 0.0f, blurB = 0.0f;
                if (blurA > 0.5f) {
                    const float f = blurA / 255.0f;
                    blurR = std::clamp(rp[idx] / f, 0.0f, 255.0f);
                    blurG = std::clamp(gp[idx] / f, 0.0f, 255.0f);
                    blurB = std::clamp(bp[idx] / f, 0.0f, 255.0f);
                }
                // screen合成: 1-(1-orig)(1-blur) を0〜1に正規化して計算する
                const float on = p.r / 255.0f, og = p.g / 255.0f, ob = p.b / 255.0f;
                const float bn = blurR / 255.0f, bg = blurG / 255.0f, bb = blurB / 255.0f;
                const float sr = 1.0f - (1.0f - on) * (1.0f - bn);
                const float sg = 1.0f - (1.0f - og) * (1.0f - bg);
                const float sb = 1.0f - (1.0f - ob) * (1.0f - bb);
                const float strengthF = static_cast<float>(strength);
                const float fr = std::clamp(p.r + strengthF * (sr * 255.0f - p.r), 0.0f, 255.0f);
                const float fg = std::clamp(p.g + strengthF * (sg * 255.0f - p.g), 0.0f, 255.0f);
                const float fb = std::clamp(p.b + strengthF * (sb * 255.0f - p.b), 0.0f, 255.0f);
                const float fa = std::clamp(p.a + strengthF * (blurA - p.a), 0.0f, 255.0f);

                Bitmap::Pixel result{static_cast<uint8_t>(std::lround(fr)), static_cast<uint8_t>(std::lround(fg)),
                                      static_cast<uint8_t>(std::lround(fb)), static_cast<uint8_t>(std::lround(fa))};
                image.setPixel(x, y, result);
            }
        }
    });
}

// 放射ブラー(ズームブラー): 各ピクセルを中心へ向けてtaps回だけ縮小サンプリング(バイリニア)して
// 平均する。中心ピクセルは(x,y)==centerのためオフセットが常に0になり、どのtapでも中心そのものを
// サンプルするので不変。周辺ほど中心からの距離が長く、taps間のスケール差が大きいのでよく滲む
void applyRadialBlur(Bitmap& image, const Effect& effect) {
    const double centerX = param(effect, "centerX", 0.5);
    const double centerY = param(effect, "centerY", 0.5);
    const double amount = std::clamp(param(effect, "amount", 0.02), 0.0, 0.2);
    const int taps = std::clamp(static_cast<int>(std::lround(param(effect, "taps", 8.0))), 2, 32);
    const int w = image.width();
    const int h = image.height();
    if (w <= 0 || h <= 0 || amount <= 0.0) return;

    const Bitmap source = image;  // サンプリング元。書き込み先(image)と分離して読み取り競合を避ける
    const double cx = centerX * (w - 1);
    const double cy = centerY * (h - 1);

    parallelForRows(0, h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            for (int x = 0; x < w; ++x) {
                double sumR = 0.0, sumG = 0.0, sumB = 0.0, sumA = 0.0;
                for (int t = 0; t < taps; ++t) {
                    const double scale = 1.0 - amount * (static_cast<double>(t) / (taps - 1));
                    const double sx = cx + (x - cx) * scale;
                    const double sy = cy + (y - cy) * scale;
                    const Bitmap::Pixel sample = sampleBilinear(source, sx, sy);
                    sumR += sample.r;
                    sumG += sample.g;
                    sumB += sample.b;
                    sumA += sample.a;
                }
                Bitmap::Pixel result{static_cast<uint8_t>(std::lround(sumR / taps)),
                                      static_cast<uint8_t>(std::lround(sumG / taps)),
                                      static_cast<uint8_t>(std::lround(sumB / taps)),
                                      static_cast<uint8_t>(std::lround(sumA / taps))};
                image.setPixel(x, y, result);
            }
        }
    });
}

// ビネット(周辺減光): 中心からの正規化距離d(0=中心、1=四隅相当)に対しsmoothstepで
// なめらかに暗くする。中心はd=0なのでsmoothstepの下端未満となり常に不変
void applyVignette(Bitmap& image, const Effect& effect) {
    const double amount = std::clamp(param(effect, "amount", 0.4), 0.0, 1.0);
    const double softness = std::clamp(param(effect, "softness", 0.5), 0.05, 1.0);
    const int w = image.width();
    const int h = image.height();
    if (w <= 0 || h <= 0 || amount <= 0.0) return;

    const double cx = (w - 1) / 2.0;
    const double cy = (h - 1) / 2.0;
    const double maxDist = std::sqrt(cx * cx + cy * cy);
    if (maxDist <= 0.0) return;
    const double edge0 = 1.0 - softness;

    parallelForRows(0, h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            for (int x = 0; x < w; ++x) {
                Bitmap::Pixel p = image.pixel(x, y);
                if (p.a == 0) continue;
                const double dx = x - cx;
                const double dy = y - cy;
                const double d = std::sqrt(dx * dx + dy * dy) / maxDist;
                const double t = std::clamp((d - edge0) / softness, 0.0, 1.0);
                const double darken = t * t * (3.0 - 2.0 * t) * amount;  // smoothstep
                const double factor = 1.0 - darken;
                p.r = static_cast<uint8_t>(std::lround(std::clamp(p.r * factor, 0.0, 255.0)));
                p.g = static_cast<uint8_t>(std::lround(std::clamp(p.g * factor, 0.0, 255.0)));
                p.b = static_cast<uint8_t>(std::lround(std::clamp(p.b * factor, 0.0, 255.0)));
                image.setPixel(x, y, p);
            }
        }
    });
}

// グレイン(フィルム粒状感): コマ番号+粒座標(ピクセル座標をsize単位に丸めたブロック)から
// hashNoise01で決定論的な擬似乱数を作り、輝度へ±ノイズを加減算する。同コマなら常に同じ結果になる。
// (frame, bx, by)をアバランシェハッシュへ直接渡すため、フレームが1つ違うだけでもブロックごとに
// 無相関な新しいパターンになる(std::minstd_randの初回出力はシードに線形なため使わない)
void applyGrain(Bitmap& image, const Effect& effect, size_t frame) {
    const double amount = std::clamp(param(effect, "amount", 0.15), 0.0, 1.0);
    const double size = std::clamp(param(effect, "size", 1.0), 1.0, 4.0);
    const int w = image.width();
    const int h = image.height();
    if (w <= 0 || h <= 0 || amount <= 0.0) return;

    parallelForRows(0, h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const int by = static_cast<int>(y / size);
            for (int x = 0; x < w; ++x) {
                Bitmap::Pixel p = image.pixel(x, y);
                if (p.a == 0) continue;
                const int bx = static_cast<int>(x / size);
                const double noise =
                    hashNoise01(static_cast<uint64_t>(frame), static_cast<uint64_t>(bx), static_cast<uint64_t>(by)) *
                        2.0 -
                    1.0;  // -1.0〜1.0
                const double delta = noise * amount * 127.5;
                p.r = static_cast<uint8_t>(std::lround(std::clamp(p.r + delta, 0.0, 255.0)));
                p.g = static_cast<uint8_t>(std::lround(std::clamp(p.g + delta, 0.0, 255.0)));
                p.b = static_cast<uint8_t>(std::lround(std::clamp(p.b + delta, 0.0, 255.0)));
                image.setPixel(x, y, p);
            }
        }
    });
}

// 色収差: R/Bチャンネルを中心から半径方向に±amount*d(dは正規化距離、0=中心〜1=四隅相当)だけ
// ずらしてバイリンサンプルする(Gはそのまま)。中心は距離0のためオフセット無し=不変、
// 周辺ほどR/Bのずれが大きくなり色にじみが出る
void applyChromAb(Bitmap& image, const Effect& effect) {
    const double amount = std::clamp(param(effect, "amount", 2.0), 0.0, 20.0);
    const int w = image.width();
    const int h = image.height();
    if (w <= 0 || h <= 0 || amount <= 0.0) return;

    const Bitmap source = image;  // サンプリング元。書き込み先(image)と分離して読み取り競合を避ける
    const double cx = (w - 1) / 2.0;
    const double cy = (h - 1) / 2.0;
    const double maxDist = std::sqrt(cx * cx + cy * cy);
    if (maxDist <= 0.0) return;

    parallelForRows(0, h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            for (int x = 0; x < w; ++x) {
                const double dx = x - cx;
                const double dy = y - cy;
                const double dist = std::sqrt(dx * dx + dy * dy);
                const Bitmap::Pixel orig = source.pixel(x, y);
                if (dist < 1e-6) {
                    image.setPixel(x, y, orig);
                    continue;
                }
                const double d = dist / maxDist;
                const double ux = dx / dist;
                const double uy = dy / dist;
                const double shift = amount * d;
                const Bitmap::Pixel rSample = sampleBilinear(source, x + ux * shift, y + uy * shift);
                const Bitmap::Pixel bSample = sampleBilinear(source, x - ux * shift, y - uy * shift);
                Bitmap::Pixel result{rSample.r, orig.g, bSample.b, orig.a};
                image.setPixel(x, y, result);
            }
        }
    });
}

// フィルム: 色温度・分光クロストーク・露出・特性曲線(S字)・黒浮き・チャンネル独立の粒状を
// この順に重ねてフィルムらしい発色と粒状感を再現する。色は0〜1のdoubleで処理し最後に量子化する。
// 透明ピクセル(a==0)はスキップして余白を汚さない
void applyFilm(Bitmap& image, const Effect& effect, size_t frame) {
    const double exposureEV = std::clamp(param(effect, "exposure", 0.0), -2.0, 2.0);
    const double contrast = std::clamp(param(effect, "contrast", 0.35), 0.0, 1.0);
    const double fade = std::clamp(param(effect, "fade", 0.04), 0.0, 0.3);
    const double warmth = std::clamp(param(effect, "warmth", 0.1), -1.0, 1.0);
    const double crosstalk = std::clamp(param(effect, "crosstalk", 0.08), 0.0, 0.5);
    const double grain = std::clamp(param(effect, "grain", 0.25), 0.0, 1.0);
    const double grainSize = std::clamp(param(effect, "grainSize", 1.6), 1.0, 4.0);
    const int w = image.width();
    const int h = image.height();
    if (w <= 0 || h <= 0) return;

    const double exposureMul = std::pow(2.0, exposureEV);

    parallelForRows(0, h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const int by = static_cast<int>(y / grainSize);
            for (int x = 0; x < w; ++x) {
                Bitmap::Pixel p = image.pixel(x, y);
                if (p.a == 0) continue;
                const int bx = static_cast<int>(x / grainSize);

                double r = p.r / 255.0, g = p.g / 255.0, b = p.b / 255.0;

                // 1. 色温度(タングステン/デイライトの近似): 暖色よりでR上げ・B下げ(warmth>0)
                r *= 1.0 + 0.15 * warmth;
                b *= 1.0 - 0.15 * warmth;

                // 2. 分光クロストーク(フィルム色素の感度重なり): 行正規化行列で混色する。
                // k=0で恒等、kが大きいほど混色して彩度が落ちる
                const double k = crosstalk;
                const double rr = (1.0 - 2.0 * k) * r + k * g + k * b;
                const double gg = k * r + (1.0 - 2.0 * k) * g + k * b;
                const double bb = k * r + k * g + (1.0 - 2.0 * k) * b;
                r = rr;
                g = gg;
                b = bb;

                // 3. 露出(EV)
                r *= exposureMul;
                g *= exposureMul;
                b *= exposureMul;

                double channels[3] = {r, g, b};
                for (int c = 0; c < 3; ++c) {
                    // 4. 特性曲線: smoothstepでS字(トウ+ショルダー)を掛ける
                    const double xClamped = std::clamp(channels[c], 0.0, 1.0);
                    const double s = xClamped * xClamped * (3.0 - 2.0 * xClamped);
                    double y = xClamped + (s - xClamped) * contrast;

                    // 5. 黒浮き(フィルムの最小濃度Dmin)
                    y = fade + (1.0 - fade) * y;

                    // 6. 粒状(RGB各チャンネル独立、中間調で最大・黒白で0の重み)。
                    // (frame, bx, by, c)から作るハッシュノイズはフレーム間・チャンネル間で無相関
                    if (grain > 0.0) {
                        const double noise = hashNoise01(static_cast<uint64_t>(frame), static_cast<uint64_t>(bx),
                                                          static_cast<uint64_t>(by), static_cast<uint64_t>(c)) *
                                                  2.0 -
                                              1.0;
                        const double weight = 4.0 * y * (1.0 - y);
                        y += noise * grain * 0.25 * weight;
                    }

                    channels[c] = std::clamp(y, 0.0, 1.0);
                }

                p.r = static_cast<uint8_t>(std::lround(channels[0] * 255.0));
                p.g = static_cast<uint8_t>(std::lround(channels[1] * 255.0));
                p.b = static_cast<uint8_t>(std::lround(channels[2] * 255.0));
                image.setPixel(x, y, p);
            }
        }
    });
}

}  // namespace

void applyEffect(Bitmap& image, const Effect& effect, size_t frame, double pixelScale) {
    if (!effect.enabled || image.isEmpty()) return;

    // キーフレーム曲線(コマ間の線形補間)を反映したパラメータ一式に解決してから適用する
    Effect resolved = effect;
    resolved.params = effect.paramsAt(frame);
    resolved.paramCurves.clear();

    // プロキシ縮小レンダリング時: px単位のパラメータを画像の縮小率に合わせてスケールし、
    // フル解像度と見た目を揃える(相対値のパラメータ=濃度・倍率・タップ数等はそのまま)
    if (pixelScale > 0.0 && pixelScale < 0.999) {
        const auto scaleIf = [&](const char* key) {
            const auto it = resolved.params.find(key);
            if (it != resolved.params.end()) it->second *= pixelScale;
        };
        scaleIf("radius");       // ブラー/グロー/ディフュージョンの半径
        scaleIf("amplitudeX");   // シェイクの振幅
        scaleIf("amplitudeY");
        if (resolved.type == EffectType::ChromAb) scaleIf("amount");  // 色収差のずれ量(px)
        if (resolved.type == EffectType::Grain) scaleIf("size");      // 粒サイズ(px)
        if (resolved.type == EffectType::Film) scaleIf("grainSize");  // フィルム粒サイズ(px)
    }

    switch (resolved.type) {
        case EffectType::Blur:
            applyBlur(image, resolved);
            break;
        case EffectType::Glow:
            applyGlow(image, resolved);
            break;
        case EffectType::Para:
            applyPara(image, resolved);
            break;
        case EffectType::Shake:
            applyShake(image, resolved, frame);
            break;
        case EffectType::ColorCorrect:
            applyColorCorrect(image, resolved);
            break;
        case EffectType::Diffusion:
            applyDiffusion(image, resolved);
            break;
        case EffectType::RadialBlur:
            applyRadialBlur(image, resolved);
            break;
        case EffectType::Vignette:
            applyVignette(image, resolved);
            break;
        case EffectType::Grain:
            applyGrain(image, resolved, frame);
            break;
        case EffectType::ChromAb:
            applyChromAb(image, resolved);
            break;
        case EffectType::Film:
            applyFilm(image, resolved, frame);
            break;
    }
}

}  // namespace core
