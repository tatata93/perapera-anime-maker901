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
};

// カットのコマframeを最終画として合成する(紙=白の不透明画像)。
// 表示と同じ規則: 可視セル×可視レイヤーを下→上にsrc-over合成。
// GLに依存しないCPU実装(書き出し・テスト用)。
Bitmap renderCutFrame(const Cut& cut, size_t frame, int width, int height, const RenderOptions& options = {});

}  // namespace core
