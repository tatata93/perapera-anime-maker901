#include "Compositor.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <map>
#include <vector>

#include "EffectProcessor.h"
#include "Multiplane.h"
#include "Parallel.h"

namespace core {

namespace {
// 距離ブラシの色マップ+スロットを、レンダラが読むグレースケール距離マップ(輝度→距離)へ解決する。
// 各画素は最も近い色のスロットの距離になり、その距離を[dmin,dmax]へ正規化した輝度で表す。
// 未塗り(alpha<128)はalpha0のまま=段の基準距離にフォールバックする
Bitmap resolveDistanceGray(const Bitmap& colorMap, const std::vector<MultiplaneDistanceStop>& stops, double& dmin,
                           double& dmax) {
    dmin = std::numeric_limits<double>::max();
    dmax = std::numeric_limits<double>::lowest();
    for (const MultiplaneDistanceStop& s : stops) {
        dmin = std::min(dmin, s.distanceMm);
        dmax = std::max(dmax, s.distanceMm);
    }
    if (!(dmax > dmin)) dmax = dmin + 1.0;
    Bitmap gray(colorMap.width(), colorMap.height());
    for (int y = 0; y < colorMap.height(); ++y) {
        for (int x = 0; x < colorMap.width(); ++x) {
            const Bitmap::Pixel p = colorMap.pixel(x, y);
            if (p.a < 128) {
                gray.setPixel(x, y, {0, 0, 0, 0});  // 未塗り
                continue;
            }
            int best = 0;
            long bestD = std::numeric_limits<long>::max();
            for (size_t i = 0; i < stops.size(); ++i) {
                const long dr = static_cast<long>(p.r) - stops[i].r, dg = static_cast<long>(p.g) - stops[i].g,
                           db = static_cast<long>(p.b) - stops[i].b;
                const long d = dr * dr + dg * dg + db * db;
                if (d < bestD) {
                    bestD = d;
                    best = static_cast<int>(i);
                }
            }
            const int gv = std::clamp(
                static_cast<int>(std::lround((stops[best].distanceMm - dmin) / (dmax - dmin) * 255.0)), 0, 255);
            gray.setPixel(x, y, {static_cast<uint8_t>(gv), static_cast<uint8_t>(gv), static_cast<uint8_t>(gv), 255});
        }
    }
    return gray;
}
}  // namespace

namespace {

// srcをscale倍(0<scale<1)に縮小したビットマップを返す(プレミルチプライドのバイリニア縮小)。
// プロキシ縮小レンダリング用。scale>=0.999ならコピーを避けられないためsrcのコピーを返す
Bitmap downsampleBitmap(const Bitmap& src, double scale) {
    if (src.isEmpty() || scale >= 0.999) return src;
    const int outW = std::max(1, static_cast<int>(std::lround(src.width() * scale)));
    const int outH = std::max(1, static_cast<int>(std::lround(src.height() * scale)));
    Bitmap out(outW, outH);

    parallelForRows(0, outH, [&](int rowBegin, int rowEnd) {
        for (int oy = rowBegin; oy < rowEnd; ++oy) {
            for (int ox = 0; ox < outW; ++ox) {
                // 出力ピクセル中心 → 元画像座標(バイリニア4タップ、端はクランプ)
                const double sx = (ox + 0.5) / scale - 0.5;
                const double sy = (oy + 0.5) / scale - 0.5;
                const int x0 = static_cast<int>(std::floor(sx));
                const int y0 = static_cast<int>(std::floor(sy));
                const double fx = sx - x0;
                const double fy = sy - y0;

                double r = 0.0, g = 0.0, b = 0.0, a = 0.0;
                for (int k = 0; k < 4; ++k) {
                    const int tx = std::clamp(x0 + (k & 1), 0, src.width() - 1);
                    const int ty = std::clamp(y0 + (k >> 1), 0, src.height() - 1);
                    const double wx = (k & 1) ? fx : 1.0 - fx;
                    const double wy = (k >> 1) ? fy : 1.0 - fy;
                    const double w = wx * wy;
                    const Bitmap::Pixel p = src.pixel(tx, ty);
                    const double pa = p.a / 255.0;
                    // 透明部分の色(通常0)に引っ張られないよう、色はアルファを掛けて平均する
                    r += w * p.r * pa;
                    g += w * p.g * pa;
                    b += w * p.b * pa;
                    a += w * p.a;
                }
                Bitmap::Pixel result{0, 0, 0, static_cast<uint8_t>(std::lround(std::clamp(a, 0.0, 255.0)))};
                if (result.a > 0) {
                    const double invA = 255.0 / a;
                    result.r = static_cast<uint8_t>(std::lround(std::clamp(r * invA, 0.0, 255.0)));
                    result.g = static_cast<uint8_t>(std::lround(std::clamp(g * invA, 0.0, 255.0)));
                    result.b = static_cast<uint8_t>(std::lround(std::clamp(b * invA, 0.0, 255.0)));
                }
                out.setPixel(ox, oy, result);
            }
        }
    });
    return out;
}

// マスク付きでエフェクトを適用する。effect.maskが空なら通常適用。
// 非空なら画面(キャンバス)座標のマスクのアルファを適用強度として、適用前/適用後を
// ピクセルごとにブレンドする(a=0は元のまま、a=255は完全適用)。
// imageのローカル座標(0,0)は画面のフル解像度座標(offsetX, offsetY)に対応する(全体エフェクトは
// 0,0、セル対象はそのセルの画面配置オフセット)。
// scaleはプロキシ縮小率(imageが縮小されている場合、マスクはフル解像度のまま参照し、
// エフェクトのpxパラメータはapplyEffect側でスケールされる)
void applyEffectWithMask(Bitmap& image, const Effect& effect, size_t frame, int offsetX, int offsetY,
                          double scale = 1.0) {
    if (effect.mask.isEmpty()) {
        applyEffect(image, effect, frame, scale);
        return;
    }
    const Bitmap before = image;  // 適用前を保存
    applyEffect(image, effect, frame, scale);

    // エフェクトはサイズを変えない(ブラー/グロー/パラ/シェイクとも同寸)が念のため小さい方に合わせる
    const int w = std::min(image.width(), before.width());
    const int h = std::min(image.height(), before.height());
    const bool proxied = scale > 0.0 && scale < 0.999;
    const auto blend = [](uint8_t b, uint8_t a, float t) {
        return static_cast<uint8_t>(std::lround(b + (a - b) * t));
    };
    parallelForRows(0, h, [&](int rowBegin, int rowEnd) {
        for (int y = rowBegin; y < rowEnd; ++y) {
            for (int x = 0; x < w; ++x) {
                // マスクはフル解像度の画面座標のまま参照する(プロキシ時は座標を逆スケール)
                const int lx = proxied ? static_cast<int>(std::lround((x + 0.5) / scale - 0.5)) : x;
                const int ly = proxied ? static_cast<int>(std::lround((y + 0.5) / scale - 0.5)) : y;
                const int mx = lx + offsetX;
                const int my = ly + offsetY;
                uint8_t maskA = 0;
                if (mx >= 0 && my >= 0 && mx < effect.mask.width() && my < effect.mask.height()) {
                    maskA = effect.mask.pixel(mx, my).a;
                }
                if (maskA == 255) continue;  // 完全適用: image(適用後)のまま
                const float t = maskA / 255.0f;
                const Bitmap::Pixel bp = before.pixel(x, y);
                const Bitmap::Pixel ap = image.pixel(x, y);
                image.setPixel(x, y,
                               {blend(bp.r, ap.r, t), blend(bp.g, ap.g, t), blend(bp.b, ap.b, t),
                                blend(bp.a, ap.a, t)});
            }
        }
    });
}

// srcをdst(不透明前提)へ、(offsetX, offsetY)だけずらしてsrc-over合成する。
// はみ出す部分はクリップされる(タップ/ペグ移動・引きセル対応)
void blendOver(Bitmap& dst, const Bitmap& src, int offsetX, int offsetY, double opacity = 1.0) {
    if (opacity <= 0.0) return;
    opacity = std::clamp(opacity, 0.0, 1.0);
    const int x0 = std::max(0, offsetX);
    const int y0 = std::max(0, offsetY);
    const int x1 = std::min(dst.width(), src.width() + offsetX);
    const int y1 = std::min(dst.height(), src.height() + offsetY);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const Bitmap::Pixel s = src.pixel(x - offsetX, y - offsetY);
            if (s.a == 0) continue;
            if (s.a == 255 && opacity >= 0.999) {
                dst.setPixel(x, y, {s.r, s.g, s.b, 255});
                continue;
            }
            const float a = static_cast<float>((s.a / 255.0) * opacity);
            Bitmap::Pixel d = dst.pixel(x, y);
            d.r = static_cast<uint8_t>(std::lround(s.r * a + d.r * (1.0f - a)));
            d.g = static_cast<uint8_t>(std::lround(s.g * a + d.g * (1.0f - a)));
            d.b = static_cast<uint8_t>(std::lround(s.b * a + d.b * (1.0f - a)));
            d.a = 255;
            dst.setPixel(x, y, d);
        }
    }
}

