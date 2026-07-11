#pragma once

#include <cstdint>
#include <vector>

#include "Bitmap.h"

namespace core {

// 撮影台の1段(セル/背景を置く平面)。カメラは原点でZ+方向(下)を向く
struct MultiplanePlane {
    const Bitmap* artwork = nullptr;  // 透明ビットマップ(straight-alpha)
    double distanceMm = 500.0;        // レンズから平面までの距離(mm)
    double widthMm = 400.0;           // アートワークの物理幅(mm)。高さはビットマップのアスペクトから
    double offsetXMm = 0.0;           // 面内オフセット(mm、+xは画面右方向)
    double offsetYMm = 0.0;           // (+yは画面下方向)
};

// 撮影台カメラ(実写相当の薄レンズモデル)
struct MultiplaneCamera {
    double focalLengthMm = 50.0;
    double sensorWidthMm = 36.0;      // センサー高さはwidth*出力height/出力widthで決まる
    double apertureFStop = 0.0;       // 0以下=ピンホール(全面パンフォーカス)。>0でレンズ径=focal/fstop
    double focusDistanceMm = 500.0;   // ピントの合う平面距離
};

// レイトレースで合成する。背景(全平面の奥)は白。samplesPerPixel>1でDoF/ジッタのモンテカルロ平均。
// seedで決定論的(同じ入力なら同じ出力)
Bitmap renderMultiplane(const std::vector<MultiplanePlane>& planes, const MultiplaneCamera& camera, int width,
                         int height, int samplesPerPixel = 1, uint32_t seed = 1);

}  // namespace core
