#include "Cel.h"

#include <algorithm>

namespace core {

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

void Cel::applyStepPattern(int step, size_t frameCount) {
    if (step < 1) step = 1;
    m_exposure.assign(frameCount, -1);
    const int drawings = static_cast<int>(drawingCount());
    if (drawings == 0) return;
    for (size_t t = 0; t < frameCount; ++t) {
        const int drawing = static_cast<int>(t) / step;
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

}  // namespace core