// 透明背景対応のsrc-over合成。blendOverはdst不透明前提でd.a=255に固定するため、
// 透明キャンバスへ重ねると縁が黒へ寄って不正になる。こちらは出力アルファ
// (oa = sa + da*(1-sa))を正しく計算し、色は非プリマルチプライドで合成する(透明PNG・
// 別レイヤー(プリビズ等)の上への重ね合成用)。
void blendOverTransparent(Bitmap& dst, const Bitmap& src, int offsetX, int offsetY, double opacity = 1.0) {
    if (opacity <= 0.0) return;
    opacity = std::clamp(opacity, 0.0, 1.0);
    const int x0 = std::max(0, offsetX);
    const int y0 = std::max(0, offsetY);
    const int x1 = std::min(dst.width(), src.width() + offsetX);
    const int y1 = std::min(dst.height(), src.height() + offsetY);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const Bitmap::Pixel s = src.pixel(x - offsetX, y - offsetY);
            if (s.a == 0) continue;
            const Bitmap::Pixel d = dst.pixel(x, y);
            const float sa = static_cast<float>((s.a / 255.0) * opacity);
            const float da = d.a / 255.0f;
            const float oa = sa + da * (1.0f - sa);
            if (oa <= 0.0f) {
                dst.setPixel(x, y, {0, 0, 0, 0});
                continue;
            }
            const auto ch = [&](uint8_t sc, uint8_t dc) {
                return static_cast<uint8_t>(std::lround((sc * sa + dc * da * (1.0f - sa)) / oa));
            };
            dst.setPixel(x, y, {ch(s.r, d.r), ch(s.g, d.g), ch(s.b, d.b),
                                static_cast<uint8_t>(std::lround(oa * 255.0f))});
        }
    }
}

