#pragma once

#include "Bitmap.h"

namespace core {

// 描画で書き換えられた矩形領域(テクスチャ部分更新用)。[x0,x1) x [y0,y1) の半開区間。
struct DirtyRect {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;

    bool isEmpty() const { return x1 <= x0 || y1 <= y0; }
    int width() const { return x1 - x0; }
    int height() const { return y1 - y0; }

    void unite(const DirtyRect& other);
    void clampTo(int width, int height);
};

enum class BrushMode {
    Paint,  // 色を塗る(src-over合成)
    Erase,  // 透明に戻す(アルファを削る)
};

struct BrushSettings {
    float radius = 6.0f;                // 基本半径(px)
    Bitmap::Pixel color{0, 0, 0, 255};  // 描画色(Eraseモードでは未使用)
    float spacingRatio = 0.35f;         // 半径に対するスタンプ間隔の比率
    bool pressureAffectsRadius = true;  // 筆圧で半径を変化させるか
    BrushMode mode = BrushMode::Paint;
};

// スタンプ方式のブラシ。ストローク座標列を受け取り、Bitmapへアンチエイリアス付きの
// 円スタンプを等間隔に打つ。Qt非依存(単体テスト可能)。
class BrushEngine {
public:
    BrushSettings& settings() { return m_settings; }
    const BrushSettings& settings() const { return m_settings; }

    DirtyRect beginStroke(Bitmap& bitmap, float x, float y, float pressure);
    DirtyRect continueStroke(Bitmap& bitmap, float x, float y, float pressure);
    void endStroke();

    bool isStrokeActive() const { return m_active; }

private:
    DirtyRect stamp(Bitmap& bitmap, float cx, float cy, float pressure) const;
    float stampRadius(float pressure) const;

    BrushSettings m_settings;
    bool m_active = false;
    float m_lastX = 0.0f;
    float m_lastY = 0.0f;
    float m_lastPressure = 1.0f;
    float m_residual = 0.0f;  // 次のスタンプまでの繰り越し距離
};

}  // namespace core
