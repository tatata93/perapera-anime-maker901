#include "Compositor.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "EffectProcessor.h"
#include "Multiplane.h"

namespace core {

namespace {

// マスク付きでエフェクトを適用する。effect.maskが空なら通常適用。
// 非空なら画面(キャンバス)座標のマスクのアルファを適用強度として、適用前/適用後を
// ピクセルごとにブレンドする(a=0は元のまま、a=255は完全適用)。
// imageのローカル座標(0,0)は画面座標(offsetX, offsetY)に対応する(全体エフェクトは0,0、
// セル対象はそのセルの画面配置オフセット)
void applyEffectWithMask(Bitmap& image, const Effect& effect, size_t frame, int offsetX, int offsetY) {
    if (effect.mask.isEmpty()) {
        applyEffect(image, effect, frame);
        return;
    }
    const Bitmap before = image;  // 適用前を保存
    applyEffect(image, effect, frame);

    // エフェクトはサイズを変えない(ブラー/グロー/パラ/シェイクとも同寸)が念のため小さい方に合わせる
    const int w = std::min(image.width(), before.width());
    const int h = std::min(image.height(), before.height());
    const auto blend = [](uint8_t b, uint8_t a, float t) {
        return static_cast<uint8_t>(std::lround(b + (a - b) * t));
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int mx = x + offsetX;
            const int my = y + offsetY;
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
}

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

// セルの可視レイヤーを重ねた透明合成ビットマップ(セルローカル座標、0,0起点)を作る。
// レイヤーが1枚も無ければ空のBitmapを返す。デジタル合成(セル別エフェクト適用)と
// クラシック撮影(マルチプレーンのアートワーク)の両方から使う共通処理
Bitmap buildCelComposite(const Cel& cel, int drawing, const RenderOptions& options) {
    std::vector<const Bitmap*> visibleLayers;
    int celW = 0;
    int celH = 0;
    for (size_t li = 0; li < cel.layerCount(); ++li) {
        const Layer& layer = cel.layer(li);
        if (!layer.visible()) continue;
        if (layer.role() == LayerRole::ColorTrace && !options.includeColorTrace) continue;
        if (layer.role() == LayerRole::Correction && !options.includeCorrection) continue;
        if (static_cast<size_t>(drawing) >= layer.frameCount()) continue;

        const Bitmap& src = layer.frame(static_cast<size_t>(drawing)).bitmap();
        if (src.isEmpty()) continue;
        visibleLayers.push_back(&src);
        celW = std::max(celW, src.width());
        celH = std::max(celH, src.height());
    }
    if (celW == 0 || celH == 0) return Bitmap();

    Bitmap celImage(celW, celH);
    celImage.fill({0, 0, 0, 0});
    for (const Bitmap* src : visibleLayers) blendOver(celImage, *src, 0, 0);
    return celImage;
}

// クラシック撮影(マルチプレーン撮影台)経路。割付(planes)にあるセルごとに
// セル単独の透明合成ビットマップを作り、マルチプレーンのレイトレースで撮影する。
// その後は従来経路と同じく全体エフェクト→カメラフレームクロップを適用する
Bitmap renderCutFrameClassic(const Cut& cut, size_t frame, int width, int height, const RenderOptions& options) {
    const MultiplaneSetup& setup = cut.multiplane();

    // artwork(Bitmap)は差し替え中もポインタが有効である必要があるため、
    // plane数ぶんreserveしてから push_back する(再確保でポインタが無効化しないように)
    std::vector<Bitmap> composites;
    std::vector<MultiplanePlane> mplanes;
    composites.reserve(setup.planes.size());
    mplanes.reserve(setup.planes.size());

    for (const MultiplaneCelPlane& p : setup.planes) {
        if (options.onlyCel >= 0 && options.onlyCel != p.celIndex) continue;
        if (p.celIndex < 0 || static_cast<size_t>(p.celIndex) >= cut.celCount()) continue;
        const Cel& cel = cut.cel(static_cast<size_t>(p.celIndex));
        if (!cel.visible()) continue;
        const int drawing = cel.exposure(frame);
        if (drawing < 0) continue;  // このコマにセルなし

        Bitmap celImage = buildCelComposite(cel, drawing, options);
        if (celImage.isEmpty()) continue;

        // このセルをtargetCelとする有効な撮影エフェクトをスタック順で適用する(従来経路と同じ規則)。
        // マスクはセルの画面配置オフセット(タップ移動)を考慮して参照する
        const Vec2 position = cel.positionAt(frame);
        const int celOffsetX = static_cast<int>(std::lround(position.x));
        const int celOffsetY = static_cast<int>(std::lround(position.y));
        for (const Effect& effect : cut.effects()) {
            if (effect.enabled && effect.targetCel == p.celIndex)
                applyEffectWithMask(celImage, effect, frame, celOffsetX, celOffsetY);
        }

        // タップ/ペグ移動(px)を平面内オフセット(mm)へ変換する
        const double mmPerPx = p.widthMm / celImage.width();

        composites.push_back(std::move(celImage));
        MultiplanePlane mp;
        mp.artwork = &composites.back();
        mp.distanceMm = p.distanceMm;
        mp.widthMm = p.widthMm;
        mp.offsetXMm = position.x * mmPerPx;
        mp.offsetYMm = position.y * mmPerPx;
        mplanes.push_back(mp);
    }

    int samples = setup.samplesPerPixel;
    if (options.multiplaneSampleCap > 0) samples = std::min(samples, options.multiplaneSampleCap);
    Bitmap out = renderMultiplane(mplanes, setup.camera, width, height, samples, 1);

    // 全平面合成後: 画面全体(targetCel==-1)を対象とする有効な撮影エフェクトをスタック順で適用する
    for (const Effect& effect : cut.effects()) {
        if (effect.enabled && effect.targetCel == -1) applyEffectWithMask(out, effect, frame, 0, 0);
    }

    if (const auto cam = cut.cameraFrameAt(frame)) {
        out = applyCameraFrame(out, *cam, width, height);
    }
    return out;
}

}  // namespace

Bitmap renderCutFrame(const Cut& cut, size_t frame, int width, int height, const RenderOptions& options) {
    // クラシック撮影(マルチプレーン撮影台)が有効なら専用経路へ。無効時は完全に従来動作
    // (バイト単位で同一)を保証する
    if (cut.multiplane().enabled) {
        return renderCutFrameClassic(cut, frame, width, height, options);
    }

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

        // このセルをtargetCelとする有効な撮影エフェクトを集める(スタック順)
        std::vector<const Effect*> celEffects;
        for (const Effect& effect : cut.effects()) {
            if (effect.enabled && effect.targetCel == static_cast<int>(ci)) celEffects.push_back(&effect);
        }

        if (celEffects.empty()) {
            // 従来経路: レイヤーを直接outへblendOverする(エフェクト無しの場合の性能維持、バイト同一を保証)
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
        } else {
            // エフェクトあり: セルのレイヤーをまずセル自身の座標系(0,0起点)の透明キャンバスへ合成し、
            // そのコピーへエフェクトを適用してから画面へ合成する(他セル・紙には影響しない)
            Bitmap celImage = buildCelComposite(cel, drawing, options);
            if (!celImage.isEmpty()) {
                // マスクはセルの画面配置オフセット(タップ移動)を考慮して参照する
                for (const Effect* effect : celEffects)
                    applyEffectWithMask(celImage, *effect, frame, offsetX, offsetY);
                blendOver(out, celImage, offsetX, offsetY);
            }
        }
    }

    // 全セル合成後: 画面全体(targetCel==-1)を対象とする有効な撮影エフェクトをスタック順で適用する
    for (const Effect& effect : cut.effects()) {
        if (effect.enabled && effect.targetCel == -1) applyEffectWithMask(out, effect, frame, 0, 0);
    }

    // カメラフレーム(画面に写る範囲)が指定されていればクロップ+リサンプルする。
    // エフェクトの後にクロップする(撮影の自然な順: エフェクト→カメラ)。
    // キーもエフェクトも無い場合は完全に既存動作のまま(バイト単位で同一)
    if (const auto cam = cut.cameraFrameAt(frame)) {
        out = applyCameraFrame(out, *cam, width, height);
    }
    return out;
}

}  // namespace core
