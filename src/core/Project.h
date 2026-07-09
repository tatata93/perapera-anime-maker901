#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Bitmap.h"
#include "Scene.h"

namespace core {

// アニメーション制作プロジェクト全体。シーンを順序付きで保持する。
class Project {
public:
    explicit Project(std::string name = "Untitled") : m_name(std::move(name)) {}

    const std::string& name() const { return m_name; }
    void setName(std::string name) { m_name = std::move(name); }

    Scene& addScene(std::string name);
    void removeScene(size_t index);

    size_t sceneCount() const { return m_scenes.size(); }
    Scene& scene(size_t index) { return *m_scenes.at(index); }
    const Scene& scene(size_t index) const { return *m_scenes.at(index); }

    // カラーパレット(登録色の一覧)。既定は空
    std::vector<Bitmap::Pixel>& palette() { return m_palette; }
    const std::vector<Bitmap::Pixel>& palette() const { return m_palette; }

private:
    std::string m_name;
    std::vector<std::unique_ptr<Scene>> m_scenes;
    std::vector<Bitmap::Pixel> m_palette;
};

}  // namespace core