// カメラフレーム(画面に写る範囲)でsrcをクロップ+バイリニア補間でリサンプルする。
// 出力は同じwidth×height。切り出し矩形はキャンバス外にはみ出すことがあり、
// その場合は紙(白)として扱う
void multiplyAlpha(Bitmap& image, double opacity) {
    if (image.isEmpty()) return;
    opacity = std::clamp(opacity, 0.0, 1.0);
    if (opacity >= 0.999) return;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            Bitmap::Pixel p = image.pixel(x, y);
            p.a = static_cast<uint8_t>(std::lround(p.a * opacity));
            image.setPixel(x, y, p);
        }
    }
}

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
    parallelForRows(0, height, [&](int rowBegin, int rowEnd) {
        for (int oy = rowBegin; oy < rowEnd; ++oy) {
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
    });
    return out;
}

// このセルに(このdrawingで)可視かつ非空のColorTraceレイヤーがあるか。
// エフェクト無しセルの高速経路(レイヤーを直接画面へblendOverする経路)でも、色トレス線の
// 同化処理(buildCelComposite)を通す必要があるかどうかの判定に使う
bool celHasVisibleColorTrace(const Cel& cel, int drawing) {
    for (size_t li = 0; li < cel.layerCount(); ++li) {
        const Layer& layer = cel.layer(li);
        if (!layer.visible()) continue;
        if (layer.opacity() <= 0.0) continue;
        if (layer.role() != LayerRole::ColorTrace) continue;
        if (static_cast<size_t>(drawing) >= layer.frameCount()) continue;
        if (!layer.frame(static_cast<size_t>(drawing)).bitmap().isEmpty()) return true;
    }
    return false;
}

// 最終合成でColorTraceレイヤーを除外した場合に、そのセルの「線が通っていた場所」を示す
// マスクを作る(celImageと同じセルローカル座標、celW×celH)。ColorTraceレイヤーが複数あれば
// 全て重ね合わせる。レイヤーのビットマップがcelImageより小さい/大きい場合は共通部分のみ見る
std::vector<bool> buildColorTraceMask(const Cel& cel, int drawing, int celW, int celH) {
    std::vector<bool> mask(static_cast<size_t>(celW) * static_cast<size_t>(celH), false);
    for (size_t li = 0; li < cel.layerCount(); ++li) {
        const Layer& layer = cel.layer(li);
        if (!layer.visible()) continue;  // 非表示のトレス線は同化対象にしない(buildCelCompositeの除外と揃える)
        if (layer.opacity() <= 0.0) continue;
        if (layer.role() != LayerRole::ColorTrace) continue;
        if (static_cast<size_t>(drawing) >= layer.frameCount()) continue;
        const Bitmap& src = layer.frame(static_cast<size_t>(drawing)).bitmap();
        if (src.isEmpty()) continue;

        const int w = std::min(src.width(), celW);
        const int h = std::min(src.height(), celH);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                if (src.pixel(x, y).a > 0) mask[static_cast<size_t>(y) * celW + x] = true;
            }
        }
    }
    return mask;
}

// 塗分け線(色トレス線)の白化修正: 最終合成では色トレス線レイヤーを除外するが、塗り(彩色)は
// 線の途中までしか届いていないため、線のあった場所が透明のまま残り紙の白として見えてしまう。
// 業界標準どおり、色トレス線は仕上げでその領域の塗り色に同化させる: traceMask内でまだ不透明
// でないピクセルを、隣接する不透明ピクセルの色の平均で埋めていく。
// Jacobi方式(各パスは直前パスのスナップショットだけを読む)なので実行順に依存せず決定論的。
// 変化が無くなるか最大64パスで打ち切る(境界は両側から同時に埋まっていくため線の中央付近になる)
void dissolveColorTraceLines(Bitmap& celImage, const std::vector<bool>& traceMask, int celW, int celH) {
    constexpr int kMaxPasses = 64;
    constexpr int kDx[4] = {-1, 1, 0, 0};
    constexpr int kDy[4] = {0, 0, -1, 1};

    for (int pass = 0; pass < kMaxPasses; ++pass) {
        const Bitmap snapshot = celImage;  // 前パスの状態(このパス中はこれだけを読む)
        std::atomic<bool> changed{false};
        parallelForRows(0, celH, [&](int rowBegin, int rowEnd) {
            for (int y = rowBegin; y < rowEnd; ++y) {
                for (int x = 0; x < celW; ++x) {
                    if (!traceMask[static_cast<size_t>(y) * celW + x]) continue;
                    if (snapshot.pixel(x, y).a == 255) continue;  // 既に不透明(同化済み)

                    int sumR = 0, sumG = 0, sumB = 0, count = 0;
                    for (int k = 0; k < 4; ++k) {
                        const int nx = x + kDx[k];
                        const int ny = y + kDy[k];
                        if (nx < 0 || ny < 0 || nx >= celW || ny >= celH) continue;
                        const Bitmap::Pixel np = snapshot.pixel(nx, ny);
                        if (np.a != 255) continue;  // 不透明な隣接ピクセルの色だけを使う
                        sumR += np.r;
                        sumG += np.g;
                        sumB += np.b;
                        ++count;
                    }
                    if (count == 0) continue;  // まだ埋める材料が無い(次パス以降で埋まる可能性あり)

                    celImage.setPixel(x, y,
                                       {static_cast<uint8_t>(sumR / count), static_cast<uint8_t>(sumG / count),
                                        static_cast<uint8_t>(sumB / count), 255});
                    changed = true;
                }
            }
        });
        if (!changed) break;  // これ以上埋まらない(収束)
    }
}

