#include "Project.h"

namespace core {

Scene& Project::addScene(std::string name) {
    m_scenes.push_back(std::make_unique<Scene>(std::move(name)));
    return *m_scenes.back();
}

void Project::removeScene(size_t index) {
    m_scenes.erase(m_scenes.begin() + static_cast<ptrdiff_t>(index));
}

}  // namespace core
