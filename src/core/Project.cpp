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

SettingBoard& Project::duplicateSettingBoard(size_t index, std::string name) {
    SettingBoard copy = m_settingBoards.at(index);
    copy.name = std::move(name);
    auto it =
        m_settingBoards.insert(m_settingBoards.begin() + static_cast<ptrdiff_t>(index + 1), std::move(copy));
    return *it;
}

void Project::setCanvasSize(int width, int height) {
    m_canvasWidth = std::clamp(width, kMinCanvasSize, kMaxCanvasSize);
    m_canvasHeight = std::clamp(height, kMinCanvasSize, kMaxCanvasSize);
}

}  // namespace core
