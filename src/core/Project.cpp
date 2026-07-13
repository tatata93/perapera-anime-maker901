#include "Project.h"

#include <algorithm>

namespace core {

namespace {
constexpr int kMinCanvasSize = 16;
constexpr int kMaxCanvasSize = 8192;
}  // namespace

Scene& Project::addScene(std::string name) {
    m_scenes.push_back(std::make_unique<Scene>(std::move(name)));
    return *m_scenes.back();
}

void Project::removeScene(size_t index) {
    m_scenes.erase(m_scenes.begin() + static_cast<ptrdiff_t>(index));
}

void Project::setCanvasSize(int width, int height) {
    m_canvasWidth = std::clamp(width, kMinCanvasSize, kMaxCanvasSize);
    m_canvasHeight = std::clamp(height, kMinCanvasSize, kMaxCanvasSize);
}

}  // namespace core
