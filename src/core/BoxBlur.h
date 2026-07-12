#pragma once

#include <algorithm>
#include <vector>

#include "Parallel.h"

namespace core {

// 分離型の箱ぼかし(スライディングウィンドウ、端はクランプ)。1チャンネル分のfloatバッファを
// 書き換える。EffectProcessor(ブラー/グロー)とMultiplane(透過光のハレーション)で共用する。
// 行/列ごとに独立なので行(列)単位で並列化している(結果はシリアルと同一)

inline void boxBlurHorizontal(std::vector<float>& buf, int w, int h, int r) {
    if (w <= 0 || h <= 0 || r <= 0) return;
    std::vector<float> tmp(buf.size());
    const int windowSize = 2 * r + 1;
    parallelForRows(0, h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const float* row = &buf[static_cast<size_t>(y) * w];
            float* outRow = &tmp[static_cast<size_t>(y) * w];
            double sum = 0.0;
            for (int x = -r; x <= r; ++x) {
                const int cx = std::clamp(x, 0, w - 1);
                sum += row[cx];
            }
            outRow[0] = static_cast<float>(sum / windowSize);
            for (int x = 1; x < w; ++x) {
                const int addX = std::clamp(x + r, 0, w - 1);
                const int subX = std::clamp(x - r - 1, 0, w - 1);
                sum += row[addX] - row[subX];
                outRow[x] = static_cast<float>(sum / windowSize);
            }
        }
    });
    buf = std::move(tmp);
}

inline void boxBlurVertical(std::vector<float>& buf, int w, int h, int r) {
    if (w <= 0 || h <= 0 || r <= 0) return;
    std::vector<float> tmp(buf.size());
    const int windowSize = 2 * r + 1;
    parallelForRows(0, w, [&](int x0, int x1) {
        for (int x = x0; x < x1; ++x) {
            double sum = 0.0;
            for (int y = -r; y <= r; ++y) {
                const int cy = std::clamp(y, 0, h - 1);
                sum += buf[static_cast<size_t>(cy) * w + x];
            }
            tmp[static_cast<size_t>(0) * w + x] = static_cast<float>(sum / windowSize);
            for (int y = 1; y < h; ++y) {
                const int addY = std::clamp(y + r, 0, h - 1);
                const int subY = std::clamp(y - r - 1, 0, h - 1);
                sum += buf[static_cast<size_t>(addY) * w + x] - buf[static_cast<size_t>(subY) * w + x];
                tmp[static_cast<size_t>(y) * w + x] = static_cast<float>(sum / windowSize);
            }
        }
    });
    buf = std::move(tmp);
}

// 箱ぼかしを3回(横+縦を1セット×3)かける。ガウスぼかしの近似
inline void tripleBoxBlur(std::vector<float>& buf, int w, int h, int r) {
    for (int i = 0; i < 3; ++i) {
        boxBlurHorizontal(buf, w, h, r);
        boxBlurVertical(buf, w, h, r);
    }
}

}  // namespace core
