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
// アンチエイリアスされた線の縁にハロー(塗り残し)が出ないよう、塗り領域は
// dilatePx分だけ線の下へ膨張させる(彩色レイヤーは主線の下に置かれる前提)。
//
// 戻り値は書き換えた矩形(空なら何も塗られなかった)。
DirtyRect floodFill(Bitmap& target, const std::vector<const Bitmap*>& boundaryLayers, int seedX, int seedY,
                    Bitmap::Pixel color, uint8_t alphaThreshold = 64, int dilatePx = 2);

}  // namespace core
