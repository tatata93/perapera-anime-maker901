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

}  // namespace

void applyEffect(Bitmap& image, const Effect& effect, size_t frame) {
    if (!effect.enabled || image.isEmpty()) return;

    // キーフレーム曲線(コマ間の線形補間)を反映したパラメータ一式に解決してから適用する
    Effect resolved = effect;
    resolved.params = effect.paramsAt(frame);
    resolved.paramCurves.clear();

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
    }
}

}  // namespace core
