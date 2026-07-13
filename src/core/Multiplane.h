#pragma once

#include <cstdint>
#include <map>
#include <string>
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

// 透過光(T光)=撮影台のバックライト。実際の透過光撮影の「二重露光」を再現する:
// 通常露光(トップライト反射、紙=白)に加えて、下からの光源が全平面を色フィルターとして
// 透過しレンズへ届く露光を加算する。各平面の画素は per-channel の透過率
//   T_c = (1 - a) + a * (c_c) * paintTransmittance   (a,cは0〜1)
// として掛け合わされる(Beer-Lambert式の色フィルターの積)。不透明な黒(c=0)は完全に遮光する
// ため、「黒く塗ったセルに穴(未塗り部)を開けるとそこだけ光る」という実物と同じ使い方になる。
// 透過光は反射光と同じレンズサンプル光線で計算されるため、ピント外れの穴は物理的に正しい
// 玉ボケになる。bloomはフィルムのハレーション(強い光のにじみ)の近似
struct MultiplaneBacklight {
    std::string name;                  // UI表示用の灯の名前(既定は空、複数灯を見分けるため)
    bool enabled = false;
    double intensity = 4.0;            // 紙白(=1.0)に対する光源の相対強度
    double colorR = 1.0;               // 光源色(0〜1)
    double colorG = 0.92;
    double colorB = 0.78;              // 既定はやや電球色
    double paintTransmittance = 0.1;   // 塗料の透過率(0=完全遮光、1=完全なカラーゲル)
    double bloomRadiusPx = 24.0;       // ハレーションの広がり(出力px)
    double bloomStrength = 0.5;        // ハレーションの強さ(0=なし)

    // 光源マスク(スクリーン座標)。空でなければ、各出力ピクセルの透過光をこのマスクの
    // アルファ(0〜255)で乗算する=光源の形を絞る。ペンで描く(Compositor側で実効マスクを
    // 組み立てて渡す。マスクは出力解像度と同サイズが基本だが、サイズが違う場合はバイリニアで
    // サンプルする=安全側)
    Bitmap mask;
    // セル/レイヤーを光源マスクとして使う(-1=なし)。maskCelIndex>=0のとき、そのセルの
    // 現在コマの絵のアルファを光の形として使う(Compositor::renderCutFrameClassicが解決して
    // 上のmaskへ合成する)。maskLayerIndex=-1はセル全体(可視レイヤー合成)、>=0はそのレイヤーのみ
    int maskCelIndex = -1;
    int maskLayerIndex = -1;

    // コマ→強度のキーフレーム曲線(この灯だけの点滅用。空なら基本値intensityを使う)。
    // 灯ごとに独立して点滅できる(蛍光灯と液晶が別タイミングで明滅する等)
    std::map<size_t, double> intensityKeys;
};

// レイトレースで合成する。背景(全平面の奥)は白。samplesPerPixel>1でDoF/ジッタのモンテカルロ平均。
// seedで決定論的(同じ入力なら同じ出力)。backlightsがnullptr/空/全enabled=falseなら透過光なし
// (従来とバイト単位で同一)。呼び出し側(Compositor)が灯ごとにintensity/mask/bloomRadiusPxを
// 解決済みの状態で渡す
Bitmap renderMultiplane(const std::vector<MultiplanePlane>& planes, const MultiplaneCamera& camera, int width,
                         int height, int samplesPerPixel = 1, uint32_t seed = 1,
                         const std::vector<MultiplaneBacklight>* backlights = nullptr);

}  // namespace core
