#include "FillTool.h"

#include <algorithm>
#include <queue>

namespace core {

namespace {

// boundaryLayersのいずれかでalphaが閾値以上なら「線」(塗り止め)とみなす
bool isBlocked(const std::vector<const Bitmap*>& layers, int x, int y, uint8_t threshold) {
    for (const Bitmap* layer : layers) {
        if (!layer || layer->isEmpty()) continue;
        if (x >= layer->width() || y >= layer->height()) continue;
        if (layer->pixel(x, y).a >= threshold) return true;
    }
    return false;
}

}  // namespace

DirtyRect floodFill(Bitmap& target, const std::vector<const Bitmap*>& boundaryLayers, int seedX, int seedY,
                    Bitmap::Pixel color, uint8_t alphaThreshold, int dilatePx) {
    const int w = target.width();
    const int h = target.height();
    if (w <= 0 || h <= 0) return {};
    if (seedX < 0 || seedX >= w || seedY < 0 || seedY >= h) return {};
    if (isBlocked(boundaryLayers, seedX, seedY, alphaThreshold)) return {};  // 線の上をクリックした

    // BFSで連結した空き領域を収集する
    std::vector<uint8_t> region(static_cast<size_t>(w) * h, 0);
    std::queue<std::pair<int, int>> frontier;
    const auto idx = [w](int x, int y) { return static_cast<size_t>(y) * w + x; };

    region[idx(seedX, seedY)] = 1;
    frontier.push({seedX, seedY});
    DirtyRect dirty{seedX, seedY, seedX + 1, seedY + 1};

    while (!frontier.empty()) {
        const auto [x, y] = frontier.front();
        frontier.pop();

        constexpr int dx[] = {1, -1, 0, 0};
        constexpr int dy[] = {0, 0, 1, -1};
        for (int d = 0; d < 4; ++d) {
            const int nx = x + dx[d];
            const int ny = y + dy[d];
            if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
            if (region[idx(nx, ny)]) continue;
            if (isBlocked(boundaryLayers, nx, ny, alphaThreshold)) continue;
            region[idx(nx, ny)] = 1;
            frontier.push({nx, ny});
        }
        dirty.unite({x, y, x + 1, y + 1});
    }

    // 線の縁(アンチエイリアス部)のハローを防ぐため、領域をdilatePxだけ膨張させる。
    // 膨張分は線の半透明部の下に潜り込み、上に重なる主線で隠れる
    for (int pass = 0; pass < dilatePx; ++pass) {
        std::vector<uint8_t> grown = region;
        const int y0 = std::max(0, dirty.y0 - dilatePx);
        const int y1 = std::min(h, dirty.y1 + dilatePx);
        const int x0 = std::max(0, dirty.x0 - dilatePx);
        const int x1 = std::min(w, dirty.x1 + dilatePx);
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                if (region[idx(x, y)]) continue;
                const bool nearRegion = (x > 0 && region[idx(x - 1, y)]) || (x + 1 < w && region[idx(x + 1, y)]) ||
                                        (y > 0 && region[idx(x, y - 1)]) || (y + 1 < h && region[idx(x, y + 1)]);
                if (nearRegion) {
                    grown[idx(x, y)] = 1;
                    dirty.unite({x, y, x + 1, y + 1});
                }
            }
        }
        region.swap(grown);
    }

    // 領域をターゲットレイヤーへ塗る(不透明)
    for (int y = dirty.y0; y < dirty.y1; ++y) {
        for (int x = dirty.x0; x < dirty.x1; ++x) {
            if (region[idx(x, y)]) target.setPixel(x, y, color);
        }
    }

    dirty.clampTo(w, h);
    return dirty;
}

}  // namespace core
