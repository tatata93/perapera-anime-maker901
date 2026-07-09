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

}  // namespace core
