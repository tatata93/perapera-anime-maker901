#pragma once

#include <cstddef>

#include "Bitmap.h"
#include "Effect.h"

namespace core {

// エフェクト1つをimageへ適用する(インプレース)。straight-alpha前提(色にアルファは掛かっていない)。
// frameはコマ番号(Shake/Grainの決定論的な擬似乱数生成に使う)。
// pixelScaleはプロキシ縮小レンダリング用(RenderOptions::proxyScale): imageが縮小されている場合、
// px単位のパラメータ(半径・振幅・色収差量・粒サイズ)を同じ倍率でスケールして見た目を揃える。
// 1.0(既定)は従来とバイト同一
void applyEffect(Bitmap& image, const Effect& effect, size_t frame, double pixelScale = 1.0);

}  // namespace core
