#include "Cel.h"

#include <algorithm>

namespace core {

namespace {

// srcを中央基準でdstへコピーする(dstはあらかじめ透明で初期化されている前提)。
// dstの方が小さい方向ははみ出す部分を切り捨てる(同じ式で拡大・縮小の両方に対応する)
void copyCentered(const Bitmap& src, Bitmap& dst) {
    const int offsetX = (dst.width() - src.width()) / 2;
    const int offsetY = (dst.height() - src.height()) / 2;
    const int srcX0 = std::max(0, -offsetX);
    const int srcY0 = std::max(0, -offsetY);
    const int srcX1 = std::min(src.width(), dst.width() - offsetX);
    const int srcY1 = std::min(src.height(), dst.height() - offsetY);
    for (int y = srcY0; y < srcY1; ++y) {
        for (int x = srcX0; x < srcX1; ++x) {
            dst.setPixel(x + offsetX, y + offsetY, src.pixel(x, y));
        }
    }
}

}  // namespace

Layer& Cel::addLayer(std::string name) {
    m_layers.push_back(std::make_unique<Layer>(std::move(name)));
    return *m_layers.back();
}

void Cel::removeLayer(size_t index) {
    m_layers.erase(m_layers.begin() + static_cast<ptrdiff_t>(index));
}

void Cel::setExposure(size_t frame, int drawing) {
    if (frame >= m_exposure.size()) m_exposure.resize(frame + 1, -1);
    m_exposure[frame] = drawing;
}

void Cel::applyStepPattern(int step, size_t frameCount, size_t startFrame) {
    if (step < 1) step = 1;
    m_exposure.assign(frameCount, -1);
    const int drawings = static_cast<int>(drawingCount());
    if (drawings == 0) return;
    if (startFrame >= frameCount) return;
    for (size_t t = startFrame; t < frameCount; ++t) {
        const int drawing = static_cast<int>(t - startFrame) / step;
        if (drawing >= drawings) break;  // 動画が尽きたら以降は空欄のまま
        m_exposure[t] = drawing;
    }
}

Vec2 Cel::positionAt(size_t frame) const {
    if (m_positionKeys.empty()) return {};

    // frame以上の最初のキーを探す
    const auto upper = m_positionKeys.lower_bound(frame);
    if (upper == m_positionKeys.begin()) return upper->second;          // 最初のキーより前
    if (upper == m_positionKeys.end()) return std::prev(upper)->second;  // 最後のキーより後
    if (upper->first == frame) return upper->second;                     // キー上

    // 前後のキーで線形補間(等速)
    const auto lower = std::prev(upper);
    const float t = static_cast<float>(frame - lower->first) / static_cast<float>(upper->first - lower->first);
    return {lower->second.x + (upper->second.x - lower->second.x) * t,
            lower->second.y + (upper->second.y - lower->second.y) * t};
}

void Cel::moveLayer(size_t from, size_t to) {
    if (from >= m_layers.size() || to >= m_layers.size() || from == to) return;
    std::unique_ptr<Layer> moved = std::move(m_layers[from]);
    m_layers.erase(m_layers.begin() + static_cast<ptrdiff_t>(from));
    m_layers.insert(m_layers.begin() + static_cast<ptrdiff_t>(to), std::move(moved));
}

void Cel::resizePaper(int newW, int newH) {
    if (newW <= 0 || newH <= 0) return;  // 不正値は無視する

    for (auto& layer : m_layers) {
        for (size_t fi = 0; fi < layer->frameCount(); ++fi) {
            Bitmap& bitmap = layer->frame(fi).bitmap();
            if (bitmap.isEmpty()) continue;  // 空コマ(未描画)はそのまま
            Bitmap resized(newW, newH);
            resized.fill({0, 0, 0, 0});
            copyCentered(bitmap, resized);
            bitmap = std::move(resized);
        }
    }
    m_paperWidth = newW;
    m_paperHeight = newH;
}

}  // namespace core
