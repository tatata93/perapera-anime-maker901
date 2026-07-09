#include "Cel.h"

namespace core {

Layer& Cel::addLayer(std::string name) {
    m_layers.push_back(std::make_unique<Layer>(std::move(name)));
    return *m_layers.back();
}

void Cel::removeLayer(size_t index) {
    m_layers.erase(m_layers.begin() + static_cast<ptrdiff_t>(index));
}

void Cel::moveLayer(size_t from, size_t to) {
    if (from >= m_layers.size() || to >= m_layers.size() || from == to) return;
    std::unique_ptr<Layer> moved = std::move(m_layers[from]);
    m_layers.erase(m_layers.begin() + static_cast<ptrdiff_t>(from));
    m_layers.insert(m_layers.begin() + static_cast<ptrdiff_t>(to), std::move(moved));
}

}  // namespace core
