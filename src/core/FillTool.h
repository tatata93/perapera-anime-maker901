#pragma once

#include <vector>

#include "BrushEngine.h"  // DirtyRect

namespace core {

// 塗りつぶし(バケツ)。seedから連結した「空き領域」をcolorで塗る。
//
// 境界判定にはboundaryLayers(表示中レイヤー群、target自身を含めてよい)の
// 合成アルファを使う。いずれかのレイヤーでalpha >= alphaThreshold の画素は
// 線とみなして塗りを堰き止める。これにより主線レイヤーの線で囲んだ領域を
// 彩色レイヤーへ塗る、実際の彩色工程と同じ流れになる。
//
// 通常の主線は境界として塗りを止めるだけで、バケツが線画を塗り潰さない。
// fillableBoundaryLayersに渡した塗分け線(色トレス線)だけは、境界として止めた後に
// dilatePx分だけ線の内側へ塗りを入れる。最終画で消える指示線だけを塗りへ同化させるため。
//
// 戻り値は書き換えた矩形(空なら何も塗られなかった)。
DirtyRect floodFill(Bitmap& target, const std::vector<const Bitmap*>& boundaryLayers, int seedX, int seedY,
                    Bitmap::Pixel color, uint8_t alphaThreshold = 64, int dilatePx = 2,
                    const std::vector<const Bitmap*>& fillableBoundaryLayers = {});

}  // namespace core