// セルの可視レイヤーを重ねた透明合成ビットマップ(セルローカル座標、0,0起点)を作る。
// レイヤーが1枚も無ければ空のBitmapを返す。デジタル合成(セル別エフェクト適用)と
// クラシック撮影(マルチプレーンのアートワーク)の両方から使う共通処理
Bitmap buildCelComposite(const Cel& cel, int drawing, const RenderOptions& options) {
    struct LayerCompositeSource {
        const Bitmap* bitmap = nullptr;
        double opacity = 1.0;
    };
    std::vector<LayerCompositeSource> visibleLayers;
    int celW = 0;
    int celH = 0;
    for (size_t li = 0; li < cel.layerCount(); ++li) {
        const Layer& layer = cel.layer(li);
        if (!layer.visible()) continue;
        if (layer.opacity() <= 0.0) continue;
        if (layer.role() == LayerRole::ColorTrace && !options.includeColorTrace) continue;
        if (layer.role() == LayerRole::Correction && !options.includeCorrection) continue;
        if (static_cast<size_t>(drawing) >= layer.frameCount()) continue;

        const Bitmap& src = layer.frame(static_cast<size_t>(drawing)).bitmap();
        if (src.isEmpty()) continue;
        visibleLayers.push_back({&src, layer.opacity()});
        celW = std::max(celW, src.width());
        celH = std::max(celH, src.height());
    }
    if (celW == 0 || celH == 0) return Bitmap();

    Bitmap celImage(celW, celH);
    celImage.fill({0, 0, 0, 0});
    for (const LayerCompositeSource& src : visibleLayers) blendOverTransparent(celImage, *src.bitmap, 0, 0, src.opacity);

    // 色トレス線を除外した合成で、かつこのセルに実際に色トレス線があるときだけ同化処理を行う
    // (線が無いセルは従来どおり一切変化しない)
    if (!options.includeColorTrace) {
        const std::vector<bool> traceMask = buildColorTraceMask(cel, drawing, celW, celH);
        if (std::any_of(traceMask.begin(), traceMask.end(), [](bool b) { return b; })) {
            dissolveColorTraceLines(celImage, traceMask, celW, celH);
        }
    }

    return celImage;
}

// 透過光(T光)の光源マスク用: 指定セル/レイヤーの現在コマの絵をアルファ形状として使うための
// ビットマップ(セルローカル座標)を返す。celIndexが不正・このコマに絵が無い・layerIndexが不正なら
// 空(Bitmap())を返す(=マスク寄与なし=そのピクセルは光らない)
Bitmap buildBacklightCelMaskSource(const Cut& cut, int celIndex, int layerIndex, size_t frame,
                                    const RenderOptions& options) {
    if (celIndex < 0 || static_cast<size_t>(celIndex) >= cut.celCount()) return Bitmap();
    const Cel& cel = cut.cel(static_cast<size_t>(celIndex));
    const int drawing = cel.exposure(frame);
    if (drawing < 0) return Bitmap();  // このコマに絵が無い
    if (cel.opacity() <= 0.0) return Bitmap();
    if (layerIndex < 0) {
        Bitmap mask = buildCelComposite(cel, drawing, options);  // セル全体(可視レイヤー合成)
        multiplyAlpha(mask, cel.opacity());
        return mask;
    }
    if (static_cast<size_t>(layerIndex) >= cel.layerCount()) return Bitmap();
    const Layer& layer = cel.layer(static_cast<size_t>(layerIndex));
    if (!layer.visible() || layer.opacity() <= 0.0) return Bitmap();
    if (static_cast<size_t>(drawing) >= layer.frameCount()) return Bitmap();
    Bitmap mask = layer.frame(static_cast<size_t>(drawing)).bitmap();
    multiplyAlpha(mask, cel.opacity() * layer.opacity());
    return mask;
}

// 段(マルチプレーンの1つの平面)に割り付いているセルの物理配置(T光セルマスク投影に使う)
struct CelPlaneInfo {
    double distanceMm = 0.0;
    double widthMm = 0.0;
    double offsetXMm = 0.0;  // タップ/ペグ移動を反映済み(mm)
    double offsetYMm = 0.0;
};

