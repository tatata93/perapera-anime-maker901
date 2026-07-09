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

void Cel::moveLayer(size_t from, size_t to) {
    if (from >= m_layers.size() || to >= m_layers.size() || from == to) return;
    std::unique_ptr<Layer> moved = std::move(m_layers[from]);
    m_layers.erase(m_layers.begin() + static_cast<ptrdiff_t>(from));
    m_layers.insert(m_layers.begin() + static_cast<ptrdiff_t>(to), std::move(moved));
}

}  // namespace core
