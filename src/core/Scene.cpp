#include "Scene.h"

namespace core {

Cut& Scene::addCut(std::string name) {
    m_cuts.push_back(std::make_unique<Cut>(std::move(name)));
    return *m_cuts.back();
}

void Scene::removeCut(size_t index) {
    m_cuts.erase(m_cuts.begin() + static_cast<ptrdiff_t>(index));
}

}  // namespace core