// アートワークのアルファ(0〜1)をバイリニア補間でサンプルする。範囲外は0(透明)。
// Multiplane.cppのsampleArtworkBilinearと同じ流儀(テクセル中心はu-0.5)
double sampleAlphaBilinear(const Bitmap& bmp, double u, double v) {
    const auto tap = [&](int x, int y) -> double {
        if (x < 0 || y < 0 || x >= bmp.width() || y >= bmp.height()) return 0.0;
        return bmp.pixel(x, y).a / 255.0;
    };
    const double su = u - 0.5;
    const double sv = v - 0.5;
    const int x0 = static_cast<int>(std::floor(su));
    const int y0 = static_cast<int>(std::floor(sv));
    const double fx = su - x0;
    const double fy = sv - y0;
    const double top = tap(x0, y0) + (tap(x0 + 1, y0) - tap(x0, y0)) * fx;
    const double bottom = tap(x0, y0 + 1) + (tap(x0 + 1, y0 + 1) - tap(x0, y0 + 1)) * fx;
    return top + (bottom - top) * fy;
}

// T光のセルマスクを、そのセルが割り付いている段の物理配置(距離・幅・オフセット)でピンホール投影
// してサンプルする(renderMultiplaneのsamplePlane/PlaneContextと同じ式)。
// バグ修正: 従来はマスクセルを「出力px→フル解像度キャンバスpx→セルビットマップpx(1:1)」で参照して
// いたが、クラシック撮影のセルは距離mm/幅mmでレンズ投影される座標系なので、これでは実際にそのセルが
// 画面に写る位置・大きさとズレる(例: 望遠で奥のセルが小さく写っていてもマスクは等倍のまま)。
// outX/outYは出力解像度(width×height、プロキシ時は縮小済み)のピクセル座標
double sampleCelMaskAlphaProjected(const Bitmap& celSource, const CelPlaneInfo& info, double focalLengthMm,
                                    double sensorWidthMm, int outX, int outY, int outW, int outH) {
    if (celSource.isEmpty() || info.widthMm <= 0.0 || focalLengthMm <= 0.0 || outW <= 0 || outH <= 0) return 0.0;
    const double sensorHeightMm = sensorWidthMm * outH / outW;
    const double sx = ((outX + 0.5) / outW - 0.5) * sensorWidthMm;
    const double sy = ((outY + 0.5) / outH - 0.5) * sensorHeightMm;
    const double wx = sx * info.distanceMm / focalLengthMm;
    const double wy = sy * info.distanceMm / focalLengthMm;

    const double heightMm = info.widthMm * celSource.height() / celSource.width();
    const double pxPerMmX = celSource.width() / info.widthMm;
    const double pxPerMmY = celSource.height() / heightMm;
    const double u = (wx - info.offsetXMm + info.widthMm / 2.0) * pxPerMmX;
    const double v = (wy - info.offsetYMm + heightMm / 2.0) * pxPerMmY;
    return sampleAlphaBilinear(celSource, u, v);
}

