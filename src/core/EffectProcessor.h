#pragma once

#include <cstddef>

#include "Bitmap.h"
#include "Effect.h"

namespace core {

// エフェクト1つをimageへ適用する(インプレース)。straight-alpha前提(色にアルファは掛かっていない)。
// frameはコマ番号(Shakeの決定論的な擬似乱数生成に使う)。
void applyEffect(Bitmap& image, const Effect& effect, size_t frame);

}  // namespace core
