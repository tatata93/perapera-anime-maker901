#include "Cut.h"

namespace core {

Layer& Cut::addLayer(std::string name) {
    m_layers.push_back(std::make_unique<Layer>(std::move(name)));
    return *m_layers.back();
}

void Cut::removeLayer(size_t index) {
    m_layers.erase(m_layers.begin() + static_cast<ptrdiff_t>(index));
}

}  // namespace core
