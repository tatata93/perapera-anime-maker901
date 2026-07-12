#include "Cut.h"

#include <algorithm>

namespace core {

void Cut::setFrameCount(size_t count) {
    m_frameCount = std::max<size_t>(1, count);
    // 全セルの露出表の長さを尺に揃える
    for (auto& cel : m_cels) {
        cel->resizeExposure(m_frameCount);
    }
}

Cel& Cut::addCel(std::string name) {
    m_cels.push_back(std::make_unique<Cel>(std::move(name)));
    m_cels.back()->resizeExposure(m_frameCount);
    return *m_cels.back();
}

void Cut::removeCel(size_t index) {
    m_cels.erase(m_cels.begin() + static_cast<ptrdiff_t>(index));
}

void Cut::moveCel(size_t from, size_t to) {
    if (from >= m_cels.size() || to >= m_cels.size() || from == to) return;
    std::unique_ptr<Cel> moved = std::move(m_cels[from]);
    m_cels.erase(m_cels.begin() + static_cast<ptrdiff_t>(from));
    m_cels.insert(m_cels.begin() + static_cast<ptrdiff_t>(to), std::move(moved));
}

std::optional<CameraFrameState> Cut::cameraFrameAt(size_t frame) const {
    if (m_cameraKeys.empty()) return std::nullopt;

    // frame以上の最初のキーを探す(Cel::positionAtと同じ規則)
    const auto upper = m_cameraKeys.lower_bound(frame);
    if (upper == m_cameraKeys.begin()) return upper->second;          // 最初のキーより前
    if (upper == m_cameraKeys.end()) return std::prev(upper)->second;  // 最後のキーより後
    if (upper->first == frame) return upper->second;                   // キー上

    // 前後のキーで線形補間(等速)
    const auto lower = std::prev(upper);
    const float t = static_cast<float>(frame - lower->first) / static_cast<float>(upper->first - lower->first);
    const CameraFrameState& a = lower->second;
    const CameraFrameState& b = upper->second;
    CameraFrameState result;
    result.center = {a.center.x + (b.center.x - a.center.x) * t, a.center.y + (b.center.y - a.center.y) * t};
    result.scale = a.scale + (b.scale - a.scale) * static_cast<double>(t);
    return result;
}

void Cut::setCameraKey(size_t frame, CameraFrameState state) {
    state.scale = std::max(0.05, state.scale);
    m_cameraKeys[frame] = state;
}

double MultiplaneSetup::valueAt(const std::map<size_t, double>& keys, size_t frame, double base) {
    if (keys.empty()) return base;  // キーフレームが無ければ基本値をそのまま使う

    // frame以上の最初のキーを探す(Cut::cameraFrameAt/Effect::valueAtと同じクランプ規則)
    const auto upper = keys.lower_bound(frame);
    if (upper == keys.begin()) return upper->second;          // 最初のキーより前
    if (upper == keys.end()) return std::prev(upper)->second;  // 最後のキーより後
    if (upper->first == frame) return upper->second;           // キー上

    // 前後のキーで線形補間する
    const auto lower = std::prev(upper);
    const double t = static_cast<double>(frame - lower->first) / static_cast<double>(upper->first - lower->first);
    return lower->second + (upper->second - lower->second) * t;
}

}  // namespace core