// クラシック撮影(マルチプレーン撮影台)経路。割付(planes)にあるセルごとに
// セル単独の透明合成ビットマップを作り、マルチプレーンのレイトレースで撮影する。
// その後は従来経路と同じく全体エフェクト→カメラフレームクロップを適用する。
// width/heightは出力解像度(プロキシ時は縮小済み)、sはプロキシ縮小率(1.0=フル)。
// レイトレーサは解像度非依存(物理mm指定)なので、出力解像度を下げるだけで正しく縮小になる
Bitmap renderCutFrameClassic(const Cut& cut, size_t frame, int width, int height, const RenderOptions& options,
                              double s) {
    const MultiplaneSetup& setup = cut.multiplane();
    const bool proxied = s != 1.0;

    // artwork(Bitmap)は差し替え中もポインタが有効である必要があるため、
    // plane数ぶんreserveしてから push_back する(再確保でポインタが無効化しないように)
    std::vector<Bitmap> composites;
    std::vector<MultiplanePlane> mplanes;
    composites.reserve(setup.planes.size());
    mplanes.reserve(setup.planes.size());
    // 距離ブラシの解決済みグレースケール距離マップ(renderMultiplane呼び出しまでポインタを生かす)
    std::vector<Bitmap> distanceGrays;
    distanceGrays.reserve(setup.planes.size());

    // セルindex→最初に割り付いた段の物理配置(T光セルマスクの段投影に使う、バグ修正)。
    // 「そのセルが割り付いている段(planes内でcelIndex一致する最初の段)」を採用するため、
    // 既に登録済みのcelIndexは上書きしない
    std::map<int, CelPlaneInfo> celPlaneInfo;

    for (const MultiplaneCelPlane& p : setup.planes) {
        if (options.onlyCel >= 0 && options.onlyCel != p.celIndex) continue;
        if (p.celIndex < 0 || static_cast<size_t>(p.celIndex) >= cut.celCount()) continue;
        const Cel& cel = cut.cel(static_cast<size_t>(p.celIndex));
        if (!cel.visible()) continue;
        if (cel.opacity() <= 0.0) continue;
        const int drawing = cel.exposure(frame);
        if (drawing < 0) continue;  // このコマにセルなし

        Bitmap celImage = buildCelComposite(cel, drawing, options);
        if (celImage.isEmpty()) continue;

        // タップ/ペグ移動(px)を平面内オフセット(mm)へ変換する(縮小前のフル解像度幅で計算する)
        const Vec2 position = cel.positionAt(frame);
        const double mmPerPx = p.widthMm / celImage.width();

        // プロキシ時はアートワークを縮小してからエフェクトを掛ける(レイトレーサはバイリニア
        // サンプルなので、物理幅mmが同じなら解像度が下がっても写りは同じ)
        if (proxied) celImage = downsampleBitmap(celImage, s);

        // このセルをtargetCelとする有効な撮影エフェクトをスタック順で適用する(従来経路と同じ規則)。
        // マスクはセルの画面配置オフセット(タップ移動、フル解像度座標)を考慮して参照する
        const int celOffsetX = static_cast<int>(std::lround(position.x));
        const int celOffsetY = static_cast<int>(std::lround(position.y));
        for (const Effect& effect : cut.effects()) {
            if (effect.enabled && effect.activeAt(frame) && effect.targetCel == p.celIndex)
                applyEffectWithMask(celImage, effect, frame, celOffsetX, celOffsetY, s);
        }
        multiplyAlpha(celImage, cel.opacity());

        composites.push_back(std::move(celImage));
        MultiplanePlane mp;
        mp.artwork = &composites.back();
        mp.distanceMm = p.distanceMm;
        mp.widthMm = p.widthMm;
        mp.offsetXMm = position.x * mmPerPx;
        mp.offsetYMm = position.y * mmPerPx;
        // 距離ブラシ(セル内の色塗り分け)。色マップ+スロットをグレースケール距離マップへ解決して渡す
        if (!p.distanceMap.isEmpty() && !p.distanceStops.empty()) {
            double dmin = 0.0, dmax = 0.0;
            distanceGrays.push_back(resolveDistanceGray(p.distanceMap, p.distanceStops, dmin, dmax));
            mp.distanceMap = &distanceGrays.back();
            mp.distanceNearMm = dmin;
            mp.distanceFarMm = dmax;
        }
        mplanes.push_back(mp);

        if (celPlaneInfo.find(p.celIndex) == celPlaneInfo.end()) {
            celPlaneInfo[p.celIndex] = {p.distanceMm, p.widthMm, mp.offsetXMm, mp.offsetYMm};
        }
    }

    // 書き出しは高品質(exportSamplesPerPixel)、作業/プレビューは基準(samplesPerPixel)。
    // さらにプレビューは multiplaneSampleCap で上限を絞って軽くする
    int samples = options.useExportSamples ? setup.exportSamplesPerPixel : setup.samplesPerPixel;
    samples = std::max(1, samples);
    if (options.multiplaneSampleCap > 0) samples = std::min(samples, options.multiplaneSampleCap);
    // 高速プレビュー: 被写界深度(レンズ絞り)のモンテカルロを丸ごと省き、ピンホール1レイ/pxで
    // 合成する。samplesを1に落としてもノイズが出ず(ピンホールは決定的)、撮影ウィンドウが固まらない
    if (options.multiplaneFastPreview) samples = 1;

    // キーフレーム(滑らかなカメラ変化=focalKeys/focusKeys/fstopKeys)をこのコマの値へ解決する。
    // フレーミング固定: framingLockならsensorWidthMmを「基準距離framingRefDistanceMmの平面上で
    // 写る幅framingWidthMm」から導出する(焦点距離を変えても基準距離の構図が変わらない)。
    // セルマスクの段投影(下記)はこの解決後のfocal/sensorを使う
    MultiplaneCamera camera = setup.camera;
    camera.focalLengthMm = MultiplaneSetup::valueAt(setup.focalKeys, frame, camera.focalLengthMm);
    camera.focusDistanceMm = MultiplaneSetup::valueAt(setup.focusKeys, frame, camera.focusDistanceMm);
    camera.apertureFStop = MultiplaneSetup::valueAt(setup.fstopKeys, frame, camera.apertureFStop);
    if (setup.framingLock && setup.framingRefDistanceMm > 0.0) {
        camera.sensorWidthMm = setup.framingWidthMm * camera.focalLengthMm / setup.framingRefDistanceMm;
    }
    // 高速プレビュー: 絞りを0にしてピンホール化する(renderMultiplane内でapertureFStop<=0がピンホール判定)。
    // これで被写界深度のレンズサンプリングが完全に省かれ、決定的でノイズの無い軽い合成になる
    if (options.multiplaneFastPreview) camera.apertureFStop = 0.0;

    // 灯ごとに: intensity(灯のintensityKeysで解決)・実効マスク(ペン×セル)・プロキシ時の
    // ブルーム半径スケールを解決する。ペンマスク=スクリーン1:1(従来どおり)、セルマスクは
    // そのセルが割り付いている段の物理配置でピンホール投影する(バグ修正、無ければ従来の
    // キャンバス1:1フォールバック)
    std::vector<MultiplaneBacklight> resolvedBacklights;
    resolvedBacklights.reserve(setup.backlights.size());
    for (const MultiplaneBacklight& srcBl : setup.backlights) {
        MultiplaneBacklight bl = srcBl;
        if (!bl.enabled) {
            resolvedBacklights.push_back(std::move(bl));
            continue;
        }

        // 透過光のハレーション半径は出力px単位なのでプロキシ時はスケールする
        if (proxied) bl.bloomRadiusPx *= s;
        bl.intensity = MultiplaneSetup::valueAt(srcBl.intensityKeys, frame, bl.intensity);

        // 光源マスク(T光にもマスクを): ペンマスク(srcBl.mask、フル解像度スクリーン座標)と
        // セル/レイヤーマスク(srcBl.maskCelIndex)を組み合わせた実効マスクを、この出力解像度
        // (width×height、プロキシ時は縮小済み)へ直接構築する。ペンマスクの座標は
        // applyEffectWithMaskと同じ流儀(出力px→フル解像度pxへ逆スケールしてから参照)
        const bool hasPenMask = !srcBl.mask.isEmpty();
        const bool hasCelMask = srcBl.maskCelIndex >= 0;
        if (hasPenMask || hasCelMask) {
            const Bitmap maskCelSource =
                hasCelMask ? buildBacklightCelMaskSource(cut, srcBl.maskCelIndex, srcBl.maskLayerIndex, frame,
                                                          options)
                           : Bitmap();

            // 段投影(バグ修正): マスクセルが撮影台の段に割り付いていれば(celPlaneInfoにあれば)
            // その段の距離・幅・オフセットでピンホール投影してサンプルする
            const auto celPlaneIt = celPlaneInfo.find(srcBl.maskCelIndex);
            const bool hasProjection = hasCelMask && celPlaneIt != celPlaneInfo.end() && !maskCelSource.isEmpty();

            // フォールバック用(段に割り付いていないセル): 従来のキャンバス1:1参照
            Vec2 maskCelPos{0.0f, 0.0f};
            if (hasCelMask && static_cast<size_t>(srcBl.maskCelIndex) < cut.celCount()) {
                maskCelPos = cut.cel(static_cast<size_t>(srcBl.maskCelIndex)).positionAt(frame);
            }
            const int maskCelOffsetX = static_cast<int>(std::lround(maskCelPos.x));
            const int maskCelOffsetY = static_cast<int>(std::lround(maskCelPos.y));
            const Bitmap& penMask = srcBl.mask;

            Bitmap effectiveMask(width, height);
            parallelForRows(0, height, [&](int rowBegin, int rowEnd) {
                for (int y = rowBegin; y < rowEnd; ++y) {
                    for (int x = 0; x < width; ++x) {
                        const int lx = proxied ? static_cast<int>(std::lround((x + 0.5) / s - 0.5)) : x;
                        const int ly = proxied ? static_cast<int>(std::lround((y + 0.5) / s - 0.5)) : y;

                        double a = 1.0;
                        if (hasPenMask) {
                            uint8_t pa = 0;
                            if (lx >= 0 && ly >= 0 && lx < penMask.width() && ly < penMask.height())
                                pa = penMask.pixel(lx, ly).a;
                            a *= pa / 255.0;
                        }
                        if (hasCelMask) {
                            double ca = 0.0;
                            if (hasProjection) {
                                ca = sampleCelMaskAlphaProjected(maskCelSource, celPlaneIt->second,
                                                                  camera.focalLengthMm, camera.sensorWidthMm, x, y,
                                                                  width, height);
                            } else if (!maskCelSource.isEmpty()) {
                                const int mx = lx - maskCelOffsetX;
                                const int my = ly - maskCelOffsetY;
                                if (mx >= 0 && my >= 0 && mx < maskCelSource.width() && my < maskCelSource.height())
                                    ca = maskCelSource.pixel(mx, my).a / 255.0;
                            }
                            a *= ca;
                        }
                        const uint8_t a8 = static_cast<uint8_t>(std::lround(std::clamp(a, 0.0, 1.0) * 255.0));
                        effectiveMask.setPixel(x, y, {255, 255, 255, a8});
                    }
                }
            });
            bl.mask = std::move(effectiveMask);
        }
        resolvedBacklights.push_back(std::move(bl));
    }

    Bitmap out = renderMultiplane(mplanes, camera, width, height, samples, 1, &resolvedBacklights);

    // 全平面合成後: 画面全体(targetCel==-1)を対象とする有効な撮影エフェクトをスタック順で適用する
    for (const Effect& effect : cut.effects()) {
        if (effect.enabled && effect.activeAt(frame) && effect.targetCel == -1)
            applyEffectWithMask(out, effect, frame, 0, 0, s);
    }

    if (const auto cam = cut.cameraFrameAt(frame)) {
        CameraFrameState camState = *cam;
        if (proxied) {
            camState.center.x = static_cast<float>(camState.center.x * s);
            camState.center.y = static_cast<float>(camState.center.y * s);
        }
        out = applyCameraFrame(out, camState, width, height);
    }
    return out;
}

}  // namespace

