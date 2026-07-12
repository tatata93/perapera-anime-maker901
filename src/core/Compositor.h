#pragma once

#include "Bitmap.h"
#include "Cut.h"

namespace core {

// 最終画レンダリングのオプション
struct RenderOptions {
    bool includeColorTrace = false;  // 色トレス線レイヤーを含めるか(既定: 最終画なので除外)
    bool includeCorrection = false;  // 作監修正レイヤーを含めるか(既定: 除外)
    int onlyCel = -1;                // -1=全セル、0以上=指定セルのみ書き出し
    // クラシック撮影のプレビュー用サンプル数上限(0=無制限=設定どおり)。
    // 撮影ウィンドウのプレビューだけ軽くしたいときに使う(書き出しは0のままフル品質)
    int multiplaneSampleCap = 0;
    // プレビュー用の縮小レンダリング(0より大きく1未満で有効)。出力をwidth*scale×height*scaleで
    // 合成し、px単位のエフェクトパラメータ(ブラー半径・シェイク振幅等)も自動でスケールする
    // (見た目はフル解像度の縮小版に近くなる)。1.0(既定)はフル解像度=従来とバイト同一。
    // 書き出しは常に1.0を使うこと
    double proxyScale = 1.0;
};

// カットのコマframeを最終画として合成する(紙=白の不透明画像)。
// 表示と同じ規則: 可視セル×可視レイヤーを下→上にsrc-over合成。
// GLに依存しないCPU実装(書き出し・テスト用)。
Bitmap renderCutFrame(const Cut& cut, size_t frame, int width, int height, const RenderOptions& options = {});

}  // namespace core
