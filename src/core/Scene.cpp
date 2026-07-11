#include "Scene.h"

namespace core {

Cut& Scene::addCut(std::string name) {
    m_cuts.push_back(std::make_unique<Cut>(std::move(name)));
    return *m_cuts.back();
}

void Scene::removeCut(size_t index) {
    m_cuts.erase(m_cuts.begin() + static_cast<ptrdiff_t>(index));
}

void Scene::moveCut(size_t from, size_t to) {
    if (from >= m_cuts.size() || to >= m_cuts.size() || from == to) return;
    std::unique_ptr<Cut> moved = std::move(m_cuts[from]);
    m_cuts.erase(m_cuts.begin() + static_cast<ptrdiff_t>(from));
    m_cuts.insert(m_cuts.begin() + static_cast<ptrdiff_t>(to), std::move(moved));
}

}  // namespace core