Bitmap renderCutFrame(const Cut& cut, size_t frame, int width, int height, const RenderOptions& options) {
    // プロキシ縮小レンダリング(プレビュー用): 出力・エフェクトを縮小解像度で行い高速化する。
    // s==1.0(既定)は従来とバイト単位で同一
    const double s = (options.proxyScale > 0.0 && options.proxyScale < 0.999) ? options.proxyScale : 1.0;
    const bool proxied = s != 1.0;
    const int outW = proxied ? std::max(1, static_cast<int>(std::lround(width * s))) : width;
    const int outH = proxied ? std::max(1, static_cast<int>(std::lround(height * s))) : height;

    // クラシック撮影(マルチプレーン撮影台)が有効なら専用経路へ。無効時は完全に従来動作
    // (バイト単位で同一)を保証する
    if (cut.multiplane().enabled) {
        return renderCutFrameClassic(cut, frame, outW, outH, options, s);
    }

    Bitmap out(outW, outH);
    // 透明背景指定時は紙(白)を敷かず透明のまま。作画レイヤーだけがアルファ付きで合成される
    out.fill(options.transparentBackground ? Bitmap::Pixel{0, 0, 0, 0} : Bitmap::Pixel{255, 255, 255, 255});

    for (size_t ci = 0; ci < cut.celCount(); ++ci) {
        if (options.onlyCel >= 0 && static_cast<size_t>(options.onlyCel) != ci) continue;
        const Cel& cel = cut.cel(ci);
        if (!cel.visible()) continue;
        if (cel.opacity() <= 0.0) continue;
        const int drawing = cel.exposure(frame);
        if (drawing < 0) continue;  // このコマにセルなし

        // タップ/ペグ移動: このコマでのセル位置(キー間は線形補間)。
        // フル解像度座標(マスク参照用)と出力座標(プロキシ時は縮小)の両方を持つ
        const Vec2 position = cel.positionAt(frame);
        const int offsetX = static_cast<int>(std::lround(position.x));
        const int offsetY = static_cast<int>(std::lround(position.y));
        const int outOffsetX = proxied ? static_cast<int>(std::lround(position.x * s)) : offsetX;
        const int outOffsetY = proxied ? static_cast<int>(std::lround(position.y * s)) : offsetY;

        // このセルをtargetCelとする有効な撮影エフェクトを集める(スタック順)
        std::vector<const Effect*> celEffects;
        for (const Effect& effect : cut.effects()) {
            if (effect.enabled && effect.activeAt(frame) && effect.targetCel == static_cast<int>(ci))
                celEffects.push_back(&effect);
        }

        // エフェクトが無くても、除外される色トレス線(白化修正の同化処理が必要)を持つセルは
        // 高速経路を使えない(buildCelCompositeを通す必要がある)。プロキシ時は縮小合成が必要な
        // ため常にセル合成経路を使う
        const bool needsCelComposite = proxied || !celEffects.empty() ||
                                       (!options.includeColorTrace && celHasVisibleColorTrace(cel, drawing));

        if (!needsCelComposite) {
            // 従来経路: レイヤーを直接outへblendOverする(エフェクト・トレス同化が無い場合の性能維持、
            // バイト同一を保証)
            for (size_t li = 0; li < cel.layerCount(); ++li) {
                const Layer& layer = cel.layer(li);
                if (!layer.visible()) continue;
                if (layer.opacity() <= 0.0) continue;
                if (layer.role() == LayerRole::ColorTrace && !options.includeColorTrace) continue;
                if (layer.role() == LayerRole::Correction && !options.includeCorrection) continue;
                if (static_cast<size_t>(drawing) >= layer.frameCount()) continue;

                const Bitmap& src = layer.frame(static_cast<size_t>(drawing)).bitmap();
                if (src.isEmpty()) continue;
                const double opacity = cel.opacity() * layer.opacity();
                if (options.transparentBackground)
                    blendOverTransparent(out, src, offsetX, offsetY, opacity);
                else
                    blendOver(out, src, offsetX, offsetY, opacity);
            }
        } else {
            // エフェクトあり(またはプロキシ): セルのレイヤーをまずセル自身の座標系(0,0起点)の
            // 透明キャンバスへ合成し、そのコピーへエフェクトを適用してから画面へ合成する
            Bitmap celImage = buildCelComposite(cel, drawing, options);
            if (!celImage.isEmpty()) {
                if (proxied) celImage = downsampleBitmap(celImage, s);  // エフェクトは縮小解像度で行う
                // マスクはセルの画面配置オフセット(タップ移動、フル解像度座標)を考慮して参照する
                for (const Effect* effect : celEffects)
                    applyEffectWithMask(celImage, *effect, frame, offsetX, offsetY, s);
                if (options.transparentBackground)
                    blendOverTransparent(out, celImage, outOffsetX, outOffsetY, cel.opacity());
                else
                    blendOver(out, celImage, outOffsetX, outOffsetY, cel.opacity());
            }
        }
    }

    // 全セル合成後: 画面全体(targetCel==-1)を対象とする有効な撮影エフェクトをスタック順で適用する
    for (const Effect& effect : cut.effects()) {
        if (effect.enabled && effect.activeAt(frame) && effect.targetCel == -1)
            applyEffectWithMask(out, effect, frame, 0, 0, s);
    }

    // カメラフレーム(画面に写る範囲)が指定されていればクロップ+リサンプルする。
    // エフェクトの後にクロップする(撮影の自然な順: エフェクト→カメラ)。
    // キーもエフェクトも無い場合は完全に既存動作のまま(バイト単位で同一)
    if (const auto cam = cut.cameraFrameAt(frame)) {
        CameraFrameState camState = *cam;
        if (proxied) {
            // カメラ中心はフル解像度px指定なので出力座標系へスケールする(scale倍率はそのまま)
            camState.center.x = static_cast<float>(camState.center.x * s);
            camState.center.y = static_cast<float>(camState.center.y * s);
        }
        out = applyCameraFrame(out, camState, outW, outH);
    }
    return out;
}

}  // namespace core
