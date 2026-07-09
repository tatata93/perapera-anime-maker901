#include "Cut.h"

namespace core {

Cel& Cut::addCel(std::string name) {
    m_cels.push_back(std::make_unique<Cel>(std::move(name)));
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
